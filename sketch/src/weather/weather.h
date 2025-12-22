//==============  WEATHER & POLLUTION =====================
#pragma once

#include <Arduino.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))


extern String query_parameter;
extern const int8_t UTC_offset_hour[];
extern const int8_t UTC_offset_minute[];
extern uint8_t temp_unit;// 0 = celcius ,1 = farenheit
extern uint8_t offset_hour_index;
extern uint8_t offset_minute_index;



void updateWeatherPanel();
void weatherAnimation(bool animate);