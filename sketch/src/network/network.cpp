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
static TaskHandle_t wifiTask = NULL;

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

// ---------------------- LOAD WIFI LIST ----------------------
int loadWifiList(WifiEntry list[]) {

  // Ensure wifi.json exists
  if (!LittleFS.exists("/wifi.json")) {
    log_w("wifi.json not found → creating empty file");

    File f = LittleFS.open("/wifi.json", "w");
    if (!f) {
      log_e("Failed to create wifi.json");
      return false;
    }

    f.print("[]"); // create empty JSON array
    f.close();
  }

  File f = LittleFS.open(WIFI_FILE, "r");
  if (!f) return 0;

  static JsonDocument doc;
  doc.clear();
  DeserializationError err = deserializeJson(doc, f);

  f.close();

  if (err) {
    log_w("JSON parse failed → recreating file.");
    File f2 = LittleFS.open(WIFI_FILE, "w");
    f2.print("[]");
    f2.close();
    return 0;
  }

  log_d("Wi-Fi Credential List");
  int count = doc.size();
  if (count > WIFI_MAX) count = WIFI_MAX;
  for (int i = 0; i < count; i++) {

    const char *ssid = doc[i]["ssid"];
    const char *password = doc[i]["password"];

    if (ssid) {
      strncpy(list[i].ssid, ssid, sizeof(list[i].ssid) - 1);
      list[i].ssid[sizeof(list[i].ssid) - 1] = '\0';
    } else {
      list[i].ssid[0] = '\0';
    }

    if (password) {
      strncpy(list[i].password, password, sizeof(list[i].password) - 1);
      list[i].password[sizeof(list[i].password) - 1] = '\0';
    } else {
      list[i].password[0] = '\0';
    }

    char txt[128];
    snprintf(txt, sizeof(txt), "%d: %s, %s", i + 1, list[i].ssid, list[i].password); // debug print "ssid, password
    log_d("%s", txt);
  }

  return count;
}

// ---------------------- SAVE WIFI LIST ----------------------
void saveWifiList(WifiEntry list[], int count) {
  static JsonDocument doc;
  doc.clear();

  JsonArray arr = doc.to<JsonArray>();

  for (int i = 0; i < count; i++) {
    JsonObject obj = arr.add<JsonObject>();
    obj["ssid"] = list[i].ssid;
    obj["password"] = list[i].password;
  }

  File f = LittleFS.open(WIFI_FILE, "w");
  if (!f) {
    log_e("Failed to open WIFI_FILE for writing");
    return;
  }

  serializeJson(doc, f);
  f.close();
}

// ---------------------- ADD / UPDATE ENTRY ----------------------
int addOrUpdateWifi(const char *ssid, const char *password, WifiEntry list[], int count) {
  // 1) Check if SSID already exists
  for (int i = 0; i < count; i++) {
    if (strcmp(list[i].ssid, ssid) == 0) {

      // Update password + move to top
      for (int j = i; j > 0; j--) list[j] = list[j - 1];
      strncpy(list[0].ssid, ssid, sizeof(list[0].ssid) - 1);
      list[0].ssid[sizeof(list[0].ssid) - 1] = '\0';
      strncpy(list[0].password, password, sizeof(list[0].password) - 1);
      list[0].password[sizeof(list[0].password) - 1] = '\0';
      return count;
    }
  }

  // 2) New SSID
  if (count < WIFI_MAX) {
    // Shift downward
    for (int i = count; i > 0; i--) list[i] = list[i - 1];
    strncpy(list[0].ssid, ssid, sizeof(list[0].ssid) - 1);
    list[0].ssid[sizeof(list[0].ssid) - 1] = '\0';
    strncpy(list[0].password, password, sizeof(list[0].password) - 1);
    list[0].password[sizeof(list[0].password) - 1] = '\0';
    return count + 1;
  } else {
    // Overwrite oldest (index 9)
    for (int i = WIFI_MAX - 1; i > 0; i--) list[i] = list[i - 1];
    strncpy(list[0].ssid, ssid, sizeof(list[0].ssid) - 1);
    list[0].ssid[sizeof(list[0].ssid) - 1] = '\0';
    strncpy(list[0].password, password, sizeof(list[0].password) - 1);
    list[0].password[sizeof(list[0].password) - 1] = '\0';
    return WIFI_MAX;
  }
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
    // 1. บังคับปิดและลบข้อมูลสแกนเดิม
    WiFi.scanDelete();
    WiFi.disconnect(true); // ปิดการเชื่อมต่อเดิมทั้งหมด
    WiFi.mode(WIFI_OFF); // ปิดวิทยุชั่วคราว
    vTaskDelay(pdMS_TO_TICKS(100));
    // 2. เริ่มต้นระบบ Wi-Fi ใหม่
    WiFi.mode(WIFI_STA);
    vTaskDelay(pdMS_TO_TICKS(200)); // ให้เวลา Driver ตื่นขึ้นมา
    log_d("Scanning for Wi-Fi networks (blocking): ");
    // 3. สั่งสแกน
    int16_t scanResult = WiFi.scanNetworks();

    if (scanResult < 0) {
      log_e("Scan failed with error code: %d", scanResult);
      // ถ้ายังล้มเหลว ลองสั่ง WiFi.scanDelete() อีกรอบเพื่อ Reset
      WiFi.scanDelete();
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
// check connection status and attemp to connect every 30 second
void wifi_connect_task(void *param) {

  for (;;) {
    // Wifi disconnected
    if (WiFi.status() != WL_CONNECTED) {
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
                log_d("Match found: %s", ss.c_str());
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
              snprintf(connectedMsg, sizeof(connectedMsg), "Connected to %s (IP: %s)", networkName, WiFi.localIP());
              log_i("%s", connectedMsg);
              updateWiFiStatus(connectedMsg, 0x00FF00, 0x0000FF);
              // audioPlayFS(1, "/audio/ding.mp3");
              rtc.ntp_sync(UTC_offset_hour[offset_hour_index], UTC_offset_minute[offset_minute_index]);
              rtc.calibratBySeconds(0, 0.0); // mode 0 (eery 2 second, diff_time/total_calibrate_time)
                                             // check if new firmware available
              if (!firmware_checked) {
                const char *latestVer = newFirmwareAvailable();
                if (latestVer != NULL) {
                  char title[48];
                  snprintf(title, sizeof(title), LV_SYMBOL_REFRESH " New Update Available %s", latestVer);
                  // notify user about new firmware available
                  char *title_copy = strdup(title);
                  lv_async_call(
                      [](void *p) {
                        const char *t = (const char *)p;
                        lv_obj_t *msgBox = lv_msgbox_create(NULL, t, "To update firmware, please click\nUtilities -> System Information", NULL, true);
                        lv_obj_set_width(msgBox, 420);
                        lv_obj_set_style_bg_opa(msgBox, LV_OPA_90, LV_PART_MAIN);
                        lv_obj_center(msgBox);
                        lv_obj_t *titleObj = lv_msgbox_get_title(msgBox);
                        lv_obj_t *textObj = lv_msgbox_get_text(msgBox);
                        lv_obj_t *closeBtn = lv_msgbox_get_close_btn(msgBox);
                        lv_obj_set_style_text_font(titleObj, &lv_font_montserrat_18, LV_PART_MAIN);
                        lv_obj_set_style_text_font(textObj, &lv_font_montserrat_18, LV_PART_MAIN);
                        lv_obj_set_size(closeBtn, 48, 48);
                        lv_obj_set_style_text_font(closeBtn, &lv_font_montserrat_28, LV_PART_MAIN);
                        lv_obj_clear_flag(ui_Utility_Button_UpdateFirmware, LV_OBJ_FLAG_HIDDEN); // show update button

                        free((void *)t); // free heap copy
                      },
                      title_copy);
                } // newfirmwareAvailable
              } // firmware checked
              updateWeatherPanel(); // update weather condition once after internet connected

            } else { // Wifi not connected
              log_w("Wrong Wi-Fi password or timeout for %s", networkName);
              updateWiFiStatus("Wrong Wi-Fi password or timeout", 0xFF0000, 0x777777);
            }
          } // for each match
        } // If we
      }
    } // wifi connected
    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    log_d("{ Task stack remaining MIN: %u bytes }", hwm);
    vTaskDelay(pdMS_TO_TICKS(30000));
  }
}

void wifiConnect() {
  enableTlsInPsram();
  if (wifiTask == NULL) xTaskCreatePinnedToCore(wifi_connect_task, "wifi_connect_task", 6 * 1024, NULL, 1, &wifiTask, 0);
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
