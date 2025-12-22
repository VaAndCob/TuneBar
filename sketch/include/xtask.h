#include "file/file.h"

/* For RTOS Task function.
All task setting  Stack size and Priority and Core

CORE 1

  m_audioTaskHandle = xTaskCreateStaticPinnedToCore(
        &Audio::taskWrapper, "PeriodicTask", 3300, this, 6, xAudioStack,  &xAudioTaskBuffer, 1);
  xTaskCreatePinnedToCore(audio_loop_task, "audio_loop", 4 * 1024, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(rtc_read_task, "getDateTimeTask", 3 * 1024, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(button_input_task, "buttonInputTask", 2 * 1024, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(batt_level_read_task, "readBatteryLevel", 3 * 1024, NULL, 1, NULL, 1);

 xTaskCreatePinnedToCore(scan_music_task, "SD_Scan_Task", 6 * 1024, NULL, 1, NULL, 1);(create and delete)
 xTaskCreatePinnedToCore(updateWeatherPanelTask,"To update weather panel", 2 * 1024, NULL,3, NULL, 1);(create and delete)

CORE 0
 xTaskCreatePinnedToCore(WAVESHARE_349_lvgl_port_task, "LVGL", 16384, NULL, 5, NULL, 0); // Run Core 0

xTaskCreatePinnedToCore(wifi_connect_task, "wifi_connect_task", 6 * 1024, NULL, 1, &wifiTask, 0);(create and delete)

*/

#include "task_msg/task_msg.h"


//==============================================
// BUTTON INPUT TASK:
void button_input_task(void *param) {
  //   UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
  // log_d("{ Task stack remaining MIN: %u bytes }", hwm);
  vTaskDelay(pdMS_TO_TICKS(3000));//delay 1 sec avoid hold button too long and turn to off again
  bool powerBTN_pressed = false;
  long hold_timer = 0;
  AudioCommandPayload msg = {};
  for (;;) {
    // turn off power button
    if (digitalRead(SYS_OUT) == LOW) {
      if (!powerBTN_pressed) {
        powerBTN_pressed = true;
        hold_timer = millis();
      } else if (millis() - hold_timer > 500) {
        hold_timer = millis();
        audioSetVolume(20);
        audioPlayFS(1, "/audio/off.mp3"); 
        log_d("< POWER OFF >");
        vTaskDelay(pdMS_TO_TICKS(1000));
        io->digitalWrite(EXIO6_BIT, 0); // turn off
      }
    } else if (powerBTN_pressed)
      powerBTN_pressed = false;

    //-----------------------
    // screen on button
    if (digitalRead(BOOT) == LOW) { // Right most button
      vTaskDelay(pdMS_TO_TICKS(200));
      if (BL_OFF) {
        log_d("< Unlock Screen with button >");
        switch (backlight_state) {
        case 0: setUpduty(LCD_PWM_MODE_100); break;
        case 1: setUpduty(LCD_PWM_MODE_150); break;
        case 2: setUpduty(LCD_PWM_MODE_255); break;
        }
        UIStatusPayload msg = {
            .type = STATUS_SCREEN_UNLOCK,
        };
        xQueueSend(ui_status_queue, &msg, 100); // send message
        SCREEN_OFF_TIMER = millis(); // reset timer
        BL_OFF = false;
      } else { // force screen off
        log_d("< Lock Screen with button >");
        UIStatusPayload msg = {
            .type = STATUS_SCREEN_LOCK,
        };
        xQueueSend(ui_status_queue, &msg, 100); // send message
        setUpduty(LCD_PWM_MODE_0);
        BL_OFF = true;
      }
    }
    //-----------------------
    uint32_t now = millis();
    uint32_t start = SCREEN_OFF_TIMER;
    uint32_t delay = SCREEN_OFF_DELAY;

    if (delay != 0 && !BL_OFF) {
      if ((now - start) >= delay) {
        log_d("< Lock Screen >");

        UIStatusPayload msg = {
            .type = STATUS_SCREEN_LOCK,
        };
        xQueueSend(ui_status_queue, &msg, 100);

        setUpduty(LCD_PWM_MODE_0);
        BL_OFF = true;
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10)); // yield CPU
  }
}

//==============================================
// TASK: Battery Monitoring - run on Core 1
void batt_level_read_task(void *param) {
  static uint8_t last_state = 0;
  uint8_t bat_state = 0;
  vTaskDelay(pdMS_TO_TICKS(5000)); // delay to wait lvgl ready
  for (;;) {

    int rawValue = analogReadMilliVolts(ADC_BATT);
    float vbat = rawValue * 3.0f / 1000.0f;

    if (vbat >= 4.10)
      bat_state = 4;
    else if (vbat >= 3.95)
      bat_state = 3;
    else if (vbat >= 3.80)
      bat_state = 2;
    else if (vbat >= 3.60)
      bat_state = 1;
    else
      bat_state = 0;

    log_d("ADC: %d | Vbat: %.2fV | State: %d,%d", rawValue, vbat, bat_state, last_state);
    if (bat_state != last_state) {
      last_state = bat_state;
      UIStatusPayload msg = {// prepare mesasge
                             .type = STATUS_UPDATE_BATTERY_LEVEL,
                             .battery_state = bat_state};
      xQueueSend(ui_status_queue, &msg, 100); // send message
    }
    vTaskDelay(pdMS_TO_TICKS(10000));
  } // if{;;}

  // ... (HWM Log เดิม)
  UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
  log_d("{ Task stack remaining MIN: %u bytes }", hwm);
}

//==============================================

// TASK: Real Time Clock
extern PCF85063 rtc;
TickType_t lastWake = xTaskGetTickCount();
void rtc_read_task(void *param) {
  rtc.begin();
  for (;;) {
    // read RTC
    if (rtc.getDateTime()) {

      UIStatusPayload msg = {// prepare mesasge
                             .type = STATUS_UPDATE_CLOCK, .hour = now.hour, .minute = now.minute, .second = now.second, .year = now.year, .month = now.month, .dayOfMonth = now.dayOfMonth, .dayOfWeek = now.dayOfWeek};
      xQueueSend(ui_status_queue, &msg, 100); // send message

    } else {
      log_w("RTC Error");
    }
    // UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    // log_d("{ Task stack remaining MIN: %u bytes }", hwm);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(1000)); // 1 second update
  }
}
//----------------------------------------------------

// Playing status task
// convert track time
void timeStr(char *buffer, size_t size, uint32_t second) {
  uint8_t h = second / 3600;
  uint8_t m = (second % 3600) / 60;
  uint8_t s = second % 60;
  if (h > 0) {
    snprintf(buffer, size, "%d:%02d:%02d", h, m, s);
  } else {
    snprintf(buffer, size, "%d:%02d", m, s);
  }
}

//==============================================

// audio information callback
void my_audio_info(Audio::msg_t m) {

  UIStatusPayload msg = {};
  switch (mediaType) {
  case 0: // show station title / description
  {
    if (m.e == Audio::evt_streamtitle) {
      String txt = stations[stationIndex].name + "\n" + String(m.msg);
      msg = {
          .type = STATUS_UPDATE_TRACK_DESC_SET,
      };
      strncpy(msg.trackDesc, txt.c_str(), sizeof(msg.trackDesc) - 1);
      msg.trackDesc[sizeof(msg.trackDesc) - 1] = '\0';
      xQueueSend(ui_status_queue, &msg, 100); // send message
      log_i("%s", msg.trackDesc);
    }
    break;
  }
  case 1: // show song title/artist/album
  {
    if (m.e == Audio::evt_id3data) {
      if (strstr(m.msg, "Title") || strstr(m.msg, "Artist") || strstr(m.msg, "Album")) {
        String txt = String(m.msg) + "\n";
        msg = {
            .type = STATUS_UPDATE_TRACK_DESC_ADD,
        };
        strncpy(msg.trackDesc, txt.c_str(), sizeof(msg.trackDesc) - 1);
        msg.trackDesc[sizeof(msg.trackDesc) - 1] = '\0';
        xQueueSend(ui_status_queue, &msg, 100); // send message
        log_i("%s", msg.trackDesc);
      }
    }
  } break;
  } // switch
}

// audio task that fill the buffer

void audio_loop_task(void *param) {
  const TickType_t period = pdMS_TO_TICKS(5);
  TickType_t lastWakeTime = xTaskGetTickCount();
  static uint32_t last_pos = 0;
  static uint32_t last_total = 0;
  uint32_t current_pos = 0;
  uint32_t current_total = 0;
  char elapse_buf[16];
  char remain_buf[16];
  char status_buffer[50];
  UIStatusPayload msg = {};

  for (;;) {
    audio.loop();
    process_audio_cmd_que();//handle audio command from other tasks
    vTaskDelayUntil(&lastWakeTime, period);//yield for 5 ms

    if (audio.isRunning()) {
      current_pos = audio.getAudioCurrentTime();
      current_total = audio.getAudioFileDuration(); // get max length.

      // Update progress bar /elapse/remain
      if (current_pos != last_pos && current_total > 0) { // new position found
        log_d("%u/%u", current_pos, current_total);
        timeStr(elapse_buf, sizeof(elapse_buf), current_pos);
        timeStr(remain_buf, sizeof(remain_buf), current_total - current_pos);
        if (!seeking_now) {
        msg = {// prepare mesasge
               .type = STATUS_UPDATE_PLAY_POSITION,
               .current_pos = current_pos,
               .total = current_total};
        strncpy(msg.elapse_buf, elapse_buf, sizeof(msg.elapse_buf) - 1);
        msg.elapse_buf[sizeof(msg.elapse_buf) - 1] = '\0';
        strncpy(msg.remain_buf, remain_buf, sizeof(msg.remain_buf) - 1);
        msg.remain_buf[sizeof(msg.remain_buf) - 1] = '\0';
        xQueueSend(ui_status_queue, &msg, 100); // send message
        }
        // track not 0 length
        if (current_total > 0) {
          // set progress bar status when opena new track (not equal track length)
          if (current_total != last_total) {
            msg = {// prepare mesasge
                   .type = STATUS_UPDATE_PROGRESS_BAR,
                   .total = current_total};
            xQueueSend(ui_status_queue, &msg, 100); // send message
          }

          //---------
          // detect end of file track -> next track
          if ((mediaType == 1) && (current_total - current_pos <= 1)) {
            switch (playMode) {
            case 0: { // normal play mode
              trackIndex++;
              if (trackIndex == trackListLength) trackIndex = 0;
              break;
            }
            case 1: { // random play, avoid same track twice
              uint16_t old = trackIndex;
              do {
                trackIndex = random(trackListLength);
              } while (trackListLength > 1 && trackIndex == old);
              break;
            }
            case 2: { // repeat
              // do nothing
              break;
            }
            }
            // switch
            //  update track index
            snprintf(status_buffer, sizeof(status_buffer), "%d of %d", trackIndex + 1, trackListLength);
            msg = {
                .type = STATUS_UPDATE_TRACK_NUMBER,
            };
            strncpy(msg.trackNumber, status_buffer, sizeof(msg.trackNumber) - 1);
            msg.trackNumber[sizeof(msg.trackNumber) - 1] = '\0';
            xQueueSend(ui_status_queue, &msg, 100); // send message

            // play track
            String trackpath = getTrackPath(trackIndex);
            msg = {
                .type = STATUS_UPDATE_TRACK_DESC_SET,
            };
            if (!audio.connecttoFS(SD, trackpath.c_str())) {
              log_e("Failed to open file: %s", trackpath);
              strncpy(msg.trackDesc, "Cannot access music.\nPlease check the SD Card.\nOr Update music library.", sizeof(msg.trackDesc) - 1);
            } else {
              strncpy(msg.trackDesc, "", sizeof(msg.trackDesc) - 1);
            }
            msg.trackDesc[sizeof(msg.trackDesc) - 1] = '\0';
            xQueueSend(ui_status_queue, &msg, 100); // send message
          } // detect end of track -> next track
        } // not empty track

        last_pos = current_pos;
        last_total = current_total;
      } // current_pos != last_pos
    } // audio is running
    // UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    // log_d("{ Task stack remaining MIN: %u bytes }", hwm);
  } // for {;;}
}
//--------------------------------------------------------------

void memory_info() {
  log_d("\n--- Memory Status ---");
  size_t freeHeap = ESP.getFreeHeap();
  size_t totalHeap = ESP.getHeapSize();
  log_d("Heap: %u / %u bytes free (%.2f%%)\n", freeHeap, totalHeap, (freeHeap * 100.0 / totalHeap));
  log_d("Min Free Heap: %u bytes\n", ESP.getMinFreeHeap());
  log_d("Max Alloc Block: %u bytes\n", ESP.getMaxAllocHeap());
  if (psramFound()) {
    size_t freePSRAM = ESP.getFreePsram();
    size_t totalPSRAM = ESP.getPsramSize();
    log_d("PSRAM: %u / %u bytes free\n", freePSRAM, totalPSRAM);
  } else {
    log_d("PSRAM: Not found");
  }
  UBaseType_t stackLeft = uxTaskGetStackHighWaterMark(NULL);
  log_d("Current Task Stack Left: %u words (%u bytes)\n", stackLeft, stackLeft * 4);
}


/*
//------------- Micro phone task -------------------------
#include "driver/i2s_std.h"
// จอง PSRAM สำหรับเก็บเสียง 15 วินาที (ประมาณ 480KB)
const size_t MAX_REC_SIZE = 16000 * 2 * 5; 
int16_t *speech_buffer = (int16_t *)heap_caps_malloc(MAX_REC_SIZE, MALLOC_CAP_SPIRAM);
size_t speech_ptr = 0;


i2s_chan_handle_t rx_handle = {};

void mic_capture_task(void *pvParameters) {
    const size_t chunk_size = 512;
    int16_t temp[chunk_size];
    size_t bytes_read = 0;
    is_mic_mode = true;

//    mic.init_i2s1_mic_16k();
    for (;;) {
        if (is_mic_mode && rx_handle != NULL) {
            // ใช้ i2s_channel_read แทน i2s_read (นี่คือคำสั่งของ Driver ใหม่)
            esp_err_t err = i2s_channel_read(rx_handle, temp, sizeof(temp), &bytes_read, pdMS_TO_TICKS(10));
            
            if (err == ESP_OK && bytes_read > 0) {
                if (speech_ptr + (bytes_read/2) < (MAX_REC_SIZE / 2)) {
                    memcpy(&speech_buffer[speech_ptr], temp, bytes_read);
                    speech_ptr += (bytes_read / 2);
                } else {
                    is_mic_mode = false;
                    log_d("Capture size: %u bytes",bytes_read);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
*/