/* For RTOS Task function.
All task setting  Stack size and Priority and Core

CORE 1:
  m_audioTaskHandle = xTaskCreateStaticPinnedToCore(
        &Audio::taskWrapper, "PeriodicTask", 3300, this, 6, xAudioStack,  &xAudioTaskBuffer, 1);
  xTaskCreatePinnedToCore(audio_loop_task, "audio_loop", 6 * 1024, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(rtc_read_task, "getDateTimeTask", 3 * 1024, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(button_input_task, "buttonInputTask", 2 * 1024, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(batt_level_read_task, "readBatteryLevel", 2 * 1024, NULL, 1, NULL, 1);
  

  xTaskCreatePinnedToCore(scan_music_task, "SD_Scan_Task", 6 * 1024, NULL, 1, NULL, 1);(create and delete)
  xTaskCreatePinnedToCore(updateWeatherPanelTask,"To update weather panel", 2 * 1024, NULL,3, NULL, 1);(create and delete)
  xTaskCreatePinnedToCore(ota_task, "ota_task", 10 * 1024, NULL, 4, &otaTaskHandle, 1);(create and delete)

CORE 0:
  xTaskCreatePinnedToCore(WAVESHARE_349_lvgl_port_task, "LVGL", 6 * 1024, NULL, 5, NULL, 0); // Run Core 0
  xTaskCreatePinnedToCore(wifi_connect_task, "wifi_connect_task", 6 * 1024, NULL, 1, &wifiTask, 0);(create and delete)
*/
#include "file/file.h"
#include "task_msg/task_msg.h"
#include <LittleFS.h>

//==============================================
// BUTTON INPUT TASK:
void button_input_task(void *param) {
  //   UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
  // log_d("{ Task stack remaining MIN: %u bytes }", hwm);
  vTaskDelay(pdMS_TO_TICKS(3000)); // delay 1 sec avoid hold button too long and turn to off again
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
      vTaskDelay(pdMS_TO_TICKS(500));
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
  } // for {;;}
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

// AUDIO TASK  ========================================================================
// ---------- GLOBAL RECORDING STATE ----------
static const size_t SAMPLE_RATE = 48000; // or 16000 – choose ONE
static const size_t RECORD_SECONDS = 5;
static const size_t CHANNELS = 2; // 1: mono capture 2;//stereo
static const size_t BYTES_PER_SAMPLE = 2; // 16-bit PCM

static const size_t MAX_REC_SIZE = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE * RECORD_SECONDS;

static int16_t *speech_buffer = nullptr; // persistent buffer
static size_t speech_ptr = 0; // write cursor (bytes)
static bool speech_ready = false; // flag says buffer is valid
//------------------------------------
void init_audio_buffers() {
  if (!speech_buffer) {
    speech_buffer = (int16_t *)heap_caps_malloc(MAX_REC_SIZE, MALLOC_CAP_SPIRAM);
  }
  speech_ptr = 0;
  speech_ready = false;
}
//------------------------------------
void playRecordedAudio() {

  // ---------- SAFETY CHECKS ----------
  if (!speech_buffer) {
    log_e("speech_buffer NULL");
    return;
  }

  if (speech_ptr == 0) {
    log_w("No recorded audio available");
    return;
  }

  auto tx = audio.getTxHandle();
  if (!tx) {
    log_e("TX handle NULL");
    return;
  }

  // ---------- PLAYBACK ----------
  size_t length = speech_ptr; // bytes of valid mono audio
  size_t play_ptr = 0;
  size_t bytes_written = 0;

  const int NUM_SAMPLES = 256; // mono samples / block
  static int16_t stereo_buf[NUM_SAMPLES * 2]; // stereo L/R interleaved

  log_i("Playback start (%u bytes, stereo 16-bit)", (unsigned)length);

  while (play_ptr < length) {

    size_t samples_left = (length - play_ptr) / 2; // bytes→samples
    size_t samples_to_send = samples_left;

    if (samples_to_send > NUM_SAMPLES) samples_to_send = NUM_SAMPLES;

    int16_t *mono_ptr = (int16_t *)((uint8_t *)speech_buffer + play_ptr);

    // mono → stereo copy
    for (size_t i = 0; i < samples_to_send; i++) {
      int16_t s = mono_ptr[i];
      stereo_buf[2 * i + 0] = s; // L
      stereo_buf[2 * i + 1] = s; // R
    }

    esp_err_t err = i2s_channel_write(tx, stereo_buf,
                                      samples_to_send * 4, // 4 bytes per stereo frame
                                      &bytes_written, portMAX_DELAY);

    if (err != ESP_OK) {
      log_e("I2S write failed: %s", esp_err_to_name(err));
      break;
    }

    play_ptr += samples_to_send * 2; // mono bytes advanced
  }

  log_i("Playback finished (%u bytes played)", (unsigned)length);

  //---------------------------------

  log_i("=== PLAYBACK TEST: synthetic noise ===");
  for (int block = 0; block < 400; block++) { // ~2 seconds at 48k
    // generate noise
    for (int i = 0; i < NUM_SAMPLES; i++) {
      int16_t v = (rand() % 4000) - 1000;
      stereo_buf[i * 2 + 0] = v; // L
      stereo_buf[i * 2 + 1] = v; // R
    }
    size_t written = 0;
    esp_err_t err = i2s_channel_write(tx, stereo_buf,
                                      NUM_SAMPLES * 4, // stereo, 2 bytes each → 4 bytes/frame
                                      &written, portMAX_DELAY);

    if (err != ESP_OK) {
      log_e("I2S write failed: %s", esp_err_to_name(err));
      break;
    }
  }
  log_i("=== TEST PLAYBACK DONE ===");

  // optional reset for next recording session
  speech_ready = false;
  speech_ptr = 0;
}

/*
void startRecording() {
    if (audio.isRunning()) {
        audio.stopSong(); // หยุดเล่นเพลงก่อน
    }
    // ตั้งค่า I2S ให้ทำงานที่ 16kHz สำหรับ Mic Input
    // การเรียก setPinout อีกครั้งจะช่วย Reconfig I2S Clock ให้เป็น 16kHz

    // ปลุกชิป ES7210
   if (!mic.start()) {
      log_e("ES7210 not detected");
      return;
    }

    length = 0;
    is_mic_mode = true;
    log_i("Recording Started at 16kHz");
}
*/

// stereo 48k -> mono 16k (simple decimation, no filter)
void downsample48kTo16k(int16_t *in48k, size_t frames48k, int16_t *out16k, size_t &frames16k) {
  frames16k = 0;
  for (size_t i = 0; i < frames48k; i += 3) {
    int16_t left = in48k[i * 2 + 0]; // take LEFT
    out16k[frames16k++] = left;
  }
}
//------------------------------------
void stopRecording(size_t length) {

  mic.stop();
  is_mic_mode = false;
  log_i("Recording Stopped length = %d bytes", length);

  playRecordedAudio();
}

// audio information callback
void my_audio_info(Audio::msg_t m) {

  UIStatusPayload msg = {};
  switch (mediaType) {
  case 0: // show station title / description
  {
    if (m.e == Audio::evt_streamtitle) {
      msg.type = STATUS_UPDATE_TRACK_DESC_SET;
      snprintf(msg.trackDesc, sizeof(msg.trackDesc), "%s\n%s", stations[stationIndex].name, m.msg);
      xQueueSend(ui_status_queue, &msg, 100); // send message
      log_i("%s", m.msg);
    }
    break;
  }
  case 1: // show song title/artist/album
  {
    if (m.e == Audio::evt_id3data) {
      if (strstr(m.msg, "Title") || strstr(m.msg, "Artist") || strstr(m.msg, "Album")) {
        msg.type = STATUS_UPDATE_TRACK_DESC_ADD;
        snprintf(msg.trackDesc, sizeof(msg.trackDesc), "%s\n", m.msg);
        xQueueSend(ui_status_queue, &msg, 100); // send message
        log_i("%s", msg.trackDesc);
      }
    }
  } break;
  } // switch
}

//------------------------------------
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

  // --- ส่วนของ Mic (แก้ไขใหม่) ---
  // อ่านทีละ 512 bytes (จะได้ 128 stereo frames)
  const size_t stereo_chunk_bytes = 512;
  int16_t stereo_temp[stereo_chunk_bytes / 2]; // Buffer ชั่วคราวรับ Stereo
  size_t bytes_read = 0;

  UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
  log_w("{ Task stack remaining MIN: %u bytes }", hwm);

  for (;;) {

    // PLAYBACK MODE
    if (!is_mic_mode) {
      audio.loop();
      process_audio_cmd_que();
      vTaskDelayUntil(&lastWakeTime, period);

      // ... (โค้ด Update UI เดิมของคุณ เก็บไว้เหมือนเดิม) ...
      if (audio.isRunning()) {

        current_pos = audio.getAudioCurrentTime();
        current_total = audio.getAudioFileDuration();
        if (current_pos != last_pos && current_total > 0) {
          log_d("%u/%u", current_pos, current_total);
          timeStr(elapse_buf, sizeof(elapse_buf), current_pos);
          timeStr(remain_buf, sizeof(remain_buf), current_total - current_pos);
          if (!seeking_now) {
            msg = {// prepare mesasge
                   .type = STATUS_UPDATE_PLAY_POSITION,
                   .current_pos = current_pos,
                   .total = current_total};
            snprintf(msg.elapse_buf, sizeof(msg.elapse_buf), "%s", elapse_buf);
            snprintf(msg.remain_buf, sizeof(msg.remain_buf), "%s", remain_buf);
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
              msg.type = STATUS_UPDATE_TRACK_NUMBER;
               snprintf(msg.trackNumber, sizeof(msg.trackNumber), "%d of %d", trackIndex + 1, trackListLength);
              xQueueSend(ui_status_queue, &msg, 100); // send message

              // play track
              char trackPath[512];
              getTrackPath(trackIndex, trackPath, sizeof(trackPath));
              msg.type = STATUS_UPDATE_TRACK_DESC_SET;
              if (!audio.connecttoFS(SD, trackPath)) {
                log_e("Failed to open file: %s", trackPath);
                snprintf(msg.trackDesc, sizeof(msg.trackDesc),  "Cannot access music.\nPlease check the SD Card.\nOr Update music library.");
              } else {
                snprintf(msg.trackDesc, sizeof(msg.trackDesc), "");
              }
              xQueueSend(ui_status_queue, &msg, 100); // send message
            } // detect end of track -> next track
          } // not empty track

          last_pos = current_pos;
          last_total = current_total;
        }
      }
      vTaskDelay(1);
    }

    // ---------RECORD MODE-------------------------
    else {
      auto rx_handle = audio.getRxHandle();
      if (!rx_handle) {
        log_e("I2S RX Handle NULL");
        vTaskDelay(50);
        continue;
      }

      // allocate once
      if (!speech_buffer) {
        init_audio_buffers();

        if (!speech_buffer) {
          log_e("speech_buffer alloc failed");
          vTaskDelay(100);
          continue;
        }
      }

      const size_t stereo_chunk_bytes = 512;
      int16_t stereo_temp[stereo_chunk_bytes / 2];
      size_t bytes_read = 0;

      esp_err_t err = i2s_channel_read(rx_handle, stereo_temp, sizeof(stereo_temp), &bytes_read, pdMS_TO_TICKS(100));

      // stereo_temp contains interleaved L R L R ...
      /*
      for (int i = 0; i < 16; i++) {   // print the first 16 samples
          int16_t L = stereo_temp[i * 2 + 0];
          int16_t R = stereo_temp[i * 2 + 1];
          log_i("%d,%d\n", L, R);
      }
       */
      if (err == ESP_OK && bytes_read >= 4) {

        size_t frames = bytes_read / 4; // 4 bytes per stereo frame

        // prevent overflow
        if (speech_ptr + frames * 2 > MAX_REC_SIZE) {
          log_w("Recording buffer full");
          speech_ready = true;
          stopRecording(speech_ptr);
          continue;
        }

        // mono write pointer
        int16_t *dest = speech_buffer + (speech_ptr / 2);

        // copy Left only
        for (size_t i = 0; i < frames; i++) {
          dest[i] = stereo_temp[i * 2];
        }

        // advance in BYTES
        speech_ptr += frames * 2;

        if (speech_ptr >= MAX_REC_SIZE) {
          speech_ready = true;
          log_i("Buffer Full - stop");
          stopRecording(speech_ptr);
        }
      } else if (err != ESP_OK) {
        log_e("I2S Read Error: %s", esp_err_to_name(err));
      }

      vTaskDelay(1);
    }

    //-------------------------------
  } // for(;;)
} // audio loop task