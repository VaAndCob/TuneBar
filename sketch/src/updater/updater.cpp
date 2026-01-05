#include "lcd_bl_bsp/lcd_bl_pwm_bsp.h"
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
const char *current_version = "1.1.99";

bool firmware_checked = false; // check firmware flag

static TaskHandle_t otaTaskHandle = NULL;
static lv_obj_t *modal_blocker = NULL;
static lv_obj_t *ota_popup = NULL;
static lv_obj_t *ota_bar = NULL;
static lv_obj_t *ota_msg = NULL;
static lv_obj_t *btn_close = NULL;
static lv_obj_t *lbl_update = NULL;

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
      snprintf(buf, sizeof(buf), "Update %s", latestVersion);
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
  if (!txt) return;
  char *copy = strdup(txt); // 👈 heap copy
  if (!copy) return;
  log_i("%s", copy);
  lv_async_call(
      [](void *p) {
        if (ota_msg) lv_label_set_text(ota_msg, (const char *)p);
        free(p); // 👈 free after use
      },
      copy);
}
typedef struct {
  uint8_t pct;
  char txt[32];
} ota_ui_msg_t;

static void ota_set_progress_async(uint8_t pct, const char *txt) {
  log_i("%s", txt);
  ota_ui_msg_t *m = (ota_ui_msg_t *)lv_mem_alloc(sizeof(ota_ui_msg_t));
  m->pct = pct;
  snprintf(m->txt, sizeof(m->txt), "%s", txt);
  lv_async_call(
      [](void *p) {
        ota_ui_msg_t *m = (ota_ui_msg_t *)p;
        if (ota_bar) lv_bar_set_value(ota_bar, m->pct, LV_ANIM_OFF);
        if (ota_msg) lv_label_set_text(ota_msg, m->txt);
        lv_mem_free(m); // IMPORTANT: free heap copy
      },
      m);
}
static void ota_set_btn_label_async(const char *txt) {
  if (!txt) return;
  char *copy = strdup(txt); // 👈 heap copy
  if (!copy) return;
  log_i("%s", copy);
  lv_async_call(
      [](void *p) {
        lv_obj_clear_flag(btn_close, LV_OBJ_FLAG_HIDDEN);
        if (lbl_update) lv_label_set_text(lbl_update, (const char *)p);
        free(p); // 👈 free after use
      },
      copy);
}

/* ========== OTA TASK ========== */
void ota_task(void *param) {
  ota_set_message_async(LV_SYMBOL_REFRESH " Starting update...");
  vTaskDelay(pdMS_TO_TICKS(200));

  const char *firmwareURL = "https://vaandcob.github.io/webpage/firmware/tunebar/tunebar.bin";

  uint32_t temp_screen_off_delay = SCREEN_OFF_DELAY; // force turn screen on
  SCREEN_OFF_DELAY = 0;

  ota_abort = false;
  bool ok = true;

  WiFiClientSecure otaClient;
  HTTPClient otaHttp;
  otaClient.setInsecure();

  if (!otaHttp.begin(otaClient, firmwareURL)) {
    ota_set_message_async(LV_SYMBOL_WARNING " Update failed: HTTP begin failed");
    ok = false;
  }

  int httpCode = -1;
  int fileSize = -1;
  WiFiClient *stream = nullptr;

  if (ok) {
    httpCode = otaHttp.GET();
    if (httpCode != HTTP_CODE_OK) {
      char msg[96];
      snprintf(msg, sizeof(msg), LV_SYMBOL_WARNING " Update failed: %s", HTTPClient::errorToString(httpCode).c_str());
      ota_set_message_async(msg);
      ok = false;
    }
  }

  if (ok) {
    fileSize = otaHttp.getSize();
    if (fileSize > 0) {
      if (!Update.begin(fileSize)) ok = false;
    } else {
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) ok = false;
    }

    if (!ok) {
      ota_set_message_async(LV_SYMBOL_WARNING " Update failed: Not enough space");
    }
  }

  if (ok) {
    stream = otaHttp.getStreamPtr();
    if (!stream) {
      ota_set_message_async(LV_SYMBOL_WARNING " Update failed: No stream");
      ok = false;
    }
  }

  if (ok) {
    Update.onProgress([](size_t progress, size_t total) {
      static uint8_t old_pct = 0;
      uint8_t pct = (progress * 100) / total;
      if (pct == old_pct) return;
      old_pct = pct;

      char buf[32];
      snprintf(buf, sizeof(buf), LV_SYMBOL_DOWNLOAD " Updating %d %%", pct);
      ota_set_progress_async(pct, buf);
    });

    uint8_t otaBuf[512];

    // uint8_t *otaBuf = (uint8_t *)heap_caps_malloc(2048, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    uint32_t lastData = millis();
    size_t written = 0;

    while (!ota_abort && Update.isRunning()) {

      size_t avail = stream->available();

      if (avail) {
        size_t toRead = avail;
        if (toRead > sizeof(otaBuf)) toRead = sizeof(otaBuf);

        int c = stream->readBytes(otaBuf, toRead);

        if (c > 0) {
          Update.write(otaBuf, c);
          written += c;
          lastData = millis();
        }

        // ✅ EXIT WHEN FULL FILE RECEIVED
        if (fileSize > 0 && written >= (size_t)fileSize) {
          break;
        }
        // determine min stack available
        static size_t minEver = SIZE_MAX;
        size_t cur = uxTaskGetStackHighWaterMark(NULL) * sizeof(StackType_t);
        if (cur < minEver) {
          minEver = cur;
          log_i("OTA stack min ever: %u bytes", minEver);
        }

      } else if (!stream->connected()) {
        // ✅ SERVER CLOSED CONNECTION (normal end)
        break;

      } else if (millis() - lastData > 5000) {
        ota_set_message_async(LV_SYMBOL_WARNING " Update failed: Timeout");
        ok = false;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(4));
    }
  }

  if (ok && Update.end(true)) {
    ota_set_message_async(LV_SYMBOL_OK " Update success: Rebooting");
    esp_ota_mark_app_valid_cancel_rollback();
    vTaskDelay(pdMS_TO_TICKS(800));
    esp_restart();
  } else {
    ota_set_btn_label_async(LV_SYMBOL_DOWNLOAD " Update");
  }

  Update.onProgress(nullptr); // update callback

  if (Update.isRunning()) {
    Update.abort();
    Update.end(false);
  }
  otaHttp.end();
  otaClient.stop();

  vTaskDelay(pdMS_TO_TICKS(300));

  SCREEN_OFF_DELAY = temp_screen_off_delay; // set screen timer back
  BL_OFF = true;
  otaTaskHandle = NULL;
  vTaskDelete(NULL);
}

/* ========== POPUP UI ========== */
void ota_show_popup()
{
    lv_async_call(
        [](void *param) {

            ota_abort = true;

            /* ---------- MODAL BLOCKER ---------- */
            modal_blocker = lv_obj_create(lv_layer_top());
            lv_obj_remove_style_all(modal_blocker);
            lv_obj_set_size(modal_blocker, LV_HOR_RES, LV_VER_RES);
            lv_obj_center(modal_blocker);
            lv_obj_set_style_bg_opa(modal_blocker, LV_OPA_80, 0);
            lv_obj_set_style_bg_color(modal_blocker, lv_color_black(), 0);
            lv_obj_add_flag(modal_blocker, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_add_flag(modal_blocker, LV_OBJ_FLAG_GESTURE_BUBBLE);

            /* ---------- POPUP ---------- */
            ota_popup = lv_obj_create(lv_layer_top());
            lv_obj_set_size(ota_popup, 350, LV_VER_RES);
            lv_obj_center(ota_popup);
            lv_obj_set_style_radius(ota_popup, 15, 0);
            lv_obj_set_style_pad_all(ota_popup, 5, 0);

            /* ---------- TITLE ---------- */
            lv_obj_t *title = lv_label_create(ota_popup);
            lv_label_set_text(title, "Firmware Update");
            lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);
            lv_obj_set_style_text_font(title, &lv_font_montserrat_18, 0);

            /* ---------- MESSAGE ---------- */
            ota_msg = lv_label_create(ota_popup);
            lv_label_set_text(ota_msg, "Click Update to start");
            lv_obj_align(ota_msg, LV_ALIGN_TOP_MID, 0, 40);
            lv_obj_set_style_text_font(ota_msg, &lv_font_montserrat_18, 0);

            /* ---------- CLOSE BUTTON ---------- */
            btn_close = lv_btn_create(ota_popup);
            lv_obj_set_size(btn_close, 36, 36);
            lv_obj_align(btn_close, LV_ALIGN_TOP_RIGHT, 0, 0);

            lv_obj_t *lbl_x = lv_label_create(btn_close);
            lv_label_set_text(lbl_x, LV_SYMBOL_CLOSE);
            lv_obj_set_style_text_font(lbl_x, &lv_font_montserrat_28, 0);
            lv_obj_center(lbl_x);

            lv_obj_add_event_cb(
                btn_close,
                [](lv_event_t *e) {
                    if (ota_popup) {
                        lv_obj_del(ota_popup);
                        ota_popup = NULL;
                    }
                    if (modal_blocker) {
                        lv_obj_del(modal_blocker);
                        modal_blocker = NULL;
                    }
                },
                LV_EVENT_CLICKED,
                NULL
            );

            /* ---------- PROGRESS BAR ---------- */
            ota_bar = lv_bar_create(ota_popup);
            lv_obj_set_size(ota_bar, 280, 12);
            lv_obj_align(ota_bar, LV_ALIGN_CENTER, 0, 5);
            lv_bar_set_range(ota_bar, 0, 100);

            /* ---------- UPDATE BUTTON ---------- */
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
                    lv_obj_t *label = (lv_obj_t *)lv_event_get_user_data(e);
                    if (!label) return;

                    if (otaTaskHandle == NULL) {
                        lv_obj_add_flag(btn_close, LV_OBJ_FLAG_HIDDEN);
                        lv_label_set_text(label, LV_SYMBOL_CLOSE " Abort & Reboot");
                         // start OTA Task
                        xTaskCreatePinnedToCore(ota_task,"ota_task", 6 * 1024,NULL,4, &otaTaskHandle,1 );
                    } else {
                        lv_label_set_text(ota_msg, "Update aborted. Rebooting...");
                        lv_timer_create([](lv_timer_t *) { esp_restart(); },500, NULL );
                    }
                },
                LV_EVENT_CLICKED,
                lbl_update
            );

            /* ---------- BRING BLIND PANEL TO FRONT ---------- */
            lv_obj_set_parent(ui_Utility_Panel_blindPanel, lv_layer_top());
            lv_obj_move_foreground(ui_Utility_Panel_blindPanel);

        },
        NULL
    );
}


// Notify update message box ppopup
struct NotifyUpdateCtx {
    char *title;
    uint32_t prev_screen_delay;
};

// create notify a new firmware version msgbox
static void notifyUpdate_ui(void *p) {
    NotifyUpdateCtx *ctx = (NotifyUpdateCtx *)p;
    if (!ctx) return;

    SCREEN_OFF_DELAY = 0;

    lv_obj_t *msgBox = lv_msgbox_create(
        NULL,
        ctx->title,
        "To update firmware, please click\nUtilities -> System Information",
        NULL,
        true
    );

    lv_obj_set_width(msgBox, 420);
    lv_obj_center(msgBox);

    lv_obj_t *titleObj = lv_msgbox_get_title(msgBox);
    lv_obj_t *textObj  = lv_msgbox_get_text(msgBox);
    lv_obj_t *closeBtn = lv_msgbox_get_close_btn(msgBox);

    lv_obj_set_style_text_font(titleObj, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_font(textObj,  &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_font(closeBtn, &lv_font_montserrat_28, 0);
    lv_obj_set_size(closeBtn, 48, 48);

    lv_obj_clear_flag(ui_Utility_Button_UpdateFirmware, LV_OBJ_FLAG_HIDDEN);

    // Close button handler
    lv_obj_add_event_cb(
        closeBtn,
        [](lv_event_t *e) {
            NotifyUpdateCtx *ctx = (NotifyUpdateCtx *)lv_event_get_user_data(e);
            if (!ctx) return;

            SCREEN_OFF_DELAY = ctx->prev_screen_delay;
            SCREEN_OFF_TIMER = millis();
            BL_OFF = false;
        },
        LV_EVENT_RELEASED,
        ctx
    );

    // Cleanup on delete
    lv_obj_add_event_cb(
        msgBox,
        [](lv_event_t *e) {
            NotifyUpdateCtx *ctx = (NotifyUpdateCtx *)lv_event_get_user_data(e);
            if (ctx) {
                free(ctx->title);
                free(ctx);
            }
        },
        LV_EVENT_DELETE,
        ctx
    );
}

void notifyUpdate(const char *latestVer) {
    if (!latestVer) return;
    NotifyUpdateCtx *ctx = (NotifyUpdateCtx *)malloc(sizeof(NotifyUpdateCtx));
    if (!ctx) return;
    ctx->prev_screen_delay = SCREEN_OFF_DELAY;
    char title[48];
    snprintf(title, sizeof(title), LV_SYMBOL_REFRESH " New Update Available %s", latestVer);
    ctx->title = strdup(title);
    if (!ctx->title) {
        free(ctx);
        return;
    }
    lv_async_call(notifyUpdate_ui, ctx);
}


//-----  Functions to show memory and system info
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