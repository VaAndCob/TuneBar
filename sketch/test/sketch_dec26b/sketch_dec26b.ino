#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include "driver/i2s_std.h"

#include "driver/gpio.h"



#define I2C_SDA 47
#define I2C_SCL 48

#define I2S_BCLK 15
#define I2S_LRCK 46
#define I2S_MCLK 7
#define I2S_DIN 6    // ES7210 -> ESP32
#define I2S_DOUT 45  // ESP32 -> ES8311

#define ES7210_ADDR 0x40
#define ES8311_ADDR 0x18

i2s_chan_handle_t rxh, txh;

uint8_t read_reg(uint8_t addr, uint8_t reg) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return 0xFF;

  Wire.requestFrom(addr, (uint8_t)1);
  if (!Wire.available()) return 0xFF;

  return Wire.read();
}

void scan_bus() {
  Serial.println("=== I2C DEVICE SCAN ===");

  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.print("FOUND DEVICE: @ 0x");
      Serial.println(a, HEX);
    }
  }
}

void i2c_check_register(uint8_t addr) {
  delay(100);
  Serial.printf("\n=== ES7210 CHECK I2C address 0x%02X ===\n", addr);
  uint8_t regs[] = { 0x0E, 0x0F, 0x09, 0x10, 0x11, 0x12, 0x13, 0x0B, 0xFD };

  for (uint8_t i = 0; i < 9; i++) {
    uint8_t v = read_reg(addr, regs[i]);
    Serial.printf("Reg 0x%02X = 0x%02X\n", regs[i], v);
  }
}

void write_reg(uint8_t addr, uint8_t reg, uint8_t val) {
  Wire.beginTransmission(addr);
  Wire.write(reg);
  Wire.write(val);
  Wire.endTransmission(true);
}

void es7210_init() {
  Serial.println("Initializing ES7210...");
    write_reg(ES7210_ADDR, 0x00, 0xFF); // Reset
    delay(10);
    write_reg(ES7210_ADDR, 0x00, 0x41); 
    
    // 1. Power Management - สำคัญมาก [4]
    // ต้องเปิด Analog blocks (0x3F) ก่อนเพื่อให้ ADC และ Mic Bias พร้อมทำงาน
    write_reg(ES7210_ADDR, 0x01, 0x3F); 
    delay(10);
    write_reg(ES7210_ADDR, 0x01, 0x00); // Exit shutdown
    
    // 2. Clock & Format
    write_reg(ES7210_ADDR, 0x08, 0x00); // Slave Mode [5]
    write_reg(ES7210_ADDR, 0x09, 0x30); // Analog Mic Mode
    write_reg(ES7210_ADDR, 0x0B, 0x30); // I2S 16bit in 32bit frame [5]
    
    // 3. ADC & Mic Bias Setup
    write_reg(ES7210_ADDR, 0x10, 0x03); // Enable ADC1+ADC2 [6]
    write_reg(ES7210_ADDR, 0x11, 0x54); // L=ADC1, R=ADC1 [6]
    
    // เปิด Mic Bias (ต้องการ MCLK ที่เสถียร ณ จุดนี้) [6]
    write_reg(ES7210_ADDR, 0x40, 0x4B); 
    write_reg(ES7210_ADDR, 0x41, 0x78); 
    
    // 4. Analog Front-end
    write_reg(ES7210_ADDR, 0x43, 0x1F); // Gain
    write_reg(ES7210_ADDR, 0x44, 0x1F); // Gain
    write_reg(ES7210_ADDR, 0x4B, 0x00); // Power on MIC1/2 [7]
    write_reg(ES7210_ADDR, 0xFD, 0x00);

}

void es8311_init() {
  // Reset
  write_reg(ES8311_ADDR, 0x00, 0x1F);
  delay(10);
  write_reg(ES8311_ADDR, 0x00, 0x00);

  // Power up analog + digital
  write_reg(ES8311_ADDR, 0x01, 0x00);

  // -----------------------------
  // CLOCK & MASTER MODE – CRITICAL
  // -----------------------------
   write_reg(ES8311_ADDR, 0x02, 0x10);   // slave, I2S, 24-bit
//  write_reg(ES8311_ADDR, 0x02, 0x30);  // MASTER, I2S, 24-bit
  write_reg(ES8311_ADDR, 0x03, 0x10);  // MCLK = 256Fs
  write_reg(ES8311_ADDR, 0x16, 0x00);  // Fs = 48kHz

  // Select MCLK as system clock source
  write_reg(ES8311_ADDR, 0x01, 0x00);  // ensure sysclk enabled

  // Enable PLL (required to drive BCLK/LRCK)
  write_reg(ES8311_ADDR, 0x0F, 0x00);
  write_reg(ES8311_ADDR, 0x0D, 0x01);
  write_reg(ES8311_ADDR, 0x0E, 0x20);

  write_reg(ES8311_ADDR, 0x2E, 0xC0);  // enable I2S output path
  // -----------------------------
  // DAC path on (so clocks run)
  // -----------------------------
  write_reg(ES8311_ADDR, 0x31, 0x00);  // DAC power
  write_reg(ES8311_ADDR, 0x32, 0x00);  // unmute

  // Headphone/spk not strictly needed for clocks, but harmless
  write_reg(ES8311_ADDR, 0x33, 0x20);
}


//================================================
void setup() {

  Serial.begin(115200);
  delay(500);

  Wire.begin(I2C_SDA, I2C_SCL);
  scan_bus();

  // ---------- I2S channel config ----------
  i2s_chan_config_t cfg =
      I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

  // 1) create channels
  ESP_ERROR_CHECK(i2s_new_channel(&cfg, &txh, &rxh));
  assert(rxh != NULL);
  assert(txh != NULL);

  // ---------- I2S standard config ----------
  i2s_std_config_t std = {
      .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
      .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
          I2S_DATA_BIT_WIDTH_16BIT,
          I2S_SLOT_MODE_STEREO),
      .gpio_cfg =
          {
              .mclk = (gpio_num_t)I2S_MCLK,
              .bclk = (gpio_num_t)I2S_BCLK,
              .ws = (gpio_num_t)I2S_LRCK,
              .dout = (gpio_num_t)I2S_DOUT,
              .din = (gpio_num_t)I2S_DIN,
          },
  };

  // expand 16-bit samples into 32-bit slots
  std.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;

  // generate 256 × fs master clock (12.288 MHz @ 48 kHz)
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  // 2) initialize standard mode
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(txh, &std));
  ESP_ERROR_CHECK(i2s_channel_init_std_mode(rxh, &std));

  // 3) (optional) explicit MCLK routing — use ONLY if your core supports it
  // comment this out if it doesn’t compile
//   ESP_ERROR_CHECK(i2s_mclk_gpio_config((gpio_num_t)I2S_MCLK));

  // 4) enable channels
  ESP_ERROR_CHECK(i2s_channel_enable(txh));
  ESP_ERROR_CHECK(i2s_channel_enable(rxh));

  // 5) run TX once to start clocks
  int32_t dummy[1024] = {0};
  size_t n;
  i2s_channel_write(txh, dummy, sizeof(dummy), &n, 100);

  // ---------- codec bring-up ----------
  //es8311_init();
  es7210_init();
  i2c_check_register(ES7210_ADDR);

  delay(2000);
  Serial.println("Speak into mic…");
}

//================================================
void loop() {
    int32_t buf[1024];   // stereo frame
    size_t bytes_read = 0;

    if (i2s_channel_read(rxh, buf, sizeof(buf), &bytes_read, 1000) == ESP_OK
        && bytes_read >= sizeof(buf)) {

        int16_t L = (int16_t)(buf[0] >> 16);
        int16_t R = (int16_t)(buf[1] >> 16);

        Serial.printf("L: %d | R: %d\n", L, R);
    }
}

