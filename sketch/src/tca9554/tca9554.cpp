#include "TCA9554.h"

// --- TCA9554 IO Expander DEFINITIONS ---


// Pin mapping: EXIO7 corresponds to the Most Significant Bit (MSB) P7
const uint8_t EXIO1_BIT = 0b00000001; // backlight

const uint8_t EXIO6_BIT = 0b01000000; // Power
const uint8_t EXIO7_BIT = 0b10000000; // audio amp


TCA9554::TCA9554(i2c_master_dev_handle_t handle) {
    dev = handle;
    outputState = 0x00;
    configState = 0xFF;
}

void TCA9554::begin() {
    writeRegister(CONFIG_REGISTER, configState);
    writeRegister(OUTPUT_REGISTER, outputState);
    log_i("TCA9554 OK");
}

void TCA9554::setPinMode(uint8_t pin_mask, uint8_t mode) {
    if (mode == 0)
        configState &= ~pin_mask;
    else
        configState |= pin_mask;

    writeRegister(CONFIG_REGISTER, configState);
}

void TCA9554::digitalWrite(uint8_t pin_mask, uint8_t state) {
    if (state)
        outputState |= pin_mask;
    else
        outputState &= ~pin_mask;

    writeRegister(OUTPUT_REGISTER, outputState);
}

uint8_t TCA9554::digitalRead(uint8_t pin_mask) {
    uint8_t value = readRegister(INPUT_REGISTER);
    return (value & pin_mask) ? 1 : 0;
}

void TCA9554::writeRegister(uint8_t reg, uint8_t data) {
    uint8_t buf[1] = { data };
    i2c_write_buff(dev, reg, buf, 1);
}

uint8_t TCA9554::readRegister(uint8_t reg) {
    uint8_t buf = 0;
    i2c_read_buff(dev, reg, &buf, 1);
    return buf;
}
