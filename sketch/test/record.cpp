#include <record.h>
#include <LittleFS.h>
#include "es8311/es8311.h"
#include "es7210/es7210.h"
//#include "ESP32-audioI2S-master/Audio.h"   // your audio playback object

//extern Audio audio;
extern ES7210 mic;  // I2S port 1

#define I2S_MIC_PORT I2S_NUM_1
#define I2S_SAMPLE_RATE 48000
#define I2S_CHANNELS 2
#define BITS_PER_SAMPLE I2S_DATA_BIT_WIDTH_16BIT
#define RECORD_FILE_PATH "/record.wav"
#define RECORD_SECONDS 5

//i2s_chan_handle_t rx_chan; // microphone channel handle




bool record_to_wav(const char* filename, int durationSeconds) {
    
  uint8_t bitWidthBytes = (BITS_PER_SAMPLE == I2S_DATA_BIT_WIDTH_16BIT) ? 2 : 4;
  uint32_t totalSamples = durationSeconds * I2S_SAMPLE_RATE * I2S_CHANNELS;
  uint32_t totalBytes = totalSamples * bitWidthBytes;

  // Allocate RAM buffer
  uint8_t* ramBuf = (uint8_t*)malloc(totalBytes);
  if (!ramBuf) return false;

  uint32_t bytesRecorded = 0;
  while (bytesRecorded < totalBytes) {
    size_t chunk = std::min<size_t>(1024 * 2, totalBytes - bytesRecorded);
    int bytesRead = mic.read(ramBuf + bytesRecorded, chunk);
    if (bytesRead <= 0) break;  // read error or no data
    bytesRecorded += bytesRead;
  }

  log_d("Record ended. Data: %lu/%lu bytes\n", bytesRecorded, totalBytes);
  size_t printBytes = std::min<size_t>(bytesRecorded, 1024);  // first 128 bytes
  for (size_t i = 0; i < printBytes; i++) {
    log_d("%02X ", ramBuf[i]);
  }
  log_d();

  // Write WAV header + buffer to LittleFS
  File f = LittleFS.open(filename, "wb");
  if (!f) {
    free(ramBuf);
    return false;
  }

  wav_header_t header = {};
  memcpy(header.riff, "RIFF", 4);
  memcpy(header.wave, "WAVE", 4);
  memcpy(header.fmt_chunk_marker, "fmt ", 4);
  header.length_of_fmt = 16;
  header.format_type = 1;
  header.channels = I2S_CHANNELS;
  header.sample_rate = I2S_SAMPLE_RATE;
  header.bits_per_sample = bitWidthBytes * 8;
  header.byterate = header.sample_rate * header.channels * bitWidthBytes;
  header.block_align = header.channels * bitWidthBytes;
  memcpy(header.data_chunk_header, "data", 4);
  header.data_size = bytesRecorded;
  header.overall_size = 36 + header.data_size;

  f.write((uint8_t*)&header, sizeof(header));
  f.write(ramBuf, bytesRecorded);

  free(ramBuf);
  f.close();

  log_d("Recording finished: %lu bytes\n", bytesRecorded);
  return bytesRecorded > 0;
}


void test_record_audio() {

  xTaskCreatePinnedToCore([](void*) {
    log_d("Start recording...");
    if (record_to_wav(RECORD_FILE_PATH, RECORD_SECONDS)) {
      log_d("Recording saved. Playing back...");
    //  audio.connecttoFS(LittleFS, RECORD_FILE_PATH, 0);
    } else {
      log_d("Recording failed");
    }
    vTaskDelete(NULL);
  },
                          "rec_task", 4096, NULL, 6, NULL, 1);
}
