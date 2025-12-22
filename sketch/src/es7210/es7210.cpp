#include "es7210.h"
#include "i2c_bsp/i2c_bsp.h"
#include "es7210_reg.h"


ES7210::ES7210() {}
ES7210::~ES7210() {}


//i2s_chan_handle_t rx_handle = {};

esp_err_t ES7210::init_i2s1_mic_16k() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER); 
    
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_MSB_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_7,
            .bclk = GPIO_NUM_14, 
            .ws   = GPIO_NUM_15, 
            .dout = I2S_GPIO_UNUSED,
            .din  = GPIO_NUM_16, 
        },
    };

    err = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (err != ESP_OK) return err;

    err = i2s_channel_enable(rx_handle);
    return err;
}

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

bool ES7210::begin() {
    if (!reset()) return false;

    // ใช้ชื่อตาม es7210_reg.h ที่คุณหนุ่มส่งมาล่าสุด
    writeReg(ES7210_MODE_CONFIG_REG08, 0x02); 
    writeReg(ES7210_SDP_INTERFACE1_REG11, 0x00); // 0x00 = I2S, 16-bit
    
    // ตั้งค่า Analog & Power (ใช้เลขตาม .h)
    writeReg(ES7210_ANALOG_REG40, 0xC3); 
    writeReg(ES7210_MIC12_BIAS_REG41, 0x70); 
    
    writeReg(ES7210_MIC1_POWER_REG47, 0x08);
    writeReg(ES7210_MIC2_POWER_REG48, 0x08);

    setMicGain(0, 21.0f);
    setMicGain(1, 21.0f);
    
    stop(); 
    return true;
}

bool ES7210::start() {
    updateReg(ES7210_POWER_DOWN_REG06, 0x03, 0x00); // Power up ADC
    writeReg(0x14, 0x00); // Unmute ADC1/2
    return true;
}

bool ES7210::stop() {
    updateReg(ES7210_POWER_DOWN_REG06, 0x03, 0x03); // Power down ADC
    writeReg(0x14, 0x03); // Mute ADC1/2
    return true;
}

bool ES7210::setMicGain(uint8_t ch, float db) {
    uint8_t reg = (ch == 0) ? ES7210_MIC1_GAIN_REG43 : ES7210_MIC2_GAIN_REG44;
    uint8_t step = (uint8_t)(db / 3.0f);
    if (step > 0x0F) step = 0x0F;
    return writeReg(reg, step) == ESP_OK;
}
