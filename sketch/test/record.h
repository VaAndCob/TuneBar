#pragma once
#include <Arduino.h>
#include <driver/i2s_std.h>// must have to use i2s_chan_handle_t


// ----------------------------
// CONFIGURATION MACROS
// ----------------------------

#define I2S_CHANNELS        2
#define BITS_PER_SAMPLE     I2S_DATA_BIT_WIDTH_16BIT

#define RECORD_FILE_PATH    "/record.wav"
#define RECORD_SECONDS      5

// Your RX port
#define I2S_PORT            I2S_NUM_1



extern i2s_chan_handle_t rx_chan;


// ----------------------------
// WAV HEADER STRUCTURE
// ----------------------------
typedef struct {
    char     riff[4];
    uint32_t overall_size;

    char     wave[4];
    char     fmt_chunk_marker[4];

    uint32_t length_of_fmt;
    uint16_t format_type;
    uint16_t channels;

    uint32_t sample_rate;
    uint32_t byterate;
    uint16_t block_align;
    uint16_t bits_per_sample;

    char     data_chunk_header[4];
    uint32_t data_size;
} wav_header_t;


// ----------------------------
// PUBLIC API
// ----------------------------

// Configure I2S_RX (calls i2s_new_channel + std driver init)
//bool setupI2SRx();

// Record raw PCM from ES7210 → save as WAV
bool record_to_wav(const char* filename, int seconds);

// Wrapper to start LVGL async record task
void test_record_audio();

