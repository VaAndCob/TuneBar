#include "es7210.h"
#include "i2c_bsp/i2c_bsp.h"
#include "es7210_reg.h"


ES7210::ES7210() {}
ES7210::~ES7210() {}




esp_err_t ES7210::writeReg(uint8_t reg, uint8_t val) {
    if (!es7210_dev_handle) return ESP_FAIL;
    return i2c_write_buff(es7210_dev_handle, reg, &val, 1) == ESP_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t ES7210::readReg(uint8_t reg, uint8_t *val) {
    if (!es7210_dev_handle || val == nullptr) return ESP_FAIL;
    return i2c_read_buff(es7210_dev_handle, reg, val, 1) == ESP_OK ? ESP_OK : ESP_FAIL;
}


bool ES7210::updateReg(uint8_t reg, uint8_t mask, uint8_t value) {
    uint8_t old_val;
    if (readReg(reg, &old_val) != ESP_OK) return false;
    uint8_t new_val = (old_val & ~mask) | (value & mask);
    return writeReg(reg, new_val) == ESP_OK;
}

bool ES7210::reset() {
    if (writeReg(ES7210_RESET_REG00, 0xFF) != ESP_OK) return false; 
    vTaskDelay(pdMS_TO_TICKS(50));
    if (writeReg(ES7210_RESET_REG00, 0x41) != ESP_OK) return false;
    vTaskDelay(pdMS_TO_TICKS(10));
    return true;
}

bool ES7210::init()
{

      uint8_t id = 0;

    // ----------- check device responds ----------
    if (readReg(0x00, &id) != ESP_OK) {
        log_e("ES7210 not responding on I2C");
        return false;
    }
    writeReg( 0x00, 0xFF); // Reset
    delay(10);
    writeReg( 0x00, 0x41); 
    
    // 1. Power Management - สำคัญมาก [4]
    // ต้องเปิด Analog blocks (0x3F) ก่อนเพื่อให้ ADC และ Mic Bias พร้อมทำงาน
    writeReg( 0x01, 0x3F); 
    delay(10);
    writeReg( 0x01, 0x00); // Exit shutdown
    
    // 2. Clock & Format
    writeReg( 0x08, 0x00); // Slave Mode [5]
    writeReg( 0x09, 0x30); // Analog Mic Mode
    writeReg( 0x0B, 0x30); // I2S 16bit in 32bit frame [5]
    
    // 3. ADC & Mic Bias Setup
    writeReg( 0x10, 0x03); // Enable ADC1+ADC2 [6]
    writeReg( 0x11, 0x54); // L=ADC1, R=ADC1 [6]
    
    // เปิด Mic Bias (ต้องการ MCLK ที่เสถียร ณ จุดนี้) [6]
    writeReg( 0x40, 0x4B); 
    writeReg( 0x41, 0x78); 
    
    // 4. Analog Front-end
    writeReg( 0x43, 0x1F); // Gain
    writeReg( 0x44, 0x1F); // Gain
    writeReg( 0x4B, 0x00); // Power on MIC1/2 [7]
    writeReg( 0xFD, 0x00);

    return true;
}



bool ES7210::start() {
    uint8_t check;
    if (readReg(0x00, &check) != ESP_OK) {
        log_e("ES7210 I2C Error");
        return false;
    }

    // 1. Reset
    writeReg(0x00, 0xFF); 
    writeReg(0x00, 0x32); 

    // 2. Power Up (สำคัญ! ต้องสั่ง 0x06=0x00)
    writeReg(0x06, 0x00); 

    // 3. Clock Config
    writeReg(0x01, 0x00); // Clock ON
    writeReg(0x02, 0x00); // MCLK Div Auto
    writeReg(0x03, 0x10); // Auto MCLK detection

    // 4. Format: Slave, Standard I2S, 16-bit
    writeReg(0x08, 0x00); // 0x00 = Slave, I2S Normal
    writeReg(0x0B, 0x60); // 0x60 = 16-bit Data

    // 5. Input Config (Mic 1 & 3)
    writeReg(0x0E, 0x00); // ADC1 = Mic1
    writeReg(0x0F, 0x11); // ADC2 = Mic3
    writeReg(0x11, 0x14); // Output Stereo

    // 6. Analog & Bias
    writeReg(0x40, 0x42); // ADC/Bias ON
    writeReg(0x41, 0x70); // Mic 1/2 Bias High
    writeReg(0x42, 0x70); // Mic 3/4 Bias High

    // 7. Gain (+30dB)
    writeReg(0x43, 0x1E);
    writeReg(0x44, 0x1E);
    writeReg(0x45, 0x1E);
    writeReg(0x46, 0x1E);

    // 8. Power Up Inputs
    writeReg(0x47, 0x00);
    writeReg(0x48, 0x00);
    writeReg(0x49, 0x00);
    writeReg(0x4A, 0x00);

    // 9. Unmute
    writeReg(0x12, 0x00);
    writeReg(0x13, 0x00);
    writeReg(0x14, 0x00); 

    // 10. Start
    writeReg(0x09, 0x30);
    writeReg(0x0A, 0x30);

    return true;
}

bool ES7210::stop() {
    updateReg(ES7210_POWER_DOWN_REG06, 0x03, 0x03); // Power down ADC
    writeReg(0x14, 0x00); // Unmute ADC channels
    return true;
}

bool ES7210::setMicGain(uint8_t ch, float db) {
    uint8_t reg = (ch == 0) ? ES7210_MIC1_GAIN_REG43 : ES7210_MIC2_GAIN_REG44;
    uint8_t step = (uint8_t)(db / 3.0f);
    if (step > 0x0F) step = 0x0F;
    return writeReg(reg, step) == ESP_OK;
}

//=================================================

 bool is_mic_mode = false;