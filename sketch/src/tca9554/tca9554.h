#pragma once

#include <Arduino.h>
#include "i2c_bsp/i2c_bsp.h"

#define INPUT_REGISTER   0x00
#define OUTPUT_REGISTER  0x01
#define CONFIG_REGISTER  0x03

extern const uint8_t EXIO1_BIT; // backlight
extern const uint8_t EXIO6_BIT; // Power
extern const uint8_t EXIO7_BIT; // audio amp

class TCA9554 {
public:
    uint8_t outputState;
    uint8_t configState;

    i2c_master_dev_handle_t dev;

    TCA9554(i2c_master_dev_handle_t handle);

    void begin();
    void setPinMode(uint8_t pin_mask, uint8_t mode);
    void digitalWrite(uint8_t pin_mask, uint8_t state);
    uint8_t digitalRead(uint8_t pin_mask);

private:
    void writeRegister(uint8_t reg, uint8_t data);
    uint8_t readRegister(uint8_t reg);
};


