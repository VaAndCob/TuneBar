#include "ui.h"
#include <Arduino.h>
#include <Preferences.h>
#include <stdlib.h> // สำหรับ atoi
Preferences pref;
#include "network/network.h"

// #include "record.h"
#include "file/file.h"
#include "lcd_bl_bsp/lcd_bl_pwm_bsp.h"
#include "pcf85063/pcf85063.h"
#include "task_msg/task_msg.h"
#include "updater/updater.h"
#include "weather/weather.h"
#include <LittleFS.h>
#include "lvgl_port/lvgl_port.h"

#include "ESP32-audioI2S-master/Audio.h"
#include "es7210/es7210.h"
extern Audio audio;
uint8_t audio_volume = 10;
extern ES7210 mic;

PCF85063 rtc;

uint8_t timeout_index = 0; // backlight timeout index
bool paused = false;
uint8_t wallpaperIndex = 0;
bool mute = false;

#define WIFI_CHECK_INTERVAL 5000 //wifi connection checking interval 5 second.

// set wallpaper dropdown menu
const char *wallpaper_menu = "None\nThailand\nChristmas1\nChristmas2\nFuture\nNature\nUser1\nUser2\nUser3\nUser4\nUser5";
// all wallaper image in c
const lv_img_dsc_t *wallpaper_list[6] = {
    NULL, &ui_img_wallpaper_thailand_png, &ui_img_wallpaper_christmas1_png, &ui_img_wallpaper_christmas2_png, &ui_img_wallpaper_future_png, &ui_img_wallpaper_nature_png,
};
//wifi monitor call back from timer
void lvgl_wifi_check_cb(lv_timer_t *t) {
  if (wifiEnable) {//if enable
    if (WiFi.status() != WL_CONNECTED && wifiTaskHandle == NULL) {//wifi disconnected
        wifi_need_connect = true;
        lv_timer_pause(wifi_check_timer);//pause the timer
        wifiConnect();  // create task ONCE
    }
  }  
}
//----------- LOGO SCREEN EVENTS ------------
void lv_create_delayed_task(lv_timer_cb_t callback, uint32_t delay_ms, void *user_data) {
  lv_timer_t *timer = lv_timer_create(callback, delay_ms, user_data); // Create a timer
  lv_timer_set_repeat_count(timer, 1); // Set the timer to run only once
}
// initalize lvgl ui
void init_main_menu_task(lv_timer_t *timer) {
  lv_timer_del(timer);
  lv_scr_load_anim(ui_Screen_MainMenu, LV_SCR_LOAD_ANIM_FADE_IN, 1000, 0, true); // delete old screen

  // init widgets
  lv_label_set_text(ui_Player_Label_Label1, LV_SYMBOL_VOLUME_MAX);
  lv_label_set_text(ui_Player_Label_Label2, LV_SYMBOL_VOLUME_MID);
  lv_label_set_text(ui_Player_Label_Label4, LV_SYMBOL_NEXT);
  lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PLAY);
  lv_label_set_text(ui_Player_Label_Label6, LV_SYMBOL_PREV);
  lv_label_set_text(ui_Player_Label_Label17, LV_SYMBOL_LEFT LV_SYMBOL_LEFT);
  lv_label_set_text(ui_Player_Label_Label18, LV_SYMBOL_RIGHT LV_SYMBOL_RIGHT);
  lv_label_set_text(ui_Player_Label_Label7, LV_SYMBOL_UP);
  lv_label_set_text(ui_Player_Label_Battery, LV_SYMBOL_BATTERY_EMPTY);
  lv_label_set_text(ui_Player_Label_WiFi, LV_SYMBOL_WIFI);
  lv_label_set_text(ui_Player_Label_SDcard, LV_SYMBOL_SD_CARD);
  lv_label_set_text(ui_MainMenu_Label_Label12, LV_SYMBOL_SAVE " Save");
  lv_label_set_text(ui_MainMenu_Label_Label13, LV_SYMBOL_WIFI " Scan");
  lv_label_set_text(ui_MainMenu_Label_Label14, LV_SYMBOL_UPLOAD " Load");
  lv_label_set_text(ui_MainMenu_Label_Label20, LV_SYMBOL_UPLOAD " Load");
  lv_label_set_text(ui_Player_Label_Label16, LV_SYMBOL_LOOP);
  uint8_t vol = 255 - (audio_volume * 255 / 21);
  lv_obj_set_style_bg_main_stop(ui_Player_Container_VolumeControl, vol, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_stop(ui_Player_Container_VolumeControl, vol, LV_PART_MAIN);
  lv_dropdown_set_options(ui_MainMenu_Dropdown_Wallpaper, wallpaper_menu); // set custom wallppaer list
  lv_label_set_text(ui_Info_Label_Label19, LV_SYMBOL_LEFT);
  lv_label_set_text(ui_Info_Label_Label22, LV_SYMBOL_RIGHT);
  lv_label_set_text(ui_Info_Label_Label21, LV_SYMBOL_DOWN);
  lv_label_set_text(ui_Utility_Label_Label27, LV_SYMBOL_UP);

  initSDCard(); // init SD Card
  loadStationList(); // load stations.csv from LittleFS
  initSongList(); // load music library from LittleFS

  // load configuration
  pref.begin("config", true); // read only
  backlight_state = pref.getUChar("bl_state", 2); // brightness
  lv_dropdown_set_selected(ui_MainMenu_Dropdown_Brightness, backlight_state);
  setBrightness(NULL);
  timeout_index = pref.getUChar("scroffdelay", 0); // screen off delay
  switch (timeout_index) {
  case 0: SCREEN_OFF_DELAY = 0; break;
  case 1: SCREEN_OFF_DELAY = 15000; break;
  case 2: SCREEN_OFF_DELAY = 30000; break;
  case 3: SCREEN_OFF_DELAY = 60000; break;
  }
  lv_dropdown_set_selected(ui_MainMenu_Dropdown_SleepTimer, timeout_index);
  playMode = pref.getUChar("playing_mode", 0); // playing mode
  switch (playMode) {
  case 0: lv_label_set_text(ui_Player_Label_Label16, LV_SYMBOL_LOOP); break;
  case 1: lv_label_set_text(ui_Player_Label_Label16, LV_SYMBOL_SHUFFLE); break;
  case 2: lv_label_set_text(ui_Player_Label_Label16, "1"); break;
  }
  wallpaperIndex = pref.getUChar("wallpaper", 1); // wallpaper
  lv_dropdown_set_selected(ui_MainMenu_Dropdown_Wallpaper, wallpaperIndex); // backlight timeout
  setWallpaper(NULL);

  // weater and clock
  size_t len = pref.getBytesLength("query_parameter");
  pref.getBytes("query_parameter", query_parameter, len);
  log_d("Region load: Query parameter: %s", query_parameter);
  if (strcmp(query_parameter, "auto:ip") == 0) { // audo:ip
    lv_obj_add_state(ui_MainMenu_Checkbox_AutoIP, LV_STATE_CHECKED);
    lv_obj_add_flag(ui_MainMenu_Textarea_Latitude, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(ui_MainMenu_Textarea_Longitude, LV_OBJ_FLAG_HIDDEN);
  } else { // lat,long

    char qp_copy[64];
    snprintf(qp_copy, sizeof(qp_copy), "%s", query_parameter);
    char *comma = strchr(qp_copy, ',');
    if (comma != NULL) {
      *comma = '\0';
      const char *lat = qp_copy;
      const char *lon = comma + 1;
      lv_obj_clear_state(ui_MainMenu_Checkbox_AutoIP, LV_STATE_CHECKED);
      lv_textarea_set_text(ui_MainMenu_Textarea_Latitude, lat);
      lv_textarea_set_text(ui_MainMenu_Textarea_Longitude, lon);
      lv_obj_clear_flag(ui_MainMenu_Textarea_Latitude, LV_OBJ_FLAG_HIDDEN);
      lv_obj_clear_flag(ui_MainMenu_Textarea_Longitude, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // utc timezone offset
  offset_hour_index = pref.getUChar("offset_hour", 14); // 0 offset
  lv_roller_set_selected(ui_MainMenu_Roller_Hour, offset_hour_index, LV_ANIM_OFF); // set index
  offset_minute_index = pref.getUChar("offset_minute", 3); // 0 offset
  lv_roller_set_selected(ui_MainMenu_Roller_Minute, offset_minute_index, LV_ANIM_OFF); // set index
  temp_unit = pref.getUChar("temp_unit", 0); // degree C
  lv_roller_set_selected(ui_MainMenu_Roller_Unit, temp_unit, LV_ANIM_OFF); // set index

  //wifi enable
  wifiEnable = pref.getBool("wifi_enable", true);

  pref.end();
  SCREEN_OFF_TIMER = millis(); // reset timer

  if (wifiEnable) {
    lv_obj_add_state(ui_MainMenu_Switch_Wifi, LV_STATE_CHECKED);
    //wifiConnect();
    wifi_check_timer = lv_timer_create(lvgl_wifi_check_cb, WIFI_CHECK_INTERVAL, NULL);//wifi status check interval timer

  } else {//init wifi stack once to prevent Audio library crashed
    WiFi.mode(WIFI_STA);
    WiFi.disconnect(true);
    log_d("WiFi stack initialized");
  }
}

// ################# App start here after screen initialized ##############################
void appStart(lv_event_t *e) {
  initLittleFS(); // iniit LittleFS
  audioSetVolume(audio_volume);
  audioPlayFS(1, "/audio/on.mp3");
  lv_create_delayed_task(init_main_menu_task, 2500, NULL); // show display logo for 2.5 second and start initializing widgets
}

//---------------- Volume control ------------------------------
void volumeup(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  audio_volume++;
  if (audio_volume > 20) audio_volume = 20;
  audioSetVolume(audio_volume);
  log_d("Volume %d", audio_volume);
  uint8_t vol = 255 - (audio_volume * 255 / 21);
  lv_obj_set_style_bg_main_stop(ui_Player_Container_VolumeControl, vol, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_stop(ui_Player_Container_VolumeControl, vol, LV_PART_MAIN);
  lv_anim_del(NULL, (lv_anim_exec_xcb_t)_ui_anim_callback_set_opacity); // remove animation
}

void volumemute(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  if (mute) {
    mute = false;
    audioSetVolume(audio_volume);
    lv_anim_del(NULL, (lv_anim_exec_xcb_t)_ui_anim_callback_set_opacity); // remove animation
    lv_obj_set_style_opa(ui_Player_Image_volumemute, LV_OPA_COVER, LV_PART_MAIN);
    log_d("Unmute volume %d", audio_volume);
  } else {
    mute = true;
    audioSetVolume(0);
    blink_Animation(ui_Player_Image_volumemute, 0); // add animation
  }
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
}

void volumedown(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  audio_volume--;
  if (audio_volume == 0) audio_volume = 0;
  audioSetVolume(audio_volume);
  log_d("Volume %d", audio_volume);
  uint8_t vol = 255 - (audio_volume * 255 / 21);
  lv_obj_set_style_bg_main_stop(ui_Player_Container_VolumeControl, vol, LV_PART_MAIN);
  lv_obj_set_style_bg_grad_stop(ui_Player_Container_VolumeControl, vol, LV_PART_MAIN);
  lv_anim_del(NULL, (lv_anim_exec_xcb_t)_ui_anim_callback_set_opacity); // remove animation
}

//----------- PLAY BACK CONTROL EVENTS ------------
// toggle play mode RPT or RND or LOOP
void togglePlayingMode(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  playMode++;
  if (playMode > 2) playMode = 0;
  log_d("Playing mode: %d", playMode);
  switch (playMode) {
  case 0: lv_label_set_text(ui_Player_Label_Label16, LV_SYMBOL_LOOP); break;
  case 1: lv_label_set_text(ui_Player_Label_Label16, LV_SYMBOL_SHUFFLE); break;
  case 2: lv_label_set_text(ui_Player_Label_Label16, "1"); break;
  }
  pref.begin("config", false); // save pref
  pref.putUChar("playing_mode", playMode);
  pref.end();
}
// skip previous track
void previoustrack(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PAUSE);
  lv_obj_set_style_radius(ui_Player_Button_play, 10, LV_PART_MAIN);

  char status_buffer[50];
  switch (mediaType) {

  case 0: { // livestream mode
    // playing mode
    if (playMode == 0) {
      if (stationIndex == 0) {
        stationIndex = stationListLength - 1;
      } else {
        stationIndex--;
      }
    } else if (playMode == 1) {
      uint16_t old = stationIndex;
      do {
        stationIndex = random(stationListLength);
      } while (stationListLength > 1 && stationIndex == old);

    } else if (mediaType == 2) {
      // play same current trackIndex
    }
    snprintf(status_buffer, sizeof(status_buffer), "%d of %d", stationIndex + 1, stationListLength);
    lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);
    audioPlayHOST(stations[stationIndex].url, stations[stationIndex].name);
    break; // break should be outside the scope block if not used for variable declaration
  }

  case 1: { // music player mode
    int pos = audio.getAudioCurrentTime();
    if (pos > 5) { // rewind if pos > 5 sec
      AudioCommandPayload msg = {
          .cmd = CMD_AUDIO_SET_PLAY_TIME,
          .value = 0,
      };
      xQueueSend(audio_cmd_queue, &msg, 0); // send message
      return;
    }
    // playing mode
    if (playMode == 0) {
      if (trackIndex == 0) {
        trackIndex = trackListLength - 1;
      } else {
        trackIndex--;
      }
    } else if (playMode == 1) {

      uint16_t old = trackIndex;
      do {
        trackIndex = random(trackListLength);
      } while (trackListLength > 1 && trackIndex == old);

    } else if (mediaType == 2) {
      // play same current trackIndex
    }

    char trackPath[512];
    getTrackPath(trackIndex, trackPath, sizeof(trackPath));
    log_d("Next track: %s", trackPath);
    snprintf(status_buffer, sizeof(status_buffer), "%d of %d", trackIndex + 1, trackListLength);
    lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);
    audioPlayFS(0, trackPath);
    break;
  }
  case 2: // chatbot mode
    // do nothing (or add specific action for chatbot here)
    break; // Cleaned up: only one break needed

  default:
    // It's good practice to handle unknown modes
    break;
  }
}

// skip back 5 sec
void fastReverse5(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  if (mediaType == 1) audioSetTimeOffset(-5);
}
// when user drag and release progress button
void playseek(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  if (mediaType == 1) {
    seeking_now = false; // disable skip slider knob position update during playing
    lv_obj_t *widget = lv_event_get_target(e);
    int16_t seek_time = lv_slider_get_value(widget);
    audioSetPlayTime(seek_time);
  }
}
// when user press on slider knob.
void SeekingNow(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  if (mediaType == 1) {
    seeking_now = true; // skip slider knob position update during playing
  }
}

// Play / Pause
void playpause(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  if (audio.isRunning()) {
    audioPauseResume();
    lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PLAY);
    lv_obj_set_style_radius(ui_Player_Button_play, 25, LV_PART_MAIN);
    paused = true;

  } else {

    lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PAUSE);
    lv_obj_set_style_radius(ui_Player_Button_play, 10, LV_PART_MAIN);
    char status_buffer[50];
    switch (mediaType) {
    case 0: { // livestream mode
      snprintf(status_buffer, sizeof(status_buffer), "%d of %d", stationIndex + 1, stationListLength);
      lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);
      audioPlayHOST(stations[stationIndex].url, stations[stationIndex].name);
      break; // break should be outside the scope block if not used for variable declaration
    }
    case 1: { // music player mode
      if (paused) {
        audioPauseResume();
        paused = false;
        return;
      }
      char trackPath[512];
      getTrackPath(trackIndex, trackPath, sizeof(trackPath));
      log_d("Play track: %s", trackPath);
      snprintf(status_buffer, sizeof(status_buffer), "%d of %d", trackIndex + 1, trackListLength);
      lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);

      audioPlayFS(0, trackPath);
      break;
    }
    case 2: // chatbot mode

        lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PLAY);
    lv_obj_set_style_radius(ui_Player_Button_play, 25, LV_PART_MAIN);
    paused = true;
            // do nothing (or add specific action for chatbot here)
      /*
            lv_textarea_set_text(ui_Player_Textarea_status, "Listenning...");
      audioStopSong();

      int32_t dummy[1024] = {0};
      size_t n;
      auto txh = audio.getTxHandle();
      i2s_channel_write(txh, dummy, sizeof(dummy), &n, 100);

      // ปลุกชิป ES7210
      if (!mic.start()) {
        log_e("ES7210 not detected");
        return;
      }
      delay(100);
      log_i("Recording Started at 48k");
      is_mic_mode = true;
      */
      break; // Cleaned up: only one break needed
    }
  }
}

// skip next 5 sec
void fastForward5(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  if (mediaType == 1) audioSetTimeOffset(5);
}

// skip next track
void nexttrack(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset backlight timer
  lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PAUSE);
  lv_obj_set_style_radius(ui_Player_Button_play, 10, LV_PART_MAIN);
  char status_buffer[50];
  switch (mediaType) {

  case 0: { // livestream mode
    if (playMode == 0) { // normal play
      stationIndex++;
      if (stationIndex == stationListLength) {
        stationIndex = 0;
      }
    } else if (playMode == 1) { // random
      uint16_t old = stationIndex;
      do {
        stationIndex = random(stationListLength);
      } while (stationListLength > 1 && stationIndex == old);
    } else if (mediaType == 2) { // single
      // play same current trackIndex
    }
    snprintf(status_buffer, sizeof(status_buffer), "%d of %d", stationIndex + 1, stationListLength);
    lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);
    audioPlayHOST(stations[stationIndex].url, stations[stationIndex].name);
    break; // break should be outside the scope block if not used for variable declaration
  }

  case 1: { // music player mode
    if (playMode == 0) { // normal play
      trackIndex++;
      if (trackIndex == trackListLength) {
        trackIndex = 0;
      }
    } else if (playMode == 1) { // random
      uint16_t old = trackIndex;
      do {
        trackIndex = random(trackListLength);
      } while (trackListLength > 1 && trackIndex == old);

    } else if (mediaType == 2) { // single
      // play same current trackIndex
    }
    char trackPath[512];
    getTrackPath(trackIndex, trackPath, sizeof(trackPath));
    log_d("Next track: %s", trackPath);
    snprintf(status_buffer, sizeof(status_buffer), "%d of %d", trackIndex + 1, trackListLength);
    lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);

    audioPlayFS(0, trackPath);
    break;
  }

  case 2: // chatbot mode
    // do nothing (or add specific action for chatbot here)
    break; // Cleaned up: only one break needed

  default:
    // It's good practice to handle unknown modes
    break;
  }
}

//  ------- Screen Control -------

//  Set backlight timeout event
void setTimer(lv_event_t *e) {
  lv_obj_t *widget = lv_event_get_target(e);
  timeout_index = lv_dropdown_get_selected(widget);
  switch (timeout_index) {
  case 0: SCREEN_OFF_DELAY = 0; break;
  case 1: SCREEN_OFF_DELAY = 15000; break;
  case 2: SCREEN_OFF_DELAY = 30000; break;
  case 3: SCREEN_OFF_DELAY = 60000; break;
  }
  SCREEN_OFF_TIMER = millis(); // reset timer
}
//  Set backlight brightness event
void setBrightness(lv_event_t *e) {
  // lv_obj_t *widget = lv_event_get_target(e);
  lv_obj_t *widget = ui_MainMenu_Dropdown_Brightness;
  backlight_state = lv_dropdown_get_selected(widget);
  switch (backlight_state) {
  case 0: setUpduty(LCD_PWM_MODE_100); break;
  case 1: setUpduty(LCD_PWM_MODE_150); break;
  case 2: setUpduty(LCD_PWM_MODE_255); break;
  }
  SCREEN_OFF_TIMER = millis(); // reset timer
}

// turn on screen by double tap
#define DOUBLE_TAP_SPEED 1000
void turnonScreen(lv_event_t *e) {
  static uint32_t last_click_time = 0;
  uint32_t current_time = lv_tick_get();
  if(current_time - last_click_time < DOUBLE_TAP_SPEED) {
       last_click_time = 0;
      log_d("screen on");
      switch (backlight_state) {
      case 0: setUpduty(LCD_PWM_MODE_100); break;
      case 1: setUpduty(LCD_PWM_MODE_150); break;
      case 2: setUpduty(LCD_PWM_MODE_255); break;
      }
      UIStatusPayload msg = {
        .type = STATUS_SCREEN_UNLOCK,
      };
      xQueueSend(ui_status_queue, &msg, 100); // send message
    resetScreenOffTimer(NULL);
  }
  last_click_time = current_time;  
}

// change wallpapaer with dropdown
void setWallpaper(lv_event_t *e) {
  // lv_obj_t *widget = lv_event_get_target(e);
  lv_obj_t *widget = ui_MainMenu_Dropdown_Wallpaper;
  wallpaperIndex = lv_dropdown_get_selected(widget);
  const void *img_src = NULL; // default no wallpaper
  if (wallpaperIndex < 6) // default wallper
    img_src = wallpaper_list[wallpaperIndex];
  else { // user custom wallapper load from littleFS
  }
  lv_obj_set_style_bg_img_src(ui_Screen_Player, img_src, LV_PART_MAIN);
  SCREEN_OFF_TIMER = millis(); // reset timer
}

/*

#include <esp_heap_caps.h> // Ensure you include this if using ps_malloc

// Function to load entire file from LittleFS into PSRAM
// The returned pointer MUST be freed when the image is no longer needed.
const uint8_t *load_file_from_fs(const char *lvgl_path, size_t expected_size) {
    if (lvgl_path == NULL) return NULL;

    // Strip the "L:" prefix (assuming path is "L:/wallpapers/xmas4.bin")
    const char *fs_path = lvgl_path + 2;

    if (!LittleFS.exists(fs_path)) {
        log_e("Load Fail: File not found at %s", fs_path);
        return NULL;
    }

    File f = LittleFS.open(fs_path, "r");
    if (!f) {
        log_e("Load Fail: Failed to open file: %s", fs_path);
        return NULL;
    }

    size_t size = f.size();
    if (size < expected_size) {
        log_w("Load Warning: File size mismatch. Expected %d, got %d", expected_size, size);
        // We will continue loading, but log the warning.
    }

    // Allocate buffer in PSRAM
    uint8_t *data = (uint8_t *)ps_malloc(size);

    if (data == NULL) {
        log_e("Load Fail: Failed to allocate %d bytes (PSRAM full?)", size);
        f.close();
        return NULL;
    }

    size_t bytesRead = f.read(data, size);
    f.close();

    if (bytesRead != size) {
        log_e("Load Fail: Read incomplete. Expected %d, got %d.", size, bytesRead);
        free(data);
        return NULL;
    }

    log_d("Load Success: %s loaded into PSRAM (%d bytes).", fs_path, size);
    return data;
}

lv_img_dsc_t  wallpaper_img_dsc = {
   {
     0,
        640,
        172,
       LV_IMG_CF_RGB565A8, // Assuming 3 bytes/pixel (640*172*3 = 330240)
    },
    0,
    NULL,
};


void setWallpaper(lv_event_t *e) {
    lv_obj_t *widget = ui_MainMenu_Dropdown_Wallpaper;
    int wallpaperIndex = lv_dropdown_get_selected(widget);

    const char *path = NULL;

    switch (wallpaperIndex) {
        case 0: path = NULL; break;
        case 1: path = "L:/wallpapers/wallpaper1.jpg"; break;
        case 2: path = "S:/wallpapers/wallpaper2.jpg"; break;
        case 3: path = "S:/wallpapers/wallpaper3.jpg"; break;
    }

    // CRITICAL: Free the previous memory allocation before loading new data
    if (wallpaper_img_dsc.data != NULL) {
        free((void *)wallpaper_img_dsc.data);
        wallpaper_img_dsc.data = NULL;
    }

    // Check if we are setting an image (case 0 is NULL)
    if (path != NULL) {
        // CALL THE EXPLICIT LOADING FUNCTION
        wallpaper_img_dsc.data = load_file_from_fs(path, 330240);
        wallpaper_img_dsc.data_size = 330240;
    } else {
        // Clear the source if the path is NULL
        wallpaper_img_dsc.data = NULL;
        wallpaper_img_dsc.data_size = 0;
    }

    log_d("Set Wallpaper: %s", path ? path : "NULL (Default)");

    lv_obj_set_style_bg_img_src(ui_Screen_Player, &wallpaper_img_dsc, LV_PART_MAIN);

    SCREEN_OFF_TIMER = millis();
}

*/
//----------------- Main Menu Selection events ---------------------
// mode 0 = live streaming , 1 = music player, 2 = ai chat
// Livestream mode event
void livestreamMode(lv_event_t *e) {
  if (mediaType != 0) { // switch from other mode
    char status_buffer[50];
    snprintf(status_buffer, sizeof(status_buffer), "%d of %d", stationIndex + 1, stationListLength);
    lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);
    lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PLAY);
    lv_obj_set_style_radius(ui_Player_Button_play, 25, LV_PART_MAIN);
    lv_textarea_set_text(ui_Player_Textarea_status, "");
    lv_label_set_text(ui_Player_Label_ElapseTime, "0:00");
    lv_label_set_text(ui_Player_Label_RemainTime, "0:00");
    lv_slider_set_value(ui_Player_Slider_Progress, 0, LV_ANIM_OFF);
    audioStopSong();
  }
  mediaType = 0;
  lv_anim_del(NULL, (lv_anim_exec_xcb_t)_ui_anim_callback_set_image_zoom);
  lv_img_set_zoom(ui_MainMenu_Image_MusicPlayer, 256);
  lv_img_set_zoom(ui_MainMenu_Image_ChatBot, 256);
  bounce_Animation(ui_MainMenu_Image_LiveStreaming, 0);
  lv_obj_set_style_bg_img_src(ui_Player_Container_albumCover, &ui_img_images_livestreaming_png, LV_PART_MAIN);
  SCREEN_OFF_TIMER = millis(); // reset timer
  BL_OFF = false; // auto backlight on
}
// Music player mode event
void musicPlayerMode(lv_event_t *e) {
  if (mediaType != 1) { // switch form other mode
    char status_buffer[50];
    snprintf(status_buffer, sizeof(status_buffer), "%d of %d", trackIndex + 1, trackListLength);
    lv_label_set_text(ui_Player_Label_trackNumber, status_buffer);
    lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PLAY);
    lv_obj_set_style_radius(ui_Player_Button_play, 25, LV_PART_MAIN);
    lv_textarea_set_text(ui_Player_Textarea_status, "");
    audioStopSong();
  }
  mediaType = 1;
  lv_anim_del(NULL, (lv_anim_exec_xcb_t)_ui_anim_callback_set_image_zoom);
  lv_img_set_zoom(ui_MainMenu_Image_LiveStreaming, 256);
  lv_img_set_zoom(ui_MainMenu_Image_ChatBot, 256);
  bounce_Animation(ui_MainMenu_Image_MusicPlayer, 0);
  lv_obj_set_style_bg_img_src(ui_Player_Container_albumCover, &ui_img_images_music_png, LV_PART_MAIN);
  SCREEN_OFF_TIMER = millis(); // reset timer
  BL_OFF = false; // auto backlight on
}
// Chatbot mode event
void chatBotMode(lv_event_t *e) {
  if (mediaType != 2) {
    lv_label_set_text(ui_Player_Label_trackNumber, "");
    lv_label_set_text(ui_Player_Label_Label5, LV_SYMBOL_PLAY);
    lv_obj_set_style_radius(ui_Player_Button_play, 25, LV_PART_MAIN);
    lv_textarea_set_text(ui_Player_Textarea_status, "");
     audioStopSong();
     audioPlayFS(1, "/audio/ai_not_support.mp3");
  }
  mediaType = 2;
  lv_textarea_set_text(ui_Player_Textarea_status, "Under development!\n");
  lv_anim_del(NULL, (lv_anim_exec_xcb_t)_ui_anim_callback_set_image_zoom);
  lv_img_set_zoom(ui_MainMenu_Image_MusicPlayer, 256);
  lv_img_set_zoom(ui_MainMenu_Image_LiveStreaming, 256);
  bounce_Animation(ui_MainMenu_Image_ChatBot, 0);
  lv_obj_set_style_bg_img_src(ui_Player_Container_albumCover, &ui_img_images_assistant_png, LV_PART_MAIN);
  SCREEN_OFF_TIMER = millis(); // reset timer
  BL_OFF = false; // auto backlight on
   
}
// informaiton Mode event
void informationMode(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  BL_OFF = false; // auto backlight on
  if (infoPageIndex == 0) {
    weatherAnimation(true);
  }
}
// Utility Mode event
void utilityMode(lv_event_t *e) {
  // performOnlineUpdate();
}

//-------------SAVE CONFIGURATION  Menu ----------------------
// config screen close and save setting to preference
void saveConfig(lv_event_t *e) {
  pref.begin("config", false);
  pref.putUChar("bl_state", backlight_state);
  pref.putUChar("scroffdelay", timeout_index);
  pref.putUChar("wallpaper", wallpaperIndex);
  // Region
  if (lv_obj_has_state(ui_MainMenu_Checkbox_AutoIP, LV_STATE_CHECKED)) {
    strcpy(query_parameter, "auto:ip");
  } else { // merge lat long into  lag,long
    const char *lat = lv_textarea_get_text(ui_MainMenu_Textarea_Latitude);
    const char *lon = lv_textarea_get_text(ui_MainMenu_Textarea_Longitude);
    snprintf(query_parameter, sizeof(query_parameter), "%s,%s", lat, lon);
  }
  log_d("Region saved: query_parameter = %s", query_parameter);
  pref.putBytes("query_parameter", query_parameter, strlen(query_parameter) + 1);
  //---------
  pref.putUChar("offset_hour", offset_hour_index);
  pref.putUChar("offset_minute", offset_minute_index);
  pref.putUChar("temp_unit", temp_unit);

  //wifienable
  pref.putBool("wifi_enable", wifiEnable);

  pref.end();
  SCREEN_OFF_TIMER = millis(); // reset timer
  if (WiFi.status() == WL_CONNECTED) {//sync time if wifi connected
      rtc.ntp_sync(UTC_offset_hour[offset_hour_index], UTC_offset_minute[offset_minute_index]);
   }
}

// user save wifi credential
void saveWiFiCredential(lv_event_t *e) {
  char newSSID[64];
  lv_dropdown_get_selected_str(ui_MainMenu_Dropdown_NetworkList, newSSID, sizeof(newSSID));
  // read from your input box
  const char *newPWD = lv_textarea_get_text(ui_MainMenu_Textarea_Password);
  // Load → update → save
  uint8_t wifiCount = loadWifiList(wifiList);
  wifiCount = addOrUpdateWifi(newSSID, newPWD, wifiList, wifiCount);
  saveWifiList(wifiList, wifiCount);
  SCREEN_OFF_TIMER = millis(); // reset timer
}

// Discovery wifi network into dropdown
void scanNetwork(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  scanWiFi(true); // scan and update wifi list in dropdown
}
//toggle wifi on/off
void toggleWiFi(lv_event_t * e) {
  SCREEN_OFF_TIMER = millis(); // reset timer

  if (wifiEnable) {//disabled
    wifiEnable = false;
    //delete wifi check timer
    if (wifi_check_timer) lv_timer_del(wifi_check_timer);
    if(wifiTaskHandle != NULL) {//kill wifi_connect_task
      vTaskDelete(wifiTaskHandle);
      wifiTaskHandle = NULL;
    }
    WiFi.disconnectAsync();
    lv_label_set_text(ui_MainMenu_Label_connectStatus, "Disconnected");
    lv_obj_set_style_text_color(ui_MainMenu_Label_connectStatus, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_text_color(ui_Player_Label_WiFi, lv_color_hex(0x777777), LV_PART_MAIN);
    log_d("WiFi Disconnected");

  } else {//enabled

    wifiEnable = true;
    //start wifi check timer
    wifi_check_timer = lv_timer_create(lvgl_wifi_check_cb, WIFI_CHECK_INTERVAL, NULL);//wifi status check interval timer
  }
}



//---------------------------------------
void readKeyboard(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  // Which button was pressed
  uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
  const char *txt = lv_btnmatrix_get_btn_text(kb, btn_id);
  if (txt == NULL) return;
  if (strcmp(txt, LV_SYMBOL_OK) == 0) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); // ✅ OK pressed → hide keyboard
  }
  SCREEN_OFF_TIMER = millis(); // reset timer
}

// force update song list from sdcard and creaet index file in littleFS
void loadMusicFromSDCARD(lv_event_t *e) {
  // Playlist does NOT exist (or rescan needed), start the background task
  scanMusic();
  SCREEN_OFF_TIMER = millis(); // reset timer
}

// copy stations.csv from sdcard to littleFS
void loadStationFromSDCARD(lv_event_t *e) {
  // if no file stations.csv in Sd card then load default
  if (copyStationsCSV_SD_to_LittleFS()) {
    loadStationList();
  } else {
    lv_textarea_set_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_WARNING " Error copy file " MACRO_TO_STRING(STATION_LIST_FILENAME) "\nFile not found\nPlease insert SD Card\nwith file " MACRO_TO_STRING(STATION_LIST_FILENAME));
  }
  SCREEN_OFF_TIMER = millis(); // reset timer
}
// reset SCREEN_OFF_TIMER timer on config panel
void resetScreenOffTimer(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  BL_OFF = false; // wake screen explicitly
}

//------------ Info screen events ---------------
static const uint8_t maxInfoPages = 2;
// uint8_t infoPageIndex = 0-> weather, 1->nixie clock (moved to pcf8603.h)

void updateInfoPanel(uint8_t pages) {
  log_d("Info Panel Page: %d", pages);
  switch (pages) {
  case 0: { // climate
    lv_obj_clear_flag(ui_Info_Panel_Weather, LV_OBJ_FLAG_HIDDEN); // show weather
    lv_obj_add_flag(ui_Info_Panel_Clock, LV_OBJ_FLAG_HIDDEN); // hide clock
    weatherAnimation(true);
    break;
  }
  case 1: { // nixie clock
    lv_obj_clear_flag(ui_Info_Panel_Clock, LV_OBJ_FLAG_HIDDEN); // show clock
    lv_obj_add_flag(ui_Info_Panel_Weather, LV_OBJ_FLAG_HIDDEN); // hide weather
    weatherAnimation(false);
    break;
  }
  case 2: { // setup
  }
  default: {

    break;
  }
  }
  SCREEN_OFF_TIMER = millis(); // reset timer
}

void changeInfoPanelPrev(lv_event_t *e) {
  if (infoPageIndex == 0)
    infoPageIndex = maxInfoPages - 1;
  else
    infoPageIndex--;
  updateInfoPanel(infoPageIndex);
}
void changeInfoPanelNext(lv_event_t *e) {
  infoPageIndex++;
  if (infoPageIndex >= maxInfoPages) infoPageIndex = 0;
  updateInfoPanel(infoPageIndex);
}
// show and hide arrow button on info panel
bool arrowVisible = false;
void hideArrow(lv_timer_t *timer) {
  log_d("hide arrow");
  lv_timer_del(timer);
  if (!arrowVisible) return;
  arrowVisible = false;
  lv_obj_add_flag(ui_Info_Button_returnMenu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Info_Button_infoPanelRight, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Info_Button_infoPanelLeft, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(ui_Utility_Button_returnMenu, LV_OBJ_FLAG_HIDDEN);
}
void showArrow(lv_event_t *e) {
  log_d("show arrow");
  if (arrowVisible) return;
  arrowVisible = true;
  lv_obj_clear_flag(ui_Info_Button_returnMenu, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_Info_Button_infoPanelRight, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_Info_Button_infoPanelLeft, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(ui_Utility_Button_returnMenu, LV_OBJ_FLAG_HIDDEN);
  lv_create_delayed_task(hideArrow, 5000, NULL);
  SCREEN_OFF_TIMER = millis(); // reset timer
}

//------------ Weather & AQI pollution update ---------------
void updateAQIwidget(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  updateWeatherPanel();
}
// set query paramter to auto detect IP address
void set_query_para_autoip(lv_event_t *e) {
  strcpy(query_parameter, "auto:ip");
  SCREEN_OFF_TIMER = millis(); // reset timer
}
// number pad
void readNumpad(lv_event_t *e) {
  lv_obj_t *kb = lv_event_get_target(e);
  // Which button was pressed
  uint16_t btn_id = lv_btnmatrix_get_selected_btn(kb);
  const char *txt = lv_btnmatrix_get_btn_text(kb, btn_id);
  if (txt == NULL) return;
  if (strcmp(txt, LV_SYMBOL_OK) == 0) {
    lv_obj_add_flag(kb, LV_OBJ_FLAG_HIDDEN); // ✅ OK pressed → hide keyboard
  }
  SCREEN_OFF_TIMER = millis(); // reset timer
}
// set UTC offset hour
void setOffsetHour(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  lv_obj_t *widget = lv_event_get_target(e);
  offset_hour_index = lv_roller_get_selected(widget);
}
// set UTC offset minute
void setOffsetMinute(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  lv_obj_t *widget = lv_event_get_target(e);
  offset_minute_index = lv_roller_get_selected(widget);
}
// set temperature unit // 0 = celcius ,1 = farenheit
void setTempUnit(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  lv_obj_t *widget = lv_event_get_target(e);
  temp_unit = lv_roller_get_selected(widget);
}

// Utility -----------------------
void torch_ON(lv_event_t *e) {
  setUpduty(LCD_PWM_MODE_255);
  BL_OFF = true; // auto backlight on
}
void torch_OFF(lv_event_t *e) {
  BL_OFF = false;
  setBrightness(NULL);
}
void showSystemInfo(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  // system info
  static char infoText[160];
  systemInfo(infoText, sizeof(infoText));
  lv_label_set_text(ui_Utility_Label_Build, infoText);

  // memory info
  static char memText[160];
  memoryInfo(memText, sizeof(memText));
  lv_label_set_text(ui_Utility_Label_Memory, memText);
}

void ota_update(lv_event_t *e) {
  SCREEN_OFF_TIMER = millis(); // reset timer
  ota_show_popup();
}