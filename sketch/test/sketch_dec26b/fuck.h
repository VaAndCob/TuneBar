/*
#include <Arduino.h>
#include <Wire.h>
#include <stdio.h>
#include "driver/i2s_std.h"


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
  Serial.printf("\n=== ES7210 CHECK I2C address 0x%02X ===\n",addr);
  uint8_t regs[] = { 0x0E,0x0F, 0x09, 0x10, 0x11, 0x12, 0x13, 0x0B, 0xFD };

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
  write_reg(ES7210_ADDR, 0x00, 0xFF);
  delay(10);
  write_reg(ES7210_ADDR, 0x00, 0x41);

  // power management
  //write_reg(ES7210_ADDR, 0x01, 0x3F);
  // exit shutdown
  write_reg(ES7210_ADDR, 0x01, 0x00);

  write_reg(ES7210_ADDR, 0x02, 0x40);
write_reg(ES7210_ADDR, 0x06, 0x00);
write_reg(ES7210_ADDR, 0x07, 0x02);

  write_reg(ES7210_ADDR, 0x08, 0x00);// I2S slave, 24-bit
  //write_reg(ES7210_ADDR, 0x08, 0x3F);   // master mode, MCLK/BCLK/LRCK generated

  // analog mic mode
  write_reg(ES7210_ADDR, 0x09, 0x30);
  write_reg(ES7210_ADDR, 0x0A, 0x00);

  

  write_reg(ES7210_ADDR, 0x0B, 0x32);// Philips I²S, 24-bit samples in 32-bit frame

  // --- absolutely disable TDM ---
  write_reg(ES7210_ADDR, 0x0E, 0x00);  // TDM control: off
  write_reg(ES7210_ADDR, 0x0F, 0x00);  // slot config reset
  write_reg(ES7210_ADDR, 0x0C, 0x00);  // select SDOUT1, normal I2S
  // enable both ADC1+ADC2
  write_reg(ES7210_ADDR, 0x10, 0x03);  // ADC1 + ADC2 enabled

  
 // write_reg(ES7210_ADDR, 0x11, 0x55);  // works only when ADC1 is enabled // map ADC2 → L/R
  
write_reg(ES7210_ADDR, 0x11, 0x10);  // L=ADC1, R=ADC1 // map ADC1 to L/R
write_reg(ES7210_ADDR, 0x12, 0x00);   // digital mute disable L
write_reg(ES7210_ADDR, 0x13, 0x00);   // digital mute disable R
write_reg(ES7210_ADDR, 0x14, 0x00);   // global mute disable if present

  // mic bias on
  write_reg(ES7210_ADDR, 0x40, 0x43);
  write_reg(ES7210_ADDR, 0x41, 0x78);

  // gains
  write_reg(ES7210_ADDR, 0x43, 0x1F);
  write_reg(ES7210_ADDR, 0x44, 0x1F);

  // POWER ON MIC1/2 analog front-ends ✔
  write_reg(ES7210_ADDR, 0x4B, 0x00);  // MIC1/2 ON
  write_reg(ES7210_ADDR, 0x4C, 0xFF);  // MIC3/4 OFF


}

void es8311_init()
{
  // Reset
  write_reg(ES8311_ADDR, 0x00, 0x1F);
  delay(10);
  write_reg(ES8311_ADDR, 0x00, 0x00);

  // Power up analog + digital
  write_reg(ES8311_ADDR, 0x01, 0x00);

  // -----------------------------
  // CLOCK & MASTER MODE – CRITICAL
  // -----------------------------
 //   write_reg(ES8311_ADDR, 0x02, 0x10);   // slave, I2S, 24-bit
  write_reg(ES8311_ADDR, 0x02, 0x30);   // MASTER, I2S, 24-bit
  write_reg(ES8311_ADDR, 0x03, 0x10);   // MCLK = 256Fs
  write_reg(ES8311_ADDR, 0x16, 0x00);   // Fs = 48kHz

  // Select MCLK as system clock source
  write_reg(ES8311_ADDR, 0x01, 0x00);   // ensure sysclk enabled

  // Enable PLL (required to drive BCLK/LRCK)
  write_reg(ES8311_ADDR, 0x0F, 0x00);
  write_reg(ES8311_ADDR, 0x0D, 0x01);
  write_reg(ES8311_ADDR, 0x0E, 0x20);

write_reg(ES8311_ADDR, 0x2E, 0xC0); // enable I2S output path
  // -----------------------------
  // DAC path on (so clocks run)
  // -----------------------------
  write_reg(ES8311_ADDR, 0x31, 0x00); // DAC power
  write_reg(ES8311_ADDR, 0x32, 0x00); // unmute

  // Headphone/spk not strictly needed for clocks, but harmless
  write_reg(ES8311_ADDR, 0x33, 0x20);
}


//================================================
void setup() {
   

  Serial.begin(115200);
  delay(500);
  Wire.begin(I2C_SDA, I2C_SCL);
  scan_bus();


i2s_chan_config_t cfg =
    I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_SLAVE);

// 🚨 make sure channel is actually allocated
ESP_ERROR_CHECK(  i2s_new_channel(&cfg, &txh, &rxh) );
assert(rxh != NULL);

i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
        I2S_DATA_BIT_WIDTH_24BIT,
        I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
        .mclk = (gpio_num_t)I2S_MCLK,
        .bclk = (gpio_num_t)I2S_BCLK,
        .ws   = (gpio_num_t)I2S_LRCK,
        .dout = I2S_GPIO_UNUSED,
        .din  = (gpio_num_t)I2S_DIN
    }
};

// 24-in-32 frame alignment
std.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  // after i2s setup

  // kick the clock generator with a dummy write
  int32_t dummy[8] = { 0 };
  size_t n;
  i2s_channel_write(txh, dummy, sizeof(dummy), &n, 100);
  



/*


  i2s_new_channel(&cfg, &txh, &rxh);

  i2s_std_config_t std = {
    .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(48000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(
      I2S_DATA_BIT_WIDTH_24BIT,
      I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)I2S_MCLK,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws = (gpio_num_t)I2S_LRCK,
      .dout = I2S_GPIO_UNUSED,
      .din = (gpio_num_t)I2S_DIN }
  };

  std.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_32BIT;
  std.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

  i2s_channel_init_std_mode(txh, &std);
  i2s_channel_init_std_mode(rxh, &std);
  i2s_channel_enable(txh);
  i2s_channel_enable(rxh);
  // after i2s setup



  es8311_init(); 

  delay(100);
  // 🚨 THIS was missing in your code
ESP_ERROR_CHECK( i2s_channel_init_std_mode(rxh, &std) );
ESP_ERROR_CHECK( i2s_channel_init_std_mode(txh, &std) );

// enable only after init
ESP_ERROR_CHECK( i2s_channel_enable(rxh) );
ESP_ERROR_CHECK( i2s_channel_enable(txh) );


  es7210_init();
  i2c_check_register(ES7210_ADDR);

 

  delay(3000);
  Serial.println("Speak into mic…");
}
//================================================
void loop() {
size_t n;
/*
static int32_t silence[64] = {0};

i2s_channel_write(txh, silence, sizeof(silence), &n, 20);

 
  int32_t buf[64];


  i2s_channel_read(rxh, buf, sizeof(buf), &n, 100);

  if (!n) return;

  // look ONLY at first 32-bit word
  int32_t s = buf[0];

  // try multiple interpretations
  Serial.print("raw= ");
  Serial.print(s);

  Serial.print("  >>8= ");
  Serial.print(s >> 8);

  Serial.print("  >>16= ");
  Serial.print(s >> 16);

  Serial.print("  24bit= ");
  Serial.println((s << 8) >> 8);
}
*/