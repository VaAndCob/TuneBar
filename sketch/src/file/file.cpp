//=================== File System ===========================
#include "file.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "lvgl.h"
#include "task_msg/task_msg.h"
#include "ui/ui.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <driver/i2s_std.h>
#include <vector>

int trackListLength = 0; // NEW: Definition of global track length
uint16_t trackIndex = 0;
uint8_t mediaType = 0; // 0: livestream, 1: music player, 2: chatbot, 3: config
uint8_t playMode = 0; // 0 = normal, 1 = random , 2 = repeat

static TaskHandle_t scanMusicTask = NULL;

#define PATH_BUF_LEN 512

//------------ LVGL 8.x LittleFS callbacks  -------------------------

void *fs_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode) {
  // 1. Allocate a File object on the heap. This will be the pointer returned to LVGL.
  File *fp = new File();
  // 2. Determine the open mode string.
  const char *open_mode = (mode == LV_FS_MODE_RD) ? "r" : "w";
  // 3. Open the file directly into the heap-allocated object (*fp).
  *fp = LittleFS.open(path, open_mode);
  // 4. Check for failure.
  if (!*fp) {
    delete fp; // Crucial: Clean up the heap allocation if opening failed.
    return nullptr;
  }
  // 5. Success. Return the heap-allocated pointer.
  return fp;
}

lv_fs_res_t fs_close(lv_fs_drv_t *drv, void *file_p) {
  if (!file_p) return LV_RES_INV;
  File *fp = (File *)file_p;
  fp->close();
  delete fp;
  return LV_FS_RES_OK;
}

lv_fs_res_t fs_read(lv_fs_drv_t *drv, void *file_p, void *buf, uint32_t btr, uint32_t *br) {
  if (!file_p) return LV_RES_INV;
  File *fp = (File *)file_p;
  *br = fp->read((uint8_t *)buf, btr);
  return LV_FS_RES_OK;
}

lv_fs_res_t fs_seek(lv_fs_drv_t *drv, void *file_p, uint32_t pos, lv_fs_whence_t whence) {
  if (!file_p) return LV_RES_INV;
  File *fp = (File *)file_p;
  SeekMode mode;
  // 1. Map the LVGL 'whence' (reference point) to the Arduino 'SeekMode'
  switch (whence) {
  case LV_FS_SEEK_SET:
    mode = SeekSet; // Start of file
    break;
  case LV_FS_SEEK_CUR:
    mode = SeekCur; // Current position
    break;
  case LV_FS_SEEK_END:
    mode = SeekEnd; // End of file
    break;
  default: return LV_RES_INV;
  }
  // 2. Call the correct File::seek() overload
  fp->seek(pos, mode);
  return LV_FS_RES_OK;
}

//------------------------------------------------
// init LittleFS
void initLittleFS() {
  if (!LittleFS.begin(false)) {
    log_e("LittleFS mount failed. Formatting...");
    if (!LittleFS.begin(true)) {
      log_e("LittleFS format failed!");
    }
    log_d("LittleFS formatted successfully.");
  } else {
    log_d("LittleFS mounted successfully.");

    lv_fs_drv_t drv;
    lv_fs_drv_init(&drv);
    drv.letter = 'L';
    drv.open_cb = fs_open;
    drv.close_cb = fs_close;
    drv.read_cb = fs_read;
    drv.seek_cb = fs_seek;
    lv_fs_drv_register(&drv);
  }
}
//------------------------------------------------
// init sd card
void initSDCard() {
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
  SPI.begin(SPI_SCK, SPI_MISO, SPI_MOSI);
  SPI.setFrequency(4000000);
  delay(100);
  if (!SD.begin(SD_CS, SPI)) {
    log_w("SD Card Mount Failed");
    updateSDCARDStatus(LV_SYMBOL_CLOSE " Card Mount Failed", 0x777777);
  } else {
    log_d("SD Card Mounted");
    updateSDCARDStatus(LV_SYMBOL_SD_CARD " SDCard Mounted", 0x00FF00);
  }
}

// ==========================================
// Global Variables for Music List
// ==========================================
// std::vector acts like a growable array.
// It will store the full path strings to your songs.
static bool endsWithIgnoreCase(const char *str, const char *suffix) {
  size_t len1 = strlen(str);
  size_t len2 = strlen(suffix);
  if (len2 > len1) return false;

  str += len1 - len2;
  while (*suffix) {
    if (tolower((unsigned char)*str++) != tolower((unsigned char)*suffix++)) return false;
  }
  return true;
}

static bool isAudioFile(const char *name) {
    const char *ext = strrchr(name, '.');
    if (!ext) return false;

    return strcasecmp(ext, ".mp3") == 0 ||
           strcasecmp(ext, ".wav") == 0 ||
           strcasecmp(ext, ".aac") == 0 ||
           strcasecmp(ext, ".flac") == 0;
}


// ==========================================
// NEW: Recursive Directory Scanner that SAVES TO FILE
// ==========================================
static void scanDirRecursive(
    fs::FS &fs,
    const char *currentDir,
    uint8_t level,
    File &playlist,
    char *pathBuf,
    size_t pathBufLen
) {
    File dir = fs.open(currentDir);
    if (!dir || !dir.isDirectory()) {
        dir.close();
        return;
    }

    while (true) {
        File entry = dir.openNextFile();
        if (!entry) break;

        const char *entryPath = entry.name();  // FULL path on SD
        const char *base = strrchr(entryPath, '/');
        base = base ? base + 1 : entryPath;    // basename ONLY

        // Build full child path into pathBuf
        if (strcmp(currentDir, "/") == 0) {
            snprintf(pathBuf, pathBufLen, "/%s", base);
        } else {
            snprintf(pathBuf, pathBufLen, "%s/%s", currentDir, base);
        }

        if (entry.isDirectory()) {

            if (base[0] != '.' && level > 0) {
                char childDir[PATH_BUF_LEN];
                strncpy(childDir, pathBuf, sizeof(childDir));
                childDir[sizeof(childDir) - 1] = '\0';

                scanDirRecursive(
                    fs,
                    childDir,          // ✅ stable copy
                    level - 1,
                    playlist,
                    pathBuf,
                    pathBufLen
                );
            }

        } else {

            if (isAudioFile(base)) {
                playlist.println(pathBuf);
                log_d("-> %s", pathBuf);
            }
        }

        entry.close();
        vTaskDelay(1);
    }

    dir.close();
}




//create music_playlist.txt
void generatePlaylistFile(fs::FS &sourceFs, const char *dirname, uint8_t levels) {

    LittleFS.remove(PLAYLIST_FILE);
    File playlist = LittleFS.open(PLAYLIST_FILE, FILE_WRITE);

  
    if (!playlist) {
        log_e("Failed to open playlist file for writing!");
        return;
    }

    char *pathBuf = (char *)heap_caps_malloc(PATH_BUF_LEN, MALLOC_CAP_SPIRAM);
    if (!pathBuf) {
        log_e("Failed to allocate path buffer");
        playlist.close();
        return;
    }

    log_d("Scanning directory: %s", dirname);
scanDirRecursive(
    sourceFs,
    dirname,      // "/" typically
    levels,
    playlist,
    pathBuf,
    PATH_BUF_LEN
);


    heap_caps_free(pathBuf);
    playlist.close();
    log_d("Playlist file %s generation complete.", PLAYLIST_FILE);
}


// ==========================================
// NEW: Low-RAM function to count lines (tracks)
// ==========================================
int getTrackCount() {
  File playlist = LittleFS.open(PLAYLIST_FILE, FILE_READ);
  if (!playlist) {
    log_w("Failed to open playlist file: %s",PLAYLIST_FILE);
    return 0;
  }

  int count = 0;
  while (playlist.available()) {
    playlist.readStringUntil('\n'); // Read line without storing it (low RAM)
    count++;
  }
  playlist.close();
  return count;
}

// ==========================================
// NEW: Low-RAM function to read a track path by index (line number)
// ==========================================
bool getTrackPath(int index, char *outBuf, size_t outBufSize) {
  if (!outBuf || outBufSize == 0) return false;

  File playlist = LittleFS.open(PLAYLIST_FILE, FILE_READ);
  if (!playlist) {
    log_w("Failed to open playlist file.");
    outBuf[0] = '\0';
    return false;
  }

  int lineCount = 0;
  size_t len = 0;

  while (playlist.available()) {
    len = playlist.readBytesUntil('\n', outBuf, outBufSize - 1);
    outBuf[len] = '\0';

    // Strip trailing CR (\r)
    if (len > 0 && outBuf[len - 1] == '\r') {
      outBuf[len - 1] = '\0';
    }

    if (lineCount == index) {
      playlist.close();
      return true;
    }

    lineCount++;
  }

  playlist.close();
  outBuf[0] = '\0';
  return false; // index not found
}

// The dedicated SD Scan Task
void scan_music_task(void *pvParameters) {
  TaskHandle_t self = scanMusicTask;

  if (!SD.begin(SD_CS, SPI)) {
    log_w("X Card Mount Failed");
    updateSDCARDStatus(LV_SYMBOL_CLOSE " Card Mount Failed", 0x777777);
    trackListLength = 0;

  } else {

    log_d("Indexing music library, please wait...");
    updateSDCARDStatus("Indexing music library, please wait...", 0x00FF00);
    generatePlaylistFile(SD, "/", 5); // 2. Perform the blocking work: Scan and WRITE to LittleFS
    trackListLength = getTrackCount(); // 3. Update the global track count by counting lines in the new file
    char buffer[100];
    snprintf(buffer, sizeof(buffer), LV_SYMBOL_AUDIO " Found %d songs.", trackListLength);
    updateSDCARDStatus(buffer, 0x00FF00);
    log_d("%s", buffer);

    UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
    log_d("{ Task stack remaining MIN: %u bytes }", hwm);
  }
  scanMusicTask = NULL;
  vTaskDelete(self);
}

void scanMusic() {
  if (scanMusicTask == NULL) xTaskCreatePinnedToCore(scan_music_task, "SD_Scan_Task", 6 * 1024, NULL, 1, &scanMusicTask, 1);
}

// load music
void initSongList() {
  log_d("Load MUSIC library");
  // NEW: Check if the playlist file exists
  if (LittleFS.exists(PLAYLIST_FILE)) {
    // Playlist exists, just read the count and notify
    trackListLength = getTrackCount();
    char statusMsg[100];
    snprintf(statusMsg, sizeof(statusMsg), LV_SYMBOL_AUDIO " Loaded %d songs from library", trackListLength);
    log_d("%s", statusMsg);
  } else {
    if (scanMusicTask == NULL) xTaskCreatePinnedToCore(scan_music_task, "SD_Scan_Task", 6 * 1024, NULL, 1, &scanMusicTask, 1); // Run on CPU 1 to keep UI responsive on CPU 0
  }
}

//------------------- Streaming Radio ----------------------------------
// load station list from files "stations.csv" or default
radios stations[MAX_STATION_LIST_LENGTH];
int16_t stationIndex = 0;
uint8_t stationListLength = 0;

// default stations list
const char defaultStationsCSV[] PROGMEM = "Always Christmas Radio,http://185.33.21.112:80/christmas_128\n"
                                          "Klassik Radio,http://stream.klassikradio.de/christmas/mp3-128/radiode\n"
                                          "Chou Chou,http://stream1.10223.cc:8025/chouchou_ch\n"
                                          "Smooth Loungue,http://smoothjazz.cdnstream1.com/2586_128.mp3\n"
                                          "Solo Piano Radio,http://pianosolo.streamguys.net/live\n"
                                          "Muddy's Music Cafe,http://muddys.digistream.info:20398\n"
                                          "Got Radio,http://206.217.213.235:8040/\n"
                                          "Chill Step,http://chillstep.info:1984/listen.mp3\n"
                                          "Cinemix,http://kathy.torontocast.com:1190/stream\n"
                                          "Radionomy,http://listen.radionomy.com:80/InstrumentalBreezes\n"
                                          "Slow Radio,http://stream3.slowradio.com:80\n"
                                          "Baroque,http://strm112.1.fm/baroque_mobile_mp3\n"
                                          "Top Radio FM93.5,http://a10.asurahosting.com:8250/radio.mp3?refresh=1751553164542\n";

bool parseCSVLine(const char *line, char *name, size_t nameSize, char *url, size_t urlSize) {
  if (!line || !name || !url) return false;

  // Skip https streams
  if (strstr(line, "https://") != NULL) {
    return false;
  }

  const char *comma = strchr(line, ',');
  if (!comma) return false;

  // Copy station name
  size_t nameLen = comma - line;
  if (nameLen >= nameSize) nameLen = nameSize - 1;
  memcpy(name, line, nameLen);
  name[nameLen] = '\0';

  // Copy URL (strip CR/LF)
  const char *urlStart = comma + 1;
  size_t urlLen = strcspn(urlStart, "\r\n");
  if (urlLen >= urlSize) urlLen = urlSize - 1;
  memcpy(url, urlStart, urlLen);
  url[urlLen] = '\0';

  return true;
}

// load station list from littleFS or default
void loadStationList() {
  stationListLength = 0;
  lv_textarea_set_text(ui_MainMenu_Textarea_stationList, "");

  char *lineBuf = (char *)heap_caps_malloc(LINE_BUF_LEN, MALLOC_CAP_SPIRAM);
  size_t lineLen = 0;

  if (!LittleFS.exists(STATION_LIST_FILENAME)) {
    // ---------- LOAD DEFAULT (PROGMEM) ----------
    lv_textarea_add_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_FILE " Load DEFAULT " MACRO_TO_STRING(STATION_LIST_FILENAME) "\n");
    log_d("Load DEFAULT %s", STATION_LIST_FILENAME);

    for (uint32_t i = 0; i < strlen_P(defaultStationsCSV); i++) {
      char c = pgm_read_byte_near(defaultStationsCSV + i);

      if (c == '\n' || lineLen >= LINE_BUF_LEN - 1) {
        lineBuf[lineLen] = '\0';

        if (stationListLength < MAX_STATION_LIST_LENGTH && parseCSVLine(lineBuf, stations[stationListLength].name, sizeof(stations[stationListLength].name), stations[stationListLength].url, sizeof(stations[stationListLength].url))) {
          stationListLength++;
        }
        lineLen = 0;
      } else {
        lineBuf[lineLen++] = c;
      }
    }

    // Handle last line (no trailing newline)
    if (lineLen > 0 && stationListLength < MAX_STATION_LIST_LENGTH) {
      lineBuf[lineLen] = '\0';
      if (parseCSVLine(lineBuf, stations[stationListLength].name, sizeof(stations[stationListLength].name), stations[stationListLength].url, sizeof(stations[stationListLength].url))) {
        stationListLength++;
      }
    }

  } else {
    // ---------- LOAD USER CSV (LittleFS) ----------
    lv_textarea_add_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_FILE " Load USER " MACRO_TO_STRING(STATION_LIST_FILENAME) "\n");
    log_d("Load USER " MACRO_TO_STRING(STATION_LIST_FILENAME));

    File f = LittleFS.open(STATION_LIST_FILENAME, "r");
    if (!f) {
      lv_textarea_add_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_CLOSE " Cannot open " MACRO_TO_STRING(STATION_LIST_FILENAME) "\n");
      return;
    }

    while (f.available() && stationListLength < MAX_STATION_LIST_LENGTH) {
      lineLen = f.readBytesUntil('\n', lineBuf, LINE_BUF_LEN - 1);
      lineBuf[lineLen] = '\0';

      if (lineLen == 0) continue;

      if (parseCSVLine(lineBuf, stations[stationListLength].name, sizeof(stations[stationListLength].name), stations[stationListLength].url, sizeof(stations[stationListLength].url))) {
        stationListLength++;
      }
    }
    f.close();
  }

  // ---------- PRINT SUMMARY ----------
  char txt[96];
  for (int i = 0; i < stationListLength; i++) {
    snprintf(txt, sizeof(txt), "%d: %s\n", i + 1, stations[i].name);
    lv_textarea_add_text(ui_MainMenu_Textarea_stationList, txt);
  }

  snprintf(txt, sizeof(txt), "Total %d stations", stationListLength);
  lv_textarea_add_text(ui_MainMenu_Textarea_stationList, txt);
  log_d("%s", txt);
}

// copy file 'stations.csv' to littleFS
bool copyStationsCSV_SD_to_LittleFS() {
  // --- Init SD ---
  if (!SD.begin(SD_CS, SPI)) {
    log_w("SD mount failed");
    return false;
  }
  // --- Check file exists on SD ---
  if (!SD.exists(STATION_LIST_FILENAME)) {
    log_w("%s not found on SD", STATION_LIST_FILENAME);
    return false;
  }
  File src = SD.open(STATION_LIST_FILENAME, "r");
  if (!src) {
    log_w("Cannot open %s on SD", STATION_LIST_FILENAME);
    return false;
  }
  // --- Open destination file in LittleFS ---
  File dst = LittleFS.open(STATION_LIST_FILENAME, "w");
  if (!dst) {
    log_w("Cannot create %s in LittleFS", STATION_LIST_FILENAME);
    src.close();
    return false;
  }
  // --- Perform buffered copy ---
  uint8_t buf[512];
  size_t len;

  while ((len = src.read(buf, sizeof(buf))) > 0) {
    dst.write(buf, len);
  }
  src.close();
  dst.close();
  log_d("%s copied from SD → LittleFS", STATION_LIST_FILENAME);
  return true;
}