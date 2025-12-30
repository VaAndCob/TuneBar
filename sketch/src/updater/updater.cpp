#include "network/network.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

const char *compile_date = __DATE__ " - " __TIME__;
const char *current_version = "1.1.0";

bool firmware_checked = false; // check firmware flag

//------------------------------------------------

// function return firmware version to integer number
int versionToNumber(const char *version) {
  if (!version || !*version) return 0;
  int major = 0, minor = 0, patch = 0;
  const char *p = version;
  // major
  while (*p && *p != '.') {
    if (*p >= '0' && *p <= '9') major = major * 10 + (*p - '0');
    p++;
  }
  if (*p == '.')
    p++;
  else
    return major * 10000;
  // minor
  while (*p && *p != '.') {
    if (*p >= '0' && *p <= '9') minor = minor * 10 + (*p - '0');
    p++;
  }
  if (*p == '.')
    p++;
  else
    return major * 10000 + minor * 100;
  // patch
  while (*p) {
    if (*p >= '0' && *p <= '9') patch = patch * 10 + (*p - '0');
    p++;
  }
  return major * 10000 + minor * 100 + patch;
}

//----------------------------------------
// check a new firmware version available on github
bool newFirmwareAvailable() {
  log_i("Checking firmware updates - Current version: %s", current_version);

#define MAX_URL_LENGTH 256 // กำหนดขนาดบัฟเฟอร์สูงสุดที่ปลอดภัย
#define JSON_BUF_SIZE 1024

  // fetch the latest version from manifest.json
  static char reqURL[MAX_URL_LENGTH];
  snprintf(reqURL, sizeof(reqURL), "https://vaandcob.github.io/webpage/firmware/tunebar/tunebar_manifest.json");
  //snprintf(reqURL, sizeof(reqURL), "https://cdn.jsdelivr.net/gh/VaAndCob/webpage@main/firmware/tunebar/tunebar_manifest.json");

  static char jsonBuf[JSON_BUF_SIZE];
  static StaticJsonDocument<JSON_BUF_SIZE > doc;

  // fetch weather condition data from weather API
  fetchUrlData(reqURL, true ,jsonBuf, JSON_BUF_SIZE);
  sanitizeJson(jsonBuf);
  DeserializationError error = deserializeJson(doc, jsonBuf);

  
  if (error) {
    log_e("manifest JSON parse failed: %s", error.c_str());
    return false;
  }

  // Check if the "version" key exists
  if (!doc["version"].isNull()) {
    const char *latestVersion = doc["version"]; // Get as const char*
    log_i("Latest firmware: %s", latestVersion);
    firmware_checked = true;

    // version comparision
    if (versionToNumber(latestVersion) > versionToNumber(current_version)) {
      log_w("-> New firmware version available");
      return true;
    } else {
      log_i("-> Firmware is up to date");
      return false;
    }
  } else {
    log_e("Manifest JSON does not contain 'version' key.");
    return false;
  }
  return false; // Default return if something went wrong or no new version
}

//-----------------------------------------
// online update from github
bool performOnlineUpdate() { // download file from github and update
  log_i("Perform OTA update...");
  const char *firmwareURL = "https://vaandcob.github.io/webpage/firmware/tunebar/tunebar.bin";
  int last_progress = 0;
  HTTPClient client;
  client.begin(firmwareURL);

  int httpCode = client.GET(); // request file

  if (httpCode == HTTP_CODE_OK) { // 200
    uint32_t fileSize = client.getSize();
    Serial.printf("Firmware Size: %d bytes\n", fileSize);

    if (Update.begin(fileSize)) { // start updating

      uint8_t buff[1024] = {0}; // create buffer for read
      WiFiClient *stream = client.getStreamPtr(); // get tcp stream

      Update.onProgress([&](size_t progress, size_t total) { // get update progress
        uint8_t curr_progress = round(progress / (total / 100));
        if (curr_progress != last_progress) { // if progress not change then skip update progress bar
          log_i("%d %", curr_progress);
          last_progress = curr_progress;
        }
      });

      while (client.connected() && (fileSize > 0)) {
        size_t size = stream->available(); // get available data size
        if (size) { // read up to 1024 byte
          int c = stream->readBytes(buff, ((size > sizeof(buff)) ? sizeof(buff) : size));
          Update.write(buff, c);
          if (fileSize > 0) fileSize -= c; // calculate remaining byte
        } // if size
        delay(1);
      }
      if (fileSize == 0) {
        Update.end(true);
        log_i("✅ Update Successful... Restarting.");
        esp_ota_mark_app_valid_cancel_rollback(); // Prevent from booting the old firmware
        client.end();
        return true;
      }
    } else { // update.begin
      log_e("⚠️ Error: Not enough space!");
      client.end();
      return false;
    }

  } else { // error not 200
    log_e("HTTP request failed. HTTP code: %d\n", httpCode);
    client.end();
    return false;
  }
  client.end();
  return false;
} // performUpdate
//-------------------------------
