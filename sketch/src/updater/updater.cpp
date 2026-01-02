#include "network/network.h"
#include "ui/ui.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <LittleFS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_ota_ops.h>

const char *compile_date = __DATE__ " - " __TIME__;
const char *current_version = "1.1.0";

bool firmware_checked = false; // check firmware flag

static TaskHandle_t otaTaskHandle = NULL;
static lv_obj_t *modal_blocker = NULL;
static lv_obj_t *ota_popup = NULL;
static lv_obj_t *ota_bar = NULL;
static lv_obj_t *ota_msg = NULL;
static lv_obj_t *btn_close = NULL;

static volatile bool ota_abort = false;

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

const char *newFirmwareAvailable() { // return the new firmware version string if available

#define MAX_URL_LENGTH 256 // กำหนดขนาดบัฟเฟอร์สูงสุดที่ปลอดภัย
#define JSON_BUF_SIZE 1024

  // fetch the latest version from manifest.json
  static char reqURL[MAX_URL_LENGTH];
  snprintf(reqURL, sizeof(reqURL), "https://vaandcob.github.io/webpage/firmware/tunebar/tunebar_manifest.json");
  // snprintf(reqURL, sizeof(reqURL), "https://cdn.jsdelivr.net/gh/VaAndCob/webpage@main/firmware/tunebar/tunebar_manifest.json");

  static char jsonBuf[JSON_BUF_SIZE];
  static JsonDocument doc;
  doc.clear();

  // fetch weather condition data from weather API
  fetchUrlData(reqURL, true, jsonBuf, JSON_BUF_SIZE);
  sanitizeJson(jsonBuf);
  DeserializationError error = deserializeJson(doc, jsonBuf);

  if (error) {
    log_e("manifest JSON parse failed: %s", error.c_str());
    return nullptr;
  }

  // Check if the "version" key exists
  if (!doc["version"].isNull()) {
    const char *latestVersion = doc["version"]; // Get as const char*
    log_i("Firmware Version [Current: %s | Latest: %s]", current_version, latestVersion);
    firmware_checked = true; // already checked

    // version comparision
    if (versionToNumber(latestVersion) > versionToNumber(current_version)) {
      log_w("-> New firmware version available");
      char buf[32] = {0};
      snprintf(buf, sizeof(buf), "Update\nVersion\n%s", latestVersion);
      lv_label_set_text(ui_Utility_Label_Label11, buf);
      return latestVersion;
    } else {
      log_i("-> Firmware is up to date");
      return nullptr;
    }
  } else {
    log_e("Manifest JSON does not contain 'version' key.");
    return nullptr;
  }
  return nullptr; // Default return if something went wrong or no new version
}

/* ============= OTA UPDATER FUNCTIONS ========= */
// status message
static void ota_set_message_async(const char *txt) {
  log_i("%s", txt);
  lv_async_call(
      [](void *p) {
        if (ota_msg) lv_label_set_text(ota_msg, (const char *)p);
      },
      (void *)txt);
}
typedef struct {
  uint8_t pct;
  char txt[32];
} ota_ui_msg_t;

static void ota_set_progress_async(uint8_t pct, const char *txt) {
  ota_ui_msg_t *m = (ota_ui_msg_t *)lv_mem_alloc(sizeof(ota_ui_msg_t));
  m->pct = pct;
  strncpy(m->txt, txt, sizeof(m->txt) - 1);
  m->txt[sizeof(m->txt) - 1] = '\0';
  lv_async_call(
      [](void *p) {
        ota_ui_msg_t *m = (ota_ui_msg_t *)p;
        if (ota_bar) lv_bar_set_value(ota_bar, m->pct, LV_ANIM_OFF);
        if (ota_msg) lv_label_set_text(ota_msg, m->txt);
        lv_mem_free(m); // IMPORTANT: free heap copy
      },
      m);
}

/* ========== OTA TASK ========== */
void ota_task(void *param) {

  ota_abort = false;

  const char *firmwareURL = "https://vaandcob.github.io/webpage/firmware/tunebar/tunebar.bin";

  static HTTPClient client;
  static WiFiClient *stream = nullptr;
  static uint32_t fileSize = 0;

  client.begin(firmwareURL);

  int httpCode = client.GET();
  if (httpCode != HTTP_CODE_OK) {
    ota_set_message_async(LV_SYMBOL_WARNING " HTTP error");
    goto cleanup;
  }

  fileSize = client.getSize();
  stream = client.getStreamPtr();

  if (!Update.begin(fileSize)) {
    ota_set_message_async(LV_SYMBOL_WARNING " Not enough space");
    goto cleanup;
  }

  Update.onProgress([](size_t progress, size_t total) {
    static uint8_t old_pct = 0;
    uint8_t pct = (progress * 100) / total;
    if (pct == old_pct) return; // no change
    log_i("OTA Progress: %u%%", (unsigned)pct);
    old_pct = pct;
    char buf[32];
    snprintf(buf, sizeof(buf), "Updating %d %%", old_pct);
    ota_set_progress_async(pct, buf);
  });

  uint8_t buff[1024];

  while (client.connected() && fileSize > 0) {

    if (ota_abort) {
      ota_set_message_async(LV_SYMBOL_CLOSE " Aborted");
      goto cleanup;
    }

    size_t size = stream->available();
    if (size) {
      int c = stream->readBytes(buff, (size > sizeof(buff)) ? sizeof(buff) : size);
      Update.write(buff, c);
      fileSize -= c;
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (fileSize == 0 && Update.end(true)) {
    ota_set_message_async(LV_SYMBOL_OK " Update OK — rebooting…");
    esp_ota_mark_app_valid_cancel_rollback();
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
  } else {
    ota_set_message_async(LV_SYMBOL_WARNING " Update failed");
  }

cleanup:
  Update.onProgress(nullptr); // detach callback
  client.end();
  if (Update.isRunning()) {
    Update.abort();
    Update.end(false);
  }
  otaTaskHandle = NULL;
  vTaskDelete(NULL); // <- CRITICAL (do not return)
}

/* ========== POPUP UI ========== */
void ota_show_popup() {
  ota_abort = true;

  // make modal box to block input
  modal_blocker = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(modal_blocker);
  lv_obj_set_size(modal_blocker, LV_HOR_RES, LV_VER_RES);
  lv_obj_center(modal_blocker);
  /* visually dim screen (optional) */
  lv_obj_set_style_bg_opa(modal_blocker, LV_OPA_80, 0);
  lv_obj_set_style_bg_color(modal_blocker, lv_color_black(), 0);
  /* IMPORTANT — capture all input */
  lv_obj_add_flag(modal_blocker, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(modal_blocker, LV_OBJ_FLAG_GESTURE_BUBBLE);

  // make popup
  ota_popup = lv_obj_create(lv_layer_top());
  lv_obj_set_size(ota_popup, 400, LV_VER_RES);
  lv_obj_center(ota_popup);
  lv_obj_set_style_radius(ota_popup, 18, 0);
  lv_obj_set_style_pad_all(ota_popup, 8, 0);
  lv_obj_t *title = lv_label_create(ota_popup);
  lv_label_set_text(title, "Firmware Update");
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
  lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

  //----------- message --------------
  ota_msg = lv_label_create(ota_popup);
  lv_label_set_text(ota_msg, "Click Update to start");
  lv_obj_align(ota_msg, LV_ALIGN_TOP_MID, 0, 34);
  lv_obj_set_style_text_font(ota_msg, &lv_font_montserrat_18, 0);
  // ---------- CLOSE (X) BUTTON ----------
  btn_close = lv_btn_create(ota_popup);
  lv_obj_set_size(btn_close, 48, 48);
  lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_t *lbl_x = lv_label_create(btn_close);
  lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
  lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_28, 0);
  lv_obj_center(lbl_x);
  lv_obj_add_event_cb( // close handler
      btn_close,
      [](lv_event_t *e) {
        if (ota_popup) {
          lv_obj_del(ota_popup);
          ota_popup = NULL;
          lv_obj_del(modal_blocker);
          modal_blocker = NULL;
        }
      }, LV_EVENT_CLICKED, NULL);

  // Progress bar
  ota_bar = lv_bar_create(ota_popup);
  lv_obj_set_size(ota_bar, 280, 12);
  lv_obj_align(ota_bar, LV_ALIGN_CENTER, 0, 0);
  lv_bar_set_range(ota_bar, 0, 100);
  // Update button
  lv_obj_t *btn_update = lv_btn_create(ota_popup);
  lv_obj_set_size(btn_update, 180, 44);
  lv_obj_align(btn_update, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_t *lbl_update = lv_label_create(btn_update);
  lv_label_set_text(lbl_update, LV_SYMBOL_DOWNLOAD " Update");
  lv_obj_set_style_text_font(lbl_update, &lv_font_montserrat_18, 0);
  lv_obj_center(lbl_update);

  lv_obj_add_event_cb(
    btn_update,
    [](lv_event_t *e) {
        lv_obj_t *lbl_update = (lv_obj_t *)lv_event_get_user_data(e);
        if (!lbl_update) return;
        if (otaTaskHandle == NULL) {
            lv_label_set_text(ota_msg, "Starting update...");
            lv_label_set_text(lbl_update, LV_SYMBOL_CLOSE " Abort & Reboot");
            xTaskCreatePinnedToCore( ota_task,"ota_task",10 * 1024, NULL, 4, &otaTaskHandle, 1 );
        } else {
            lv_label_set_text(ota_msg, "Update aborted. Rebooting...");
            lv_timer_create([](lv_timer_t *) { esp_restart(); }, 500, NULL );
        }
    }, LV_EVENT_CLICKED,lbl_update
  );

  // swap
  lv_obj_set_parent(ui_Utility_Panel_blindPanel, lv_layer_top());
  lv_obj_move_foreground(ui_Utility_Panel_blindPanel);
}
//-------------  Functions to show

// memory and system info
void memoryInfo(char *buf, size_t len) {
  size_t ifree = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  size_t imin = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  size_t ilarge = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
  size_t pfree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  size_t pmin = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  size_t plarge = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);

  snprintf(buf, len,
           "Memory : Free / Min / LFB (bytes)\n"
           "IRAM : %u / %u / %u\n"
           "PSRAM: %u / %u / %u",
           (unsigned)ifree, (unsigned)imin, (unsigned)ilarge, (unsigned)pfree, (unsigned)pmin, (unsigned)plarge);
  log_i("%s", buf);
}

void systemInfo(char *buf, size_t len) {
  size_t used = LittleFS.usedBytes();
  size_t total = LittleFS.totalBytes();
  float pct_free = (float)(total - used) * 100.0f / (float)total;
  uint64_t mac = ESP.getEfuseMac();
  snprintf(buf, len,
           "[ TuneBar by Va&Cob ]\n"
           "BUILD  : %s - %s\n"
           "SERIAL : %012" PRIx64 "\n"
           "STORAGE : %u / %u KB (%.1f %% free)",
           current_version, compile_date, mac, (unsigned)(used / 1024), (unsigned)(total / 1024), pct_free);
  log_d("%s", buf);
}