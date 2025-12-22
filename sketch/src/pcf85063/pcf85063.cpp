//==============  REAT TIME CLOCK =====================
#include "pcf85063.h"
#include "user_config.h"
#include "../i2c_bsp/i2c_bsp.h"
#include <Arduino.h>

#include "time.h"
#define UTC_OFFSET_DST 0
#define NTP_SERVER "pool.ntp.org"


uint8_t infoPageIndex = 0; // for info panel page index
RTC_DateTime now;

uint8_t PCF85063::decToBcd(uint8_t val) {
  return ((val / 10 * 16) + (val % 10));
}

// Convert binary coded decimal to normal decimal numbers
uint8_t PCF85063::bcdToDec(uint8_t val) {
  return ((val / 16 * 10) + (val % 16));
}

void PCF85063::begin() {

  if(cap_sel(CAP_SEL_12_5PF))
    log_i("PCF85063 OK");
  else 
    log_e("PCF85063 Failed"); // CAP_SEL bit setting 12.5pF
}
/*Function: The clock timing will start */
void PCF85063::startClock(void) {
  uint8_t data = ReadReg(0x00);
  data &= ~0x20;
  WriteReg(0x00, data);
}

/*Function: The clock timing will stop */
void PCF85063::stopClock(void) {
  uint8_t data = ReadReg(0x00);
  data |= 0x20;
  WriteReg(0x00, data);
}

/****************************************************************/
/*Function: Read time and date from RTC  */
bool PCF85063::getDateTime() {
  uint8_t raw[7];
  // Read 7 bytes starting at register 0x04
  if (i2c_read_buff(rtc_dev_handle, 0x04, raw, 7) != ESP_OK) {
    return false; // handle error if needed
  }
  now.second = bcdToDec(raw[0] & 0x7F);
  now.minute = bcdToDec(raw[1]);
  now.hour = bcdToDec(raw[2] & 0x3F);
  now.dayOfMonth = bcdToDec(raw[3]);
  now.dayOfWeek = bcdToDec(raw[4]);
  now.month = bcdToDec(raw[5]);
  now.year = bcdToDec(raw[6]);
  return true;
}

/*******************************************************************/
/*Frunction: Write the time that includes the date to the RTC chip */
void PCF85063::setDateTime(uint16_t year, uint8_t month, uint8_t dayOfMonth, uint8_t hour, uint8_t minute, uint8_t second, uint8_t dayOfWeek) {
  uint8_t yr = (year >= 2000) ? (year - 2000) : year;  // convert 2025 → 25
  WriteReg(REG_SEC, decToBcd(second)); // 0 to bit 7 starts the clock, bit 8 is OS reg
  WriteReg(REG_MIN, decToBcd(minute));
  WriteReg(REG_HOUR, decToBcd(hour)); // If you want 12 hour am/pm you need to set bit 6
  WriteReg(REG_DAY_MONTH, decToBcd(dayOfMonth));
  WriteReg(REG_DAY_WEEK, decToBcd(dayOfWeek));
  WriteReg(REG_MON, decToBcd(month));
  WriteReg(REG_YEAR, decToBcd(yr));
}
void PCF85063::fillByHMS(uint8_t _hour, uint8_t _minute, uint8_t _second) {
  // assign variables
  now.hour = _hour;
  now.minute = _minute;
  now.second = _second;
}
void PCF85063::fillByYMD(uint16_t _year, uint8_t _month, uint8_t _day) {
  now.year = _year - 2000;
  now.month = _month;
  now.dayOfMonth = _day;
}
void PCF85063::fillDayOfWeek(uint8_t _dow) {
  now.dayOfWeek = _dow;
}

void PCF85063::reset() {
    WriteReg(REG_CTRL1, decToBcd(0x00));
}

/*
    @brief  clock calibration setting
    @Parameter:
     mode = 0, calibrate every 2 hours
     mode = 1, calibrate every 4 minutes
     offset_sec, offset value of one second.
     If the RTC time too fast: offset_sec < 0
     If the RTC time too slow: offset_sec > 0
*/
uint8_t PCF85063::calibratBySeconds(int mode, float offset_sec) {
  float Fmeas = 32768.0 + offset_sec * 32768.0;
  setcalibration(mode, Fmeas);
  return readCalibrationReg();
}
/*
    @brief: Clock calibrate setting
    @parm:
          mode: calibration cycle, mode 0 -> every 2 hours, mode 1 -> every 4 minutes
          Fmeas: Real frequency you detect
*/

void PCF85063::setcalibration(int mode, float Fmeas) {
  float offset = 0;
  float Tmeas = 1.0 / Fmeas;
  float Dmeas = 1.0 / 32768 - Tmeas;
  float Eppm = 1000000.0 * Dmeas / Tmeas;
  if (mode == 0) {
    offset = Eppm / 4.34;
  } else if (mode == 1) {
    offset = Eppm / 4.069;
  }

  uint8_t data = ((mode << 7) & 0x80) | ((int)(offset + 0.5) & 0x7f);
  WriteReg(PCF85063_OFFSET, data);
}

uint8_t PCF85063::readCalibrationReg(void) {
  return ReadReg(PCF85063_OFFSET);
}

void PCF85063::setRam(uint8_t value) {
  WriteReg(REG_RAM, value);
}

uint8_t PCF85063::readRamReg(void) {
  return ReadReg(REG_RAM);
}

/*
    @brief: internal oscillator capacitor selection for
           quartz crystals with a corresponding load capacitance
    @parm:
         value(0 or 1): 0 - 7 pF
                        1 - 12.5 pF
    @return: value of CAP_SEL bit
*/
uint8_t PCF85063::cap_sel(uint8_t value) {
  uint8_t control_1 = ReadReg(REG_CTRL1);
  control_1 = (control_1 & 0xFE) | (0x01 & value);
  WriteReg(REG_CTRL1, control_1);

  return ReadReg(REG_CTRL1) & 0x01;
}

#ifdef PCF85063_USE_STRINGS
void PCF85063::timeOrDateStringsToInts(const char time_or_date[], int time_or_date_parts[]) {
  int time_or_date_index = 0;
  char mutable_time_or_date[12];
  strcpy(mutable_time_or_date, time_or_date);
  char *token;

  token = strtok(mutable_time_or_date, " :");
  while (token != NULL) {
    char *token_end;
    int value = strtol(token, &token_end, 10);
    if (token == token_end) {
      // If this occurs, there was no number to be parsed. Assume month.
      if (strncmp(token, "Jan", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 1;
      } else if (strncmp(token, "Feb", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 2;
      } else if (strncmp(token, "Mar", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 3;
      } else if (strncmp(token, "Apr", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 4;
      } else if (strncmp(token, "May", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 5;
      } else if (strncmp(token, "Jun", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 6;
      } else if (strncmp(token, "Jul", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 7;
      } else if (strncmp(token, "Aug", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 8;
      } else if (strncmp(token, "Sep", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 9;
      } else if (strncmp(token, "Oct", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 10;
      } else if (strncmp(token, "Nov", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 11;
      } else if (strncmp(token, "Dec", 3) == 0) {
        time_or_date_parts[time_or_date_index++] = 12;
      }
    } else {
      time_or_date_parts[time_or_date_index++] = value;
    }
    token = strtok(NULL, " :");
  }
}

void PCF85063::fillDateByString(const char date_string[]) {
  int date_parts[3] = {0, 0, 0};
  timeOrDateStringsToInts(date_string, date_parts);
  fillByYMD(date_parts[2], date_parts[0], date_parts[1]);
}

void PCF85063::fillTimeByString(const char time_string[]) {
  int time_parts[3] = {0, 0, 0};
  timeOrDateStringsToInts(time_string, time_parts);
  fillByHMS(time_parts[0], time_parts[1], time_parts[2]);
}
#endif

bool PCF85063::WriteReg(uint8_t reg, uint8_t val) {
  return i2c_write_buff(rtc_dev_handle, reg, &val, 1) == ESP_OK;
}

uint8_t PCF85063::ReadReg(uint8_t reg) {
  uint8_t val = 0u;
  i2c_read_buff(rtc_dev_handle, reg, &val, 1);
  return val;
}

//sync ntp server to rtc
void PCF85063::ntp_sync(int8_t utc_offset_hour,int8_t utc_offset_min) {
  stopClock();
  int32_t UTC_OFFSET = 0;
  if (utc_offset_hour < -12 || utc_offset_hour > 14) {
    UTC_OFFSET = 0;
  } else {
    UTC_OFFSET = (3600 * utc_offset_hour) + (60 * utc_offset_min);
  }
  
  configTime(UTC_OFFSET, 0, NTP_SERVER);
  delay(200);

  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    setDateTime(
      timeinfo.tm_year - 100,     // YY
      timeinfo.tm_mon + 1,        // 1-12
      timeinfo.tm_mday,
      timeinfo.tm_hour,
      timeinfo.tm_min,
      timeinfo.tm_sec,
      timeinfo.tm_wday            // 0-6
    );
    char buf[40];
    strftime(buf, sizeof(buf), "%a, %b %d %Y %H:%M:%S", &timeinfo);
    log_i("NPT Synced time: %s", buf);
  } else {
    log_w("Unable to setup RTC!");
  }
    startClock();
}

//-------------- helper function for RTC--------------------------------
const char *monthNames[] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun","Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};
const char *weekdayNames[] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};


