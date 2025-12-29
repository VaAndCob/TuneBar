#ifndef USER_CONFIG_H
#define USER_CONFIG_H

//spi & i2c handle
#define LCD_HOST SPI3_HOST

// touch I2C port
#define Touch_SCL_NUM (GPIO_NUM_18)
#define Touch_SDA_NUM (GPIO_NUM_17)

// touch esp
#define ESP_SCL_NUM (GPIO_NUM_48)
#define ESP_SDA_NUM (GPIO_NUM_47)

//  DISP
#define WAVESHARE_349_PIN_NUM_LCD_CS     (GPIO_NUM_9) 
#define WAVESHARE_349_PIN_NUM_LCD_PCLK   (GPIO_NUM_10)
#define WAVESHARE_349_PIN_NUM_LCD_DATA0  (GPIO_NUM_11)
#define WAVESHARE_349_PIN_NUM_LCD_DATA1  (GPIO_NUM_12)
#define WAVESHARE_349_PIN_NUM_LCD_DATA2  (GPIO_NUM_13)
#define WAVESHARE_349_PIN_NUM_LCD_DATA3  (GPIO_NUM_14)
#define WAVESHARE_349_PIN_NUM_LCD_RST    (GPIO_NUM_21)
#define WAVESHARE_349_PIN_NUM_BK_LIGHT   (GPIO_NUM_8) 


#define I2C_TOUCH_ADDR                    0x3b
#define WAVESHARE_349_PIN_NUM_TOUCH_RST         (-1)
#define WAVESHARE_349_PIN_NUM_TOUCH_INT         (-1)


#define WAVESHARE_349_LVGL_TICK_PERIOD_MS   1
#define WAVESHARE_349_LVGL_TASK_MAX_DELAY_MS 30
#define WAVESHARE_349_LVGL_TASK_MIN_DELAY_MS 10 //less is more responsive



/*ADDR*/
#define WAVESHARE_349_RTC_ADDR 0x51 //PCf85063
#define WAVESHARE_349_IMU_ADDR 0x6b //QMI8658
#define WAVESHARE_349_CODEC_ADDR 0x18  //ES8311
#define WAVESHARE_349_EXPANDER_ADDR 0x20 //TCA9554
#define WAVESHARE_349_ADC_ADDR 0x40   //ES7210




#define USER_DISP_ROT_90    0
#define USER_DISP_ROT_NONO  0
#define Rotated USER_DISP_ROT_NONO   //软件实现旋转


#if (Rotated != USER_DISP_ROT_NONO)
#define WAVESHARE_349_LCD_H_RES 172   
#define WAVESHARE_349_LCD_V_RES 640
#else
#define WAVESHARE_349_LCD_H_RES 640   
#define WAVESHARE_349_LCD_V_RES 172
#endif

#define LCD_NOROT_HRES     172
#define LCD_NOROT_VRES     640
#define LVGL_DMA_BUFF_LEN (LCD_NOROT_HRES * 64 * 2)
#define LVGL_SPIRAM_BUFF_LEN (WAVESHARE_349_LCD_H_RES * WAVESHARE_349_LCD_V_RES * 2)



#define I2S_DSIN 6 // data in from (es7210)
#define I2S_DSOUT 45  // data out to (es8311)
#define I2S_BCLK 15  // SCLK
#define I2S_MCLK 7
#define I2S_LRC 46  // LRCK
/*
Audio I2S pins
ES8322 → Audio DAC / playback
ES7210 → Audio ADC / microphone
They are on the SAME physical I2S bus
There is one master (ESP32), and:
ESP32 TX → ES8322 (DAC)
ESP32 RX ← ES7210 (ADC)
*/

//SD CARD
#define SD_CS 38
#define SPI_MOSI 39
#define SPI_MISO 40
#define SPI_SCK 41

//ADC
#define ADC_BATT 4 

//PIN
#define SYS_OUT 16
#define BOOT 0
#define LCD_BL 8

#endif