//=================== File System ===========================
#include "file.h"
#include "ui/ui.h"
#include "FS.h"
#include "SD.h"
#include "SPI.h"
#include "lvgl.h"
#include <Arduino.h>
#include <LittleFS.h>
#include <driver/i2s_std.h>
#include <vector>
#include "task_msg/task_msg.h"


int trackListLength = 0; // NEW: Definition of global track length
uint16_t trackIndex = 0;
uint8_t mediaType = 0; // 0: livestream, 1: music player, 2: chatbot, 3: config
uint8_t playMode = 0; // 0 = normal, 1 = random , 2 = repeat

static TaskHandle_t scanMusicTask = NULL;

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
//init LittleFS
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
     updateSDCARDStatus(LV_SYMBOL_CLOSE " Card Mount Failed",0x777777);
  } else {
     log_d("SD Card Mounted");
     updateSDCARDStatus(LV_SYMBOL_SD_CARD " SDCard Mounted",0x00FF00);
  }
}

// ==========================================
// Global Variables for Music List
// ==========================================
// std::vector acts like a growable array.
// It will store the full path strings to your songs.

bool isAudioFile(String filename) {
  String lowerName = filename;
  lowerName.toLowerCase();

  if (lowerName.startsWith("._")) // Check for macOS resource files
    return false;
  if (lowerName.endsWith(".mp3"))
    return true;
  else if (lowerName.endsWith(".aac"))
    return true;
  else if (lowerName.endsWith(".flac"))
    return true;
  else if (lowerName.endsWith(".opus"))
    return true;
  else if (lowerName.endsWith(".vorbis"))
    return true;
  else if (lowerName.endsWith(".wav"))
    return true;

  return false;
}

// ==========================================
// NEW: Recursive Directory Scanner that SAVES TO FILE
// ==========================================
void generatePlaylistFile(fs::FS &sourceFs, const char *dirname, uint8_t levels) {
  File playlist = LittleFS.open(PLAYLIST_FILE, FILE_WRITE);
  if (!playlist) {
    log_e("Failed to open LittleFS playlist file for writing!");
    return;
  }

  // 2. Start SD Card Scan
  log_d("Scanning directory: %s", dirname);

  File root = sourceFs.open(dirname);
  if (!root) {
    log_d("Failed to open directory");
    playlist.close();
    return;
  }
  if (!root.isDirectory()) {
    log_d("Not a directory");
    root.close();
    playlist.close();
    return;
  }
  int count = 0;
  // Define an internal recursive function (or refactor to pass File& to the original)
  std::function<void(fs::FS &, const char *, uint8_t)> recursiveScanner = [&](fs::FS &fs, const char *dirName, uint8_t level) {
    File dir = fs.open(dirName);
    if (!dir || !dir.isDirectory()) return;

    File entry = dir.openNextFile();
    while (entry) {
      if (entry.isDirectory()) {

        String dirEntryName = entry.name();
        // --- NEW: Filter out System and Trash Directories ---
        if (dirEntryName.equalsIgnoreCase(".Trashes") || // macOS/Linux Trash
            dirEntryName.equalsIgnoreCase("$RECYCLE.BIN") || // Windows Trash
            dirEntryName.startsWith(".") // General hidden/system folders
        ) {
          log_d("Skipping system directory: %s", dirEntryName.c_str());
        }
        // --- END FILTER ---
        else if (level > 0) {
          String newPath = String(dirName);
          if (!newPath.endsWith("/")) newPath += "/";
          newPath += entry.name();
          recursiveScanner(fs, newPath.c_str(), level - 1);
        }
      } else {
        if (isAudioFile(entry.name())) {
          String fullFilePath = String(dirName);
          if (!fullFilePath.endsWith("/")) fullFilePath += "/";
          fullFilePath += entry.name();

          playlist.println(fullFilePath); // Write to LittleFS file
          count++;
          log_d("%d - %s", count, fullFilePath.c_str());
        }
      }
      entry.close();
      entry = dir.openNextFile();
    }
    dir.close();
    vTaskDelay(1); // Yield to avoid watchdog reset
  };
  // Perform the scan
  recursiveScanner(sourceFs, dirname, levels);
  playlist.close();
  log_d("Playlist file generation complete.");
}

// ==========================================
// NEW: Low-RAM function to count lines (tracks)
// ==========================================
int getTrackCount() {
  File playlist = LittleFS.open(PLAYLIST_FILE, FILE_READ);
  if (!playlist) {
    log_w("Failed to open playlist file.");
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
String getTrackPath(int index) {
  File playlist = LittleFS.open(PLAYLIST_FILE, FILE_READ);
  if (!playlist) {
    log_w("Failed to open playlist file.");
    return "";
  }

  String path = "";
  int lineCount = 0;
  // Iterate until we find the target index (line number)
  while (playlist.available()) {
    path = playlist.readStringUntil('\n');
    if (lineCount == index) {
      // Found the line, stop reading
      break;
    }
    lineCount++;
  }
  playlist.close();
  // Clean up carriage return
  if (path.length() > 0 && path[path.length() - 1] == '\r') {
    path.remove(path.length() - 1);
  }

  return path;
}


// The dedicated SD Scan Task
void scan_music_task(void *pvParameters) {
   TaskHandle_t self = scanMusicTask;
   
  if (!SD.begin(SD_CS, SPI)) {
    log_w("X Card Mount Failed");
    updateSDCARDStatus(LV_SYMBOL_CLOSE " Card Mount Failed",0x777777);
    trackListLength = 0;
    
  } else {

  log_d("Indexing music library, please wait...");
  updateSDCARDStatus("Indexing music library, please wait...",0x00FF00);
  generatePlaylistFile(SD, "/", 5); // 2. Perform the blocking work: Scan and WRITE to LittleFS
  trackListLength = getTrackCount(); // 3. Update the global track count by counting lines in the new file
  String statusMsg = LV_SYMBOL_AUDIO " Found " + String(trackListLength) + " songs."; // 4. Update the UI status with results
  log_d("%s", statusMsg.c_str());
  updateSDCARDStatus(statusMsg.c_str(),0x00FF00);

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
    String statusMsg = LV_SYMBOL_AUDIO " Loaded " + String(trackListLength) + " songs from library";
    log_d("%s", statusMsg.c_str());
  } else {
    if (scanMusicTask == NULL) xTaskCreatePinnedToCore(scan_music_task, "SD_Scan_Task", 6 * 1024, NULL, 1, &scanMusicTask, 1); // Run on CPU 1 to keep UI responsive on CPU 0
  }
}

//------------------- Streaming Radio ----------------------------------
// load station list from files "stations.csv" or default
radios stations[50];
int16_t stationIndex = 0;
uint8_t stationListLength = 0;

// default stations list
static const char defaultStationsCSV[] PROGMEM = "Always Christmas Radio,http://185.33.21.112:80/christmas_128\n"
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

void parseCSVLine(const String &line, String &name, String &url) {
  int https = line.indexOf("https");
  if (https >= 0) {
     stationListLength--;
     return;//skip https, allow only http
  }
  int comma = line.indexOf(',');//not found comma
  if (comma < 0) return;
  
  name = line.substring(0, comma);
  url = line.substring(comma + 1);
}

// load station list from littleFS or default
void loadStationList() {
  stationListLength = 0;
  lv_textarea_set_text(ui_MainMenu_Textarea_stationList, "");
  if (!LittleFS.exists(STATION_LIST_FILENAME)) {
    // Load from default PROGMEM
    lv_textarea_add_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_FILE " Load DEFAULT " MACRO_TO_STRING(STATION_LIST_FILENAME) "\n");
    log_d("Load DEFAULT %s", STATION_LIST_FILENAME);

    // PROGMEM → RAM buffer reading
    char c;
    String line = "";
    for (uint32_t i = 0; i < strlen_P(defaultStationsCSV); i++) {
      c = pgm_read_byte_near(defaultStationsCSV + i);
      if (c == '\n') {
        if (line.length() > 3) {
          parseCSVLine(line, stations[stationListLength].name, stations[stationListLength].url);
          stationListLength++;
        }
        line = "";
      } else {
        line += c;
      }
    }

  } else {
    // Load from user CSV in LittleFS
    lv_textarea_add_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_FILE " Load USER " MACRO_TO_STRING(STATION_LIST_FILENAME) "\n");
    log_d("Load USER " MACRO_TO_STRING(STATION_LIST_FILENAME));

    File f = LittleFS.open(STATION_LIST_FILENAME, "r");
    if (!f) {
      lv_textarea_add_text(ui_MainMenu_Textarea_stationList, LV_SYMBOL_CLOSE " Cannot open " MACRO_TO_STRING(STATION_LIST_FILENAME) "\n");
      return;
    }

    while (f.available() && stationListLength < 50) {
      String line = f.readStringUntil('\n');
      line.trim();
      if (line.length() == 0) continue;

      parseCSVLine(line, stations[stationListLength].name, stations[stationListLength].url);
      stationListLength++;
    }

    f.close();
  }

  // Print summary
  for (int i = 0; i < stationListLength; i++) {
    String txt = String(i + 1) + ": " + stations[i].name + "\n";
    lv_textarea_add_text(ui_MainMenu_Textarea_stationList, txt.c_str());
  }

  String summary = "Total " + String(stationListLength) + " stations";
  lv_textarea_add_text(ui_MainMenu_Textarea_stationList, summary.c_str());
  log_d("%s", summary.c_str());
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
    log_w("Cannot open %s on SD",STATION_LIST_FILENAME);
    return false;
  }
  // --- Open destination file in LittleFS ---
  File dst = LittleFS.open(STATION_LIST_FILENAME, "w");
  if (!dst) {
    log_w("Cannot create %s in LittleFS",STATION_LIST_FILENAME);
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
  log_d("%s copied from SD → LittleFS",STATION_LIST_FILENAME);
  return true;
}