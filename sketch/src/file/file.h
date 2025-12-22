#pragma once

#include <Arduino.h>
#include <lvgl.h> 
  // SD CARD
#define SD_CS 38
#define SPI_MOSI 39
#define SPI_MISO 40
#define SPI_SCK 41

#define STATION_LIST_FILENAME "/stations.csv"
#define STRINGIFY(x) #x
#define MACRO_TO_STRING(x) STRINGIFY(x)

//------------ Music
extern uint16_t trackIndex;// current track index
extern int trackListLength;// all track count
extern uint8_t mediaType;// 0: livestream, 1: music player, 2: chatbot, 3: config

#define PLAYLIST_FILE "/music_playlist.txt"


//lvgl littleFS driver
void* fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode);
lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p);
lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br);
lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos);


void initLittleFS();
void initSDCard();
String getTrackPath(int index);
void scanMusic();
void initSongList();




//------------ RADIO
// 1. Define the struct normally (no 'extern' keyword needed here)
struct radios {
  String name;
  String url;
};

// 2. Declare the global array and variables using 'extern'
extern radios stations[50];
extern int16_t stationIndex;
extern uint8_t stationListLength;
extern uint8_t playMode;


void loadStationList();
bool copyStationsCSV_SD_to_LittleFS();

