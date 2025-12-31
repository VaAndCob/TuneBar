#pragma once

#include <Arduino.h>
#include <driver/i2s_std.h>

extern i2s_chan_handle_t rx_handle;
class ES7210 {

private:

  esp_err_t writeReg(uint8_t reg, uint8_t val);
  esp_err_t readReg(uint8_t reg, uint8_t *val);
  bool updateReg(uint8_t reg, uint8_t mask, uint8_t value);


public:
  ES7210();
  ~ES7210();
  bool reset();
  bool init(); // Init ES7210 over I2C
  bool start(); // Enable ADC / I2S streaming
  bool stop(); // Disable ADC / I2S streaming
  bool setMicGain(uint8_t ch, float db);
};



  extern bool is_mic_mode;
