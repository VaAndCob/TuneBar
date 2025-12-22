#pragma once
// global_handles.h (ประกาศ extern ใน Header File)

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

// ----------------------------------------------------
// A. Command Queue: Audio task message handle
// ----------------------------------------------------
typedef enum {
  // Audio.loop()
  CMD_AUDIO_CONNECT_FS,
  CMD_AUDIO_CONNECT_HOST,
  CMD_AUDIO_GET_CUR_TIME,
  CMD_AUDIO_GET_FILE_DURATION,
  CMD_AUDIO_STOP_SONG,
  CMD_AUDIO_SET_VOLUME,
  CMD_AUDIO_SET_FILE_POSITION,
  CMD_AUDIO_SET_PLAY_TIME,
  CMD_AUDIO_SET_TIME_OFFSET,
  CMD_AUDIO_IS_RUNNING,
  CMD_AUDIO_PAUSE_RESUME,



} AudioCommandType;

typedef struct {
  AudioCommandType cmd;
  char url_filename[256];//audio source url or filename
  int value;//for sending interger value back to audio
  uint8_t source;//audio source 0=SD or 1=LittleFS
  char stationName[100];


} AudioCommandPayload;

extern QueueHandle_t audio_cmd_queue;

// ----------------------------------------------------
// B. Status Queue: UI task message que handle
// ----------------------------------------------------
typedef enum {

  STATUS_UPDATE_BATTERY_LEVEL, // ✅ เพิ่ม Type นี้
  STATUS_UPDATE_CLOCK,
  STATUS_UPDATE_PLAY_POSITION,
  STATUS_UPDATE_PROGRESS_BAR,
  STATUS_UPDATE_TRACK_NUMBER,
  STATUS_UPDATE_TRACK_DESC_SET,
  STATUS_UPDATE_TRACK_DESC_ADD,
  STATUS_SCREEN_UNLOCK,
  STATUS_SCREEN_LOCK,
  STATUS_WIFI_CONNECTION,
  STATUS_WIFI_OPTION,
  STATUS_SDCARD_STATUS,



} UIStatusType;

typedef struct {
  UIStatusType type;

  // battery
  uint8_t battery_state; // battery state(0-4)
  // rtc
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
  uint16_t year;
  uint8_t month;
  uint8_t dayOfMonth;
  uint8_t dayOfWeek;
  // audio status
  char elapse_buf[16];
  char remain_buf[16];
  uint32_t current_pos;
  uint32_t total;//track length
  char trackNumber[12];// 1/12
  char trackDesc[100]; 
  //wifi
  char status[256];
  uint32_t labelcolor;
  uint32_t wificolor;





} UIStatusPayload;

extern QueueHandle_t ui_status_queue;

//message queue sender functions
void updateSDCARDStatus(const char *status,  const uint32_t wifiColor);
void updateWiFiStatus(const char *status, const uint32_t labelColor, const uint32_t wifiColor);
void updateWiFiOption(const char *status);
void audioSetAudioFilePosition(uint16_t pos);
void audioSetPlayTime(uint16_t pos);
void audioStopSong();
void audioPauseResume();
void audioSetTimeOffset(int offset);
void audioSetVolume(int volume);
void audioPlayFS(uint8_t source, const char *filename);
void audioPlayHOST(const char *filename, const char *stationName);


//Process queue in rtos task
void process_ui_status_queue(); 
void process_audio_cmd_que();

extern bool seeking_now;

