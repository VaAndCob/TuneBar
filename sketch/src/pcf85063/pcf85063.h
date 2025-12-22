//==============  REAT TIME CLOCK =====================
#pragma once

#include <Arduino.h>

//#define PCF85063_I2C_ADDRESS 0x51
#define PCF85063_OFFSET 0x02
#define REG_CTRL1 0x00
#define REG_CTRL1_STOP 0x20
#define REG_CTRL2 0x01
#define REG_OFFSET 0x02
#define REG_RAM 0x03
#define REG_SEC 0x04
#define REG_SEC_OS 0x80
#define REG_MIN 0x05
#define REG_HOUR 0x06
#define REG_DAY_MONTH 0x07
#define REG_DAY_WEEK 0x08
#define REG_MON 0x09
#define REG_YEAR 0x0A
#define CAP_SEL_7PF 0
#define CAP_SEL_12_5PF 1

struct RTC_DateTime {
  uint8_t second;
  uint8_t minute;
  uint8_t hour;
  uint8_t dayOfWeek; // day of week, 1 = Monday
  uint8_t dayOfMonth;
  uint8_t month;
  uint16_t year;
};

extern uint8_t infoPageIndex;

extern RTC_DateTime now;
extern const char *monthNames[];
extern const char *weekdayNames[];

class PCF85063 {

public:
  void begin();
  void startClock(void);
  void stopClock(void);
  void setDateTime(uint16_t year, uint8_t month, uint8_t dayOfMonth, uint8_t hour, uint8_t minute, uint8_t second, uint8_t dayOfWeek);
  bool getDateTime(void);
  void setcalibration(int mode, float Fmeas);
  void setRam(uint8_t _value);
  uint8_t calibratBySeconds(int mode, float offset_sec);
  uint8_t readCalibrationReg(void);
  uint8_t readRamReg(void);
  uint8_t cap_sel(uint8_t value);
  void reset();
  void fillByHMS(uint8_t _hour, uint8_t _minute, uint8_t _second);
  void fillByYMD(uint16_t _year, uint8_t _month, uint8_t _day);
  void fillDayOfWeek(uint8_t _dow);
#ifdef PCF85063_USE_STRINGS
  void fillDateByString(const char date_string[] = __DATE__);
  void fillTimeByString(const char time_string[] = __TIME__);
#endif

  void ntp_sync(int8_t utc_offset_hour,int8_t utc_offset_min);


private:
  uint8_t decToBcd(uint8_t val);
  uint8_t bcdToDec(uint8_t val);

  bool WriteReg(uint8_t reg, uint8_t val);
  uint8_t ReadReg(uint8_t reg);
#ifdef PCF85063_USE_STRINGS
  void timeOrDateStringsToInts(const char time_or_date[], int parts[]);
#endif
};
// The typedef is because early versions of the library called it PCD85063TP
// instead of PCF85063. This allows both class names to work.
typedef PCF85063 PCD85063;
