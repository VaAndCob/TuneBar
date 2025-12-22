/**
 * @details
 *  - **Application Name:** ESP32S3 TuneBar  
 *  - **Developed By:** Va&Cob  
 *
 *  **Software Stack**  
 *  - PlatformIO pioArduino  
 *  - ESP32 Core 3.3.3  
 *  - LVGL 8.4.0  
 *  - Display Framework: esp32_display_panel (including required dependencies)  
 *  - ESP32 Audio I2S 3.4.3 https://github.com/schreibfaul1/ESP32-audioI2S
 *  
 * Target Hardware
 *  - **Board Model:** Waveshare ESP32-S3-Touch-LCD-3.49  
 *     (3.49″ IPS capacitive touch, 172 × 640 resolution, QSPI display interface)  
 *     Reference: https://www.waveshare.com/product/arduino/boards-kits/esp32-s3/esp32-s3-touch-lcd-3.49.htm?sku=32374  
 *
 *  - **Flash Size:** 16 MB  
    - **Parition -> Custom 16M Flash (6MB APP/3.9MB SPIFFS)
 *  - **PSRAM:** OPI PSRAM (enabled)  
 *  - **USB CDC on Boot:** Enabled  
 * !!! IMPORTANT !!!  user must upload files in folder "data" to LittleFS parition
 *
 * This documentation block provides an authoritative configuration reference
 * to ensure alignment across development, testing, and system integration workflows.
 */

#include <Arduino.h>
#include <LittleFS.h>
#include "user_config.h"
#include "lvgl_port/lvgl_port.h"
#include "esp_err.h"
#include "i2c_bsp/i2c_bsp.h"
#include "lcd_bl_bsp/lcd_bl_pwm_bsp.h"
   
#include "es8311/es8311.h"      // audio codec
#include "es7210/es7210.h"      // mic 
#include "tca9554/tca9554.h"    // io expander
#include "pcf85063/pcf85063.h"  //rtc
#include "ui/ui.h"
#include "file/file.h"
#include "weather/weather.h"
#include "ESP32-audioI2S-master/Audio.h"
extern Audio audio;


ES8311 speaker;  //ES8322 (DAC)  →  I2S_NUM_0  (TX)
ES7210 mic;   //ES7210 (ADC)  →  I2S_NUM_1  (RX)
bool is_mic_mode = false; // แฟล็กคุมโหมด
TaskHandle_t micTaskHandle = NULL; // สำหรับเก็บอ้างอิง Task

//expander
extern i2c_master_dev_handle_t tca9554_dev_handle;
TCA9554 *io = nullptr;

#include "xtask.h"  //all tasks

//rtos message que handle
QueueHandle_t ui_status_queue = NULL;
QueueHandle_t audio_cmd_queue = NULL;

//############################################################
void setup() {
  
  //message que init
  ui_status_queue = xQueueCreate(20, sizeof(UIStatusPayload));
  assert(ui_status_queue != NULL);
  audio_cmd_queue = xQueueCreate(20, sizeof(AudioCommandPayload));
  assert(audio_cmd_queue != NULL);

  randomSeed(esp_random());
  Serial.begin(115200);
  delay(100);
  log_i("🎧🕺💃👯💫[TuneBar] by Va&Cob👯🕺💃🎧");

  //input pin
  pinMode(BOOT, INPUT_PULLUP);     //volume down
  pinMode(SYS_OUT, INPUT_PULLUP);  //volume up

  //setup ADC
  analogReadResolution(12);        // 9–13 bits supported
  analogSetAttenuation(ADC_11db);  // Full-scale ~3.3V

  i2c_master_Init();  // init i2c

  //exapnder init
  io = new TCA9554(tca9554_dev_handle);
  io->begin();
  io->setPinMode(EXIO6_BIT, 0);  //set output mode

  // turn on power button
  if (digitalRead(SYS_OUT) == LOW) {
    log_d("< POWER ON >");
    io->digitalWrite(EXIO6_BIT, 1);  //hold turn on
  }

  //init lvgl
  lvgl_port_init();
  lcd_bl_pwm_bsp_init(LCD_PWM_MODE_255);  //max out the brightness

  //power amp control
  io->setPinMode(EXIO7_BIT, 0);  // 0 = OUTPUT
  delay(100);
  io->digitalWrite(EXIO7_BIT, 1);  //enable amp
  delay(100);
  if (io->digitalRead(EXIO7_BIT) == 0)
    log_e("Power Amp not turn on!");
  else
    log_i("Power Amp -> ON");

  //audio library
  Audio::audio_info_callback = my_audio_info;
  audio.setAudioTaskCore(1);  //audio default run on core 1 (lvgl run on core 0 in lvgl_port.c)
  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DSIN, I2S_MCLK, I2S_DSOUT);
  audio.forceMono(true);
  audio.setConnectionTimeout(2000, 4000);  //connection timeout ms, ms_ssl
  // audio.setVolume(audio_volume);  // default 0...21

  //es8311 audio codec
  if (!speaker.begin()) log_e("ES8311 begin failed");
  else log_i("ES8311 OK");
  speaker.setVolume(80);  //80 is best max
  speaker.setBitsPerSample(16);


if (mic.begin()) {
 if (mic.start()) {
    log_i("ES7210 detected and configured");
  } else {
    log_e("ES7210 not detected");
  }
}
  
  //Free RTOS Task
  xTaskCreatePinnedToCore(audio_loop_task, "audio_loop", 4 * 1024, NULL, 4, NULL, 1);
  xTaskCreatePinnedToCore(rtc_read_task, "getDateTimeTask", 3 * 1024, NULL, 3, NULL, 1);
  xTaskCreatePinnedToCore(button_input_task, "buttonInputTask", 3 * 1024, NULL, 2, NULL, 1);
  xTaskCreatePinnedToCore(batt_level_read_task, "readBatteryLevel", 3 * 1024, NULL, 1, NULL, 1);

 //  xTaskCreatePinnedToCore(mic_capture_task, "MicTask", 4096, NULL, 1, &micTaskHandle, 1);
  
}
//############################################################
void loop() {


}
//---------------------------------------------------