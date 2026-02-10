#include "network.h"
#include "esp32-hal.h"
#include "file/file.h"
#include "pcf85063/pcf85063.h"
#include "task_msg/task_msg.h"
#include "ui/ui.h"
#include "updater/updater.h"
#include "weather/weather.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdint.h>

extern PCF85063 rtc;

#define WIFI_CONNECT_TIMEOUT_MS 5000
#define WIFI_MAX 10
#define WIFI_FILE "/wifi.json"

WifiEntry wifiList[WIFI_MAX];

byte networks = 0;
TaskHandle_t wifiTaskHandle = NULL;
bool wifiEnable = false;
bool wifi_need_connect = false;
lv_timer_t *wifi_check_timer = NULL;

//-----------------------------------------------------------
// Enable mbedTLS to use PSRAM for dynamic memory allocation instead of internal RAM
// due to limited internal RAM size on ESP32 must larger than 40kb for TLS operation
static void *psram_calloc(size_t n, size_t size) {
  return heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
}

static void psram_free(void *ptr) {
  heap_caps_free(ptr);
}

void enableTlsInPsram() {
  mbedtls_platform_set_calloc_free(psram_calloc, psram_free);
}

// sanitize wifi ssid
static void sanitize_string(char *s) {
  if (!s) return;

  char *dst = s;
  for (char *src = s; *src; src++) {
    uint8_t c = (uint8_t)*src;
    if (c >= 32 && c != 127) { // keep printable ASCII only
      *dst++ = *src;
    }
  }
  *dst = '\0';

  // Trim trailing spaces
  while (dst > s && isspace((unsigned char)dst[-1])) {
    *--dst = '\0';
  }
}

// compare ssid
static bool ssid_equals(const char *a, const char *b) {
  if (!a || !b) return false;
  return strcmp(a, b) == 0;
}

// load wifi credential from wifi.json
int loadWifiList(WifiEntry list[]) {
  if (!LittleFS.exists(WIFI_FILE)) {
    log_w("wifi.json not found → creating empty list");
    File f = LittleFS.open(WIFI_FILE, "w");
    if (f) {
      f.print("[]");
      f.close();
    }
    return 0;
  }

  File f = LittleFS.open(WIFI_FILE, "r");
  if (!f) {
    log_e("Failed to open wifi.json");
    return 0;
  }

  static JsonDocument doc;
  doc.clear();
  DeserializationError err = deserializeJson(doc, f);
  f.close();

  if (err || !doc.is<JsonArray>()) {
    log_w("wifi.json invalid → reset");
    File f2 = LittleFS.open(WIFI_FILE, "w");
    if (f2) {
      f2.print("[]");
      f2.close();
    }
    return 0;
  }

  int count = 0;

  for (JsonObject obj : doc.as<JsonArray>()) {
    if (count >= WIFI_MAX) break;

    const char *ssid = obj["ssid"];
    const char *password = obj["password"];

    if (!ssid || !*ssid) continue;

    strncpy(list[count].ssid, ssid, sizeof(list[count].ssid) - 1);
    strncpy(list[count].password, password ? password : "", sizeof(list[count].password) - 1);

    list[count].ssid[sizeof(list[count].ssid) - 1] = '\0';
    list[count].password[sizeof(list[count].password) - 1] = '\0';

    sanitize_string(list[count].ssid);
    sanitize_string(list[count].password);

    log_d("WiFi[%d] SSID='%s'", count, list[count].ssid);
    count++;
  }
  return count;
}

// SAVE WIFI LIST
void saveWifiList(const WifiEntry list[], int count) {
  static JsonDocument doc;
  doc.clear();
  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < count; i++) {
    if (!list[i].ssid[0]) continue;

    JsonObject o = arr.add<JsonObject>();
    o["ssid"] = list[i].ssid;
    o["password"] = list[i].password;
  }
  File f = LittleFS.open(WIFI_FILE, "w");
  if (!f) {
    log_e("Failed to write wifi.json");
    return;
  }
  serializeJson(doc, f);
  f.close();
  log_d("Saved %d WiFi entries", count);
}

// ADD / UPDATE ENTRY
int addOrUpdateWifi(const char *newSSID, const char *newPassword, WifiEntry list[], int count) {
  if (!newSSID || !*newSSID) return count;

  char ssid[64];
  char password[64];

  strncpy(ssid, newSSID, sizeof(ssid) - 1);
  strncpy(password, newPassword ? newPassword : "", sizeof(password) - 1);
  ssid[sizeof(ssid) - 1] = '\0';
  password[sizeof(password) - 1] = '\0';

  sanitize_string(ssid);
  sanitize_string(password);

  log_i("Adding/Updating WiFi: '%s':'%s'", ssid, password);

  // 1) Find existing
  int found = -1;
  for (int i = 0; i < count; i++) {
    if (ssid_equals(list[i].ssid, ssid)) {
      found = i;
      break;
    }
  }

  // 2) Prepare new entry
  WifiEntry entry{};
  strncpy(entry.ssid, ssid, sizeof(entry.ssid) - 1);
  strncpy(entry.password, password, sizeof(entry.password) - 1);

  // 3) Remove old copy if exists
  if (found >= 0) {
    log_d("Updating existing SSID at index %d", found);
    for (int i = found; i > 0; i--) {
      list[i] = list[i - 1];
    }
    list[0] = entry;
    return count;
  }

  // 4) New entry
  log_d("Adding new SSID");

  if (count < WIFI_MAX) {
    for (int i = count; i > 0; i--) {
      list[i] = list[i - 1];
    }
    list[0] = entry;
    return count + 1;
  }

  // 5) List full → drop oldest
  for (int i = WIFI_MAX - 1; i > 0; i--) {
    list[i] = list[i - 1];
  }
  list[0] = entry;
  return WIFI_MAX;
}

// Discovery wifi network
void scanWiFi(bool updateList) {
  if (updateList) updateWiFiOption("Scaning..."); // clear wifi dropdown

  bool isAsync = (WiFi.status() == WL_CONNECTED);

  if (isAsync) {
    // --- Asynchronous Scan ---
    log_d("Scanning for Wi-Fi networks (async): ");
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
      log_e("Async scan failed to start");
      networks = 0;
      return;
    }

    unsigned long startAttemptTime = millis();
    int16_t scanResult;
    while ((scanResult = WiFi.scanComplete()) == WIFI_SCAN_RUNNING) {
      if (millis() - startAttemptTime > WIFI_CONNECT_TIMEOUT_MS) {
        WiFi.scanDelete(); // Clean up even on timeout
        networks = 0;
        log_e("Async scan timed out.");
        return;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
    }
    networks = (scanResult > 0) ? scanResult : 0;

  } else {
    // --- Synchronous (Blocking) Scan ---
    log_d("Preparing for blocking scan...");
    
    WiFi.scanDelete();// 1. force close and rescan
    WiFi.disconnect(true); // close all connection
    WiFi.mode(WIFI_OFF); // temporaly turn off radio
    vTaskDelay(pdMS_TO_TICKS(100));
    
    WiFi.mode(WIFI_STA);// 2. restat wifi again
    vTaskDelay(pdMS_TO_TICKS(200));
    log_d("Scanning for Wi-Fi networks (blocking): ");
    
    int16_t scanResult = WiFi.scanNetworks();// 3. scan the network

    if (scanResult < 0) {
      log_e("Scan failed with error code: %d", scanResult);
      WiFi.scanDelete();// if failed try scandelete again
      networks = 0;
    } else {
      networks = (uint8_t)scanResult;
      log_d("%d networks found", networks);
    }
  }
  // --- Process and Display Results (common part) ---

  char SSIDs[1024];
  char txt[128];
  for (byte i = 0; i < networks; ++i) {
    snprintf(txt, sizeof(txt), "%d: %s  Ch %d (%d)", i + 1, WiFi.SSID(i).c_str(), WiFi.channel(i), WiFi.RSSI(i));
    log_d("%s", txt);
    if (i != networks - 1)
      snprintf(SSIDs, sizeof(SSIDs), "%s%s\n", SSIDs, WiFi.SSID(i).c_str());
    else
      snprintf(SSIDs, sizeof(SSIDs), "%s%s", SSIDs, WiFi.SSID(i).c_str());
  }
  SSIDs[sizeof(SSIDs) - 1] = '\0';

  if (updateList) updateWiFiOption(SSIDs); // add wifi dropdown
  // --- Cleanup ---
  if (isAsync) {
    WiFi.scanDelete(); // Crucial for async scan
  }
}

//-----------------------------------------
// wifi task -> check connection status and attemp to connect every 30 second
void wifi_connect_task(void *param) {

  while (wifi_need_connect) {

    if (WiFi.getMode() != WIFI_STA) WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(100));
    uint8_t wifiCount = loadWifiList(wifiList);
    log_d("Loaded %d Wi-Fi credentials", wifiCount);
    updateWiFiStatus("Scanning...", 0x00FF00, 0x777777);

    scanWiFi(false); // scan wifi but don't update dropdown
    if (networks == 0) { // no network in this area
      log_d("No Wi-Fi networks found");
      updateWiFiStatus("No Wi-Fi networks found.", 0xFF0000, 0x777777);
      vTaskDelay(pdMS_TO_TICKS(300));

    } else {

      // check matching network with the list in wifi.json
      uint8_t matchIndex[10]; // keep the index of match ssid from wifiList.ssid
      uint8_t matchCount = 0;
      for (byte i = 0; i < networks; ++i) {
        char ss[64];
        strncpy(ss, WiFi.SSID(i).c_str(), sizeof(ss) - 1);
        ss[sizeof(ss) - 1] = '\0';

        log_d("Checking SSID: %s", ss);
        for (int k = 0; k < wifiCount; ++k) {

          char stored[64];
          strncpy(stored, wifiList[k].ssid, sizeof(stored) - 1);
          stored[sizeof(stored) - 1] = '\0';
          log_d("Stored SSID: %s", stored);

          if (strcmp(stored, ss) == 0) {
            if (matchCount < sizeof(matchIndex)) {
              matchIndex[matchCount++] = k;
              log_d("Match found: %s", ss);
            } else {
              log_w("More matches than buffer; ignoring extras");
            }
          }
        }
      } // for

      log_d("Total match: %d", matchCount);

      if (matchCount == 0) {
        log_d("No network matched.");
        updateWiFiStatus("No network matched.", 0xFF0000, 0x777777);
        vTaskDelay(pdMS_TO_TICKS(300));

      } else {

        // try to connect all matched wifi ssid
        for (uint8_t idx = 0; idx < matchCount; idx++) {
          char networkName[64];
          strncpy(networkName, wifiList[matchIndex[idx]].ssid, sizeof(networkName) - 1);
          networkName[sizeof(networkName) - 1] = '\0';

          char attemptMsg[128];
          snprintf(attemptMsg, sizeof(attemptMsg), "Attempt connecting to %s", networkName);
          log_i("%s", attemptMsg);
          updateWiFiStatus(attemptMsg, 0x00FF00, 0x0000FF);

          // attempt to connect wifi
          if (WiFi.status() == WL_CONNECTED) WiFi.disconnect(true);
          vTaskDelay(pdMS_TO_TICKS(100));
          WiFi.begin(wifiList[matchIndex[idx]].ssid, wifiList[matchIndex[idx]].password);

          unsigned long startAttemptTime = millis();
          while (WiFi.status() != WL_CONNECTED && (millis() - startAttemptTime < WIFI_CONNECT_TIMEOUT_MS)) {
            vTaskDelay(pdMS_TO_TICKS(500)); // shorter step for snappier responsiveness
            log_d(".");
          }
          // check wifi connection status
          if (WiFi.status() == WL_CONNECTED) {
            char connectedMsg[128];
            IPAddress ip = WiFi.localIP();
            snprintf(connectedMsg, sizeof(connectedMsg), "Connected to %s (IP: %u.%u.%u.%u)", networkName, ip[0], ip[1], ip[2], ip[3]);
            log_i("%s", connectedMsg);
            updateWiFiStatus(connectedMsg, 0x00FF00, 0x0000FF);

            rtc.ntp_sync(UTC_offset_hour[offset_hour_index], UTC_offset_minute[offset_minute_index]);
            rtc.calibratBySeconds(0, 0.0); // mode 0 (eery 2 second, diff_time/total_calibrate_time)
            // check if new firmware available

            if (!firmware_checked) {
              const char *latestVer = newFirmwareAvailable(); // get new firmware version
              if (latestVer != NULL) {
                notifyUpdate(latestVer); // notify user
              } // newfirmwareAvailable
            } // firmware checked
            updateWeatherPanel(); // update weather condition once after internet connected
            break;
          } else { // Wifi not connected
            log_w("Wrong Wi-Fi password or timeout for %s", networkName);
            updateWiFiStatus("Wrong Wi-Fi password or timeout", 0xFF0000, 0x777777);
          }
        } // for each match
      } // If we
    }

    if (WiFi.status() == WL_CONNECTED) {
      wifi_need_connect = false;

      lv_async_call(
          [](void *param) {
            lv_timer_t *timer = (lv_timer_t *)param;
            if (timer) {
              lv_timer_resume(timer);//resume wifi check timer
            }
          },
          wifi_check_timer);

      break;
    }
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    log_d("{ Task stack remaining MIN: %u bytes }", hwm);
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
  wifiTaskHandle = NULL;
  vTaskDelete(NULL);
}
// global wifi connect
void wifiConnect() {
  enableTlsInPsram();
  if (wifiTaskHandle == NULL) xTaskCreatePinnedToCore(wifi_connect_task, "wifi_connect_task", 6 * 1024, NULL, 1, &wifiTaskHandle, 1);
}

// sanitze JSON data by removing BOM and extraneous characters
void sanitizeJson(char *buf) {
  // 1) strip BOM if present
  if ((uint8_t)buf[0] == 0xEF && (uint8_t)buf[1] == 0xBB && (uint8_t)buf[2] == 0xBF) {
    memmove(buf, buf + 3, strlen(buf) - 2);
  }
  // 2) remove leading garbage before first '{' or '['
  char *start = buf;
  while (*start && *start != '{' && *start != '[') start++;

  if (start != buf) memmove(buf, start, strlen(start) + 1);
  // 3) truncate after last '}' or ']'
  char *endBrace = strrchr(buf, '}');
  char *endBracket = strrchr(buf, ']');
  char *end = endBrace;
  if (endBracket && endBracket > end) end = endBracket;
  if (end) *(end + 1) = '\0';
}

// -------------  Function to fetch data -------------------
bool fetchUrlData(const char *url, bool ssl, char *outBuf, size_t outBufSize) {

  if (WiFi.status() != WL_CONNECTED) return false;

  static HTTPClient httpPlain;
  static HTTPClient httpTLS;
  static WiFiClient client;
  static WiFiClientSecure clientSecure;

  HTTPClient *http = ssl ? &httpTLS : &httpPlain;

  http->end();

  if (ssl) {
    clientSecure.setInsecure();
    http->begin(clientSecure, url);
  } else {
    http->begin(client, url);
  }

  http->setConnectTimeout(3000); // TLS connection timeout

  int code = http->GET();
  if (code <= 0) {
    http->end();
    return false;
  }

  int len = http->getSize(); // >=0 = Content-Length, -1 = chunked
  WiFiClient *stream = http->getStreamPtr();

  size_t idx = 0;
  uint32_t deadline = millis() + 5000;

  while (millis() < deadline && idx < outBufSize - 1) {
    if (!stream->connected()) {
      log_w("Stream disconnected");
      break;
    }
    size_t toRead;

    if (len > 0) { // fast path: known length
      toRead = min((size_t)len, outBufSize - 1 - idx);
    } else { // chunked / unknown
      toRead = min((size_t)1024, outBufSize - 1 - idx);
    }

    int n = stream->readBytes(outBuf + idx, toRead);
    if (n <= 0) break;

    idx += n;

    if (len > 0) {
      len -= n;
      if (len == 0) break;
    }

    vTaskDelay(pdMS_TO_TICKS(1));
  }
  outBuf[idx] = '\0';
  log_d("Fetched %u bytes", (unsigned)idx);
  http->end();
  return (idx > 0);
}

//----------------------------------------------
