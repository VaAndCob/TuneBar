// Handle message que between task

/*
      UIStatusPayload msg = { //prepare mesasge
                              .type = STATUS_UPDATE_BATTERY_LEVEL,
                              .battery_state = bat_state
      };
      xQueueSend(ui_status_queue, &msg, 0);  //send message

*/
#include "task_msg.h"
#include "esp32-hal.h"
#include "esp_err.h"

#include "pcf85063/pcf85063.h"
#include "ui/ui.h"
#include "weather/weather.h"
#include <LittleFS.h>

#include "ESP32-audioI2S-master/Audio.h"
Audio audio;
extern uint8_t audio_volume;

bool seeking_now = false;

//###########  MESSAGE QUEUE SENDER functions ###############################

// SD card   "file.cpp"
void updateSDCARDStatus(const char *status,  const uint32_t wifiColor) {
  UIStatusPayload msg = {
     .type = STATUS_SDCARD_STATUS,
     .wificolor = wifiColor,
     };
  strncpy(msg.status, status, sizeof(msg.status) - 1);         
  msg.status[sizeof(msg.status) - 1] = '\0';
  xQueueSend(ui_status_queue, &msg,  0);
}
// WiFi network  "network.cpp"
void updateWiFiStatus(const char *status, const uint32_t labelColor, const uint32_t wifiColor) {
  UIStatusPayload msg = {
      .type = STATUS_WIFI_CONNECTION,
      .labelcolor = labelColor,
      .wificolor = wifiColor,
  };
  strncpy(msg.status, status, sizeof(msg.status) - 1);
  msg.status[sizeof(msg.status) - 1] = '\0';
  xQueueSend(ui_status_queue, &msg, 0);
}

void updateWiFiOption(const char *status) {
  UIStatusPayload msg = {
      .type = STATUS_WIFI_OPTION,
  };
  strncpy(msg.status, status, sizeof(msg.status) - 1);
  msg.status[sizeof(msg.status) - 1] = '\0';
  xQueueSend(ui_status_queue, &msg, 0);
}

// Audio  Control
void audioSetAudioFilePosition(uint16_t pos) {
      AudioCommandPayload msg = {
        .cmd = CMD_AUDIO_SET_FILE_POSITION,
        .value = pos,
    };
    xQueueSend(audio_cmd_queue, &msg, 0); // send message
}
void audioSetPlayTime(uint16_t pos) {
      AudioCommandPayload msg = {
        .cmd = CMD_AUDIO_SET_PLAY_TIME,
        .value = pos,
    };
    xQueueSend(audio_cmd_queue, &msg, 0); // send message
}
void audioStopSong() {
  AudioCommandPayload msg = {
      .cmd = CMD_AUDIO_STOP_SONG,
      .url_filename = {0},
      .value = 0,
      .source = 0,
      .stationName = {0},
  };
  xQueueSend(audio_cmd_queue, &msg, 0);
}
void audioPauseResume() {
  AudioCommandPayload msg = {
      .cmd = CMD_AUDIO_PAUSE_RESUME,
      .url_filename = {0},
      .value = 0,
      .source = 0,
      .stationName = {0},
  };
  xQueueSend(audio_cmd_queue, &msg, 0);
}
void audioSetTimeOffset(int offset) {
  AudioCommandPayload msg = {
      .cmd = CMD_AUDIO_SET_TIME_OFFSET,
      .url_filename = {0},
      .value = offset,
      .source = 0,
      .stationName = {0},
  };
  xQueueSend(audio_cmd_queue, &msg, 500);//wait for 0.5 sec, avoid too fast skipping
}
void audioSetVolume(int volume) {
  AudioCommandPayload msg = {
      .cmd = CMD_AUDIO_SET_VOLUME,
      .url_filename = {0},
      .value = volume,
      .source = 0,
      .stationName = {0},
  };
  xQueueSend(audio_cmd_queue, &msg, 0);
}
void audioPlayFS(uint8_t source, const char *filename) {
  AudioCommandPayload msg = {
      .cmd = CMD_AUDIO_CONNECT_FS,
      .url_filename = {0},
      .value = 0,
      .source = source,
      .stationName = {0},
  };
  strncpy(msg.url_filename, filename, sizeof(msg.url_filename) - 1);
  msg.url_filename[sizeof(msg.url_filename) - 1] = '\0';
  xQueueSend(audio_cmd_queue, &msg, 0);
}
void audioPlayHOST(const char *filename, const char *stationName) {
  AudioCommandPayload msg = {
      .cmd = CMD_AUDIO_CONNECT_HOST,
      .url_filename = {0},
      .value = 0,
      .source = 0,
      .stationName = {0},
  };
  strncpy(msg.url_filename, filename, sizeof(msg.url_filename) - 1);
  msg.url_filename[sizeof(msg.url_filename) - 1] = '\0';
  strncpy(msg.stationName, stationName, sizeof(msg.stationName) - 1);
  msg.stationName[sizeof(msg.stationName) - 1] = '\0';
  xQueueSend(audio_cmd_queue, &msg, 0);
}


//----------  Clock display helper functions ------------------------
// image name of each nixie image.
const lv_img_dsc_t nixie[10] = {  // image 0 - 9
    ui_img_numbers_0_png, ui_img_numbers_1_png, ui_img_numbers_2_png, ui_img_numbers_3_png, ui_img_numbers_4_png, ui_img_numbers_5_png, ui_img_numbers_6_png, ui_img_numbers_7_png, ui_img_numbers_8_png, ui_img_numbers_9_png,
};
// update nixie clock panel
void nixie_clock(uint8_t hour, uint8_t minute, uint8_t second) {
  static uint8_t old_time[6] = {255, 255, 255, 255, 255, 255};
  uint8_t new_time[6] = {(uint8_t)(hour / 10), (uint8_t)(hour % 10), (uint8_t)(minute / 10), (uint8_t)(minute % 10), (uint8_t)(second / 10), (uint8_t)(second % 10)};

  if (new_time[0] != old_time[0]) {
    lv_img_set_src(ui_Info_Image_hour0, &nixie[new_time[0]]);
    old_time[0] = new_time[0];
  }
  if (new_time[1] != old_time[1]) {
    lv_img_set_src(ui_Info_Image_hour1, &nixie[new_time[1]]);
    old_time[1] = new_time[1];
  }
  if (new_time[2] != old_time[2]) {
    lv_img_set_src(ui_Info_Image_minute0, &nixie[new_time[2]]);
    old_time[2] = new_time[2];
  }
  if (new_time[3] != old_time[3]) {
    lv_img_set_src(ui_Info_Image_minute1, &nixie[new_time[3]]);
    old_time[3] = new_time[3];
  }
  if (new_time[4] != old_time[4]) {
    lv_img_set_src(ui_Info_Image_second0, &nixie[new_time[4]]);
    old_time[4] = new_time[4];
  }
  if (new_time[5] != old_time[5]) {
    lv_img_set_src(ui_Info_Image_second1, &nixie[new_time[5]]);
    old_time[5] = new_time[5];
  }
}



//###########  MESSAGE QUEUE RECEIVER functions ###############################


//run in lvgl_port.c  task to update lvgl widget
void process_ui_status_queue() {
  UIStatusPayload msg;

  while (xQueueReceive(ui_status_queue, &msg, 0) == pdTRUE) {

    switch (msg.type) {

    // Battery status
    case STATUS_UPDATE_BATTERY_LEVEL:
      switch (msg.battery_state) {
      case 4: lv_label_set_text(ui_Player_Label_Battery, LV_SYMBOL_BATTERY_FULL); break;
      case 3: lv_label_set_text(ui_Player_Label_Battery, LV_SYMBOL_BATTERY_3); break;
      case 2: lv_label_set_text(ui_Player_Label_Battery, LV_SYMBOL_BATTERY_2); break;
      case 1: lv_label_set_text(ui_Player_Label_Battery, LV_SYMBOL_BATTERY_1); break;
      default: lv_label_set_text(ui_Player_Label_Battery, LV_SYMBOL_BATTERY_EMPTY); break;
      }
      break;

    // Real time clock
    case STATUS_UPDATE_CLOCK:
      char timeBuf[16];
      char dateBuf[40];
      char datetimeBuf[60];
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d:%02d", msg.hour, msg.minute, msg.second);
      snprintf(dateBuf, sizeof(dateBuf), "%s, %s %d, %d", weekdayNames[msg.dayOfWeek], monthNames[msg.month - 1], msg.dayOfMonth, 2000 + msg.year);
      snprintf(datetimeBuf, sizeof(datetimeBuf), "%s - %s", dateBuf, timeBuf);
      log_d("%s", datetimeBuf);

      switch (infoPageIndex) { // update date time by info panel page
      case 0: // weather
        lv_label_set_text(ui_Info_Label_DateTime, datetimeBuf);
        break;
      case 1: // nixie clock
        lv_label_set_text(ui_Info_Label_DateNixie, dateBuf);
        nixie_clock(msg.hour, msg.minute, msg.second);
        break;
      default: break;

      } // switch
      // update weather condition widget every 15 min.
      if (msg.minute % 15 == 0 && msg.second == 0) updateWeatherPanel();
      break;

    // audio play position
    case STATUS_UPDATE_PLAY_POSITION:
      lv_slider_set_value(ui_Player_Slider_Progress, msg.current_pos, LV_ANIM_OFF);
      lv_label_set_text(ui_Player_Label_RemainTime, msg.remain_buf);
      lv_label_set_text(ui_Player_Label_ElapseTime, msg.elapse_buf);
      break;

    // audio progress bar update
    case STATUS_UPDATE_PROGRESS_BAR: lv_slider_set_range(ui_Player_Slider_Progress, 0, msg.total); break;

    // audio current track label
    case STATUS_UPDATE_TRACK_NUMBER: lv_label_set_text(ui_Player_Label_trackNumber, msg.trackNumber); break;

    // audio track description textarea
    case STATUS_UPDATE_TRACK_DESC_SET:
      lv_textarea_set_text(ui_Player_Textarea_status, msg.trackDesc); // new track
      break;
    case STATUS_UPDATE_TRACK_DESC_ADD:
      lv_textarea_add_text(ui_Player_Textarea_status, msg.trackDesc); // new track
      break;

    case STATUS_SCREEN_LOCK:
      lv_obj_clear_flag(ui_Player_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN); // unhide blind panel
      lv_obj_clear_flag(ui_MainMenu_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN); // unhide blind panel
      lv_obj_clear_flag(ui_Info_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN); // unhide blind panel
      break;

    case STATUS_SCREEN_UNLOCK:
      lv_obj_add_flag(ui_Player_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN); // hide blind panel
      lv_obj_add_flag(ui_MainMenu_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(ui_Info_Panel_blindPanel, LV_OBJ_FLAG_HIDDEN); // hide blind panel
      break;

    case STATUS_WIFI_CONNECTION:
      lv_label_set_text(ui_MainMenu_Label_connectStatus, msg.status);
      lv_obj_set_style_text_color(ui_MainMenu_Label_connectStatus, lv_color_hex(msg.labelcolor), LV_PART_MAIN);
      lv_obj_set_style_text_color(ui_Player_Label_WiFi, lv_color_hex(msg.wificolor), LV_PART_MAIN);
      break;

    case STATUS_WIFI_OPTION:
      if (msg.status[0] != '\0')//there are wifi in the list
       lv_dropdown_set_options(ui_MainMenu_Dropdown_NetworkList, msg.status);
       else
        lv_dropdown_clear_options(ui_MainMenu_Dropdown_NetworkList);
      break;

    case STATUS_SDCARD_STATUS:
       lv_label_set_text(ui_MainMenu_Label_trackCount, msg.status);
       lv_obj_set_style_text_color(ui_Player_Label_SDcard, lv_color_hex(msg.wificolor), LV_PART_MAIN);
      break;



    } // switch
  } // while
}

// process audio command
void process_audio_cmd_que() {
  AudioCommandPayload msg;

  while (xQueueReceive(audio_cmd_queue, &msg, 0) == pdTRUE) {

    switch (msg.cmd) {
    //PLAY AUDIO FILE
    case CMD_AUDIO_CONNECT_FS: {
      bool ok = false;
      UIStatusPayload payload = {
          // prepare mesasge
          .type = STATUS_UPDATE_TRACK_DESC_SET,
      };
      switch (msg.source) {
      case 0: ok = audio.connecttoFS(SD, msg.url_filename); break;
      case 1: ok = audio.connecttoFS(LittleFS, msg.url_filename); break;
      } // switch (msg.source)
      if (!ok) {
        log_w("Failed to open file: %s", msg.url_filename);
        audio.connecttoFS(LittleFS, "/audio/error.mp3");
        strncpy(payload.trackDesc, "Cannot access music.\nPlease check the SD Card.\nOr Update music library.", sizeof(payload.trackDesc) - 1);
      } else {
        strncpy(payload.trackDesc, "", sizeof(payload.trackDesc) - 1);
      }
      payload.trackDesc[sizeof(payload.trackDesc) - 1] = '\0';
      xQueueSend(ui_status_queue, &payload, 100); // send message
     break;
    }
    //Play URL
    case CMD_AUDIO_CONNECT_HOST: {
      bool ok = audio.connecttohost(msg.url_filename);
      UIStatusPayload payload = {
          .type = STATUS_UPDATE_TRACK_DESC_SET,
      };
      if (!ok) {
        log_w("Failed to open url: %s", msg.stationName);
        audio.connecttoFS(LittleFS, "/audio/error.mp3");
        strncpy(payload.trackDesc, "Network connection unavailable.\nPlease reconnect to continue streaming.", sizeof(payload.trackDesc) - 1);
      } else {
        strncpy(payload.trackDesc, msg.stationName, sizeof(payload.trackDesc) - 1);
      }
      payload.trackDesc[sizeof(payload.trackDesc) - 1] = '\0';
      xQueueSend(ui_status_queue, &payload, 100); // send message
     break;
    }

    //OTHER
    case CMD_AUDIO_STOP_SONG: audio.stopSong(); break;
    case CMD_AUDIO_SET_VOLUME: audio.setVolume(msg.value); break;
    case CMD_AUDIO_GET_CUR_TIME: break;
    case CMD_AUDIO_SET_FILE_POSITION: audio.setAudioFilePosition(msg.value); break;
    case CMD_AUDIO_SET_PLAY_TIME: audio.setAudioPlayTime(msg.value); break;
    case CMD_AUDIO_SET_TIME_OFFSET: audio.setTimeOffset(msg.value); break;
    case CMD_AUDIO_IS_RUNNING: break;
    case CMD_AUDIO_PAUSE_RESUME: audio.pauseResume(); break;
    case CMD_AUDIO_GET_FILE_DURATION: break;

    } // switch
  } // while
}