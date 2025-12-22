#include "weather.h"
#include "network/network.h"
#include "ui/ui.h"
#include <Arduino.h>

#include "pcf85063/pcf85063.h"
#include <ArduinoJson.h>

const char *weatherUrl = "http://api.weatherapi.com/v1/current.json?key=%s&q=%s&aqi=yes";
//"https://api.weatherapi.com/v1/current.json?key=%s&q=%s&aqi=yes"; for SSL
const char *weatherApiKey = "1f00c88f3483483a9ba62101250612";

String query_parameter = "auto:ip"; // auto detect by IP , or city name "Bangkok", or "lat,long" 13.6499579,100.4103726
const int8_t UTC_offset_hour[27] = {14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0, -1, -2, -3, -4, -5, -6, -7, -8, -9, -10, -11, -12};
const int8_t UTC_offset_minute[7] = {45, 30, 15, 0, -15, -30, -45};
uint8_t temp_unit = 0; // unit index  0=celcius, 1 = farenheit
uint8_t city_index = 0;
uint8_t offset_hour_index = 0;
uint8_t offset_minute_index = 0;

static TaskHandle_t weatherTask = NULL;

#define MAX_URL_LENGTH 256 // กำหนดขนาดบัฟเฟอร์สูงสุดที่ปลอดภัย

struct Weather_JSON { // all aqi data from json
  String state;
  uint8_t usepa_index;
  const char *name;
  String last_updated;

  float temp_c;
  float temp_f;
  float wind_kph;
  float wind_mph;
  float wind_degree;
  String wind_dir;
  float pressure_mb;
  float pressure_in;
  float precip_mm;
  float precip_in;
  float humidity;
  float cloud;
  float feelslike_c;
  float feelslike_f;
  float uv;
  uint16_t code;
  uint8_t is_day;

  // AQI
  float co;
  float no2;
  float o3;
  float so2;
  float pm2_5;
  float pm10;
};

//==================== Weather condition ====================
#define SKY_DAY_CLEAR 0x41b3fd
#define SKY_NIGHT_CLEAR 0x0c1419
#define SKY_RAIN 0x132D3E
#define SKY_THUNDERSTORM 0x132D3E
#define SKY_SNOW 0x487593
#define SKY_FOG 0x8f9091
#define SKY_CLOUDY 0x8f9091
#define LAND_DAY_CLEAR 0x228322
#define LAND_NIGHT_CLEAR 0x103a10
#define LAND_SNOW 0xf0f0f0

//---------- set weather panel background sky and land color by weather code
void setWeatherPanelBgColor(int code, bool isNight) {
  int sky_color = 0;
  int land_color = 0;

  // --- Clear / Sunny ---
  if (code == 1000) {
    if (isNight) {
      sky_color = SKY_NIGHT_CLEAR;
      land_color = LAND_NIGHT_CLEAR;
    } else {
      sky_color = SKY_DAY_CLEAR;
      land_color = LAND_DAY_CLEAR;
    }
  }
  // --- Thunderstorm ---
  if (code == 1087 || code == 1273 || code == 1276 || code == 1279 || code == 1282) {
    sky_color = SKY_THUNDERSTORM;
    if (isNight)
      land_color = LAND_NIGHT_CLEAR;
    else
      land_color = LAND_DAY_CLEAR;
  }
  // --- Rain ---
  if ((code >= 1150 && code <= 1246) || code == 1063 || code == 1171 || code == 1180 || code == 1183 || code == 1186 || code == 1189 || code == 1192 || code == 1195 || code == 1198 || code == 1201) {
    sky_color = SKY_RAIN;
    if (isNight)
      land_color = LAND_NIGHT_CLEAR;
    else
      land_color = LAND_DAY_CLEAR;
  }
  // --- Snow / Sleet / Ice Pellets ---
  if ((code >= 1066 && code <= 1072) || (code >= 1114 && code <= 1237) || (code >= 1249 && code <= 1264)) {
    land_color = LAND_SNOW;
    sky_color = SKY_SNOW;
  }
  // --- Fog / Mist / Freezing Fog ---
  if (code == 1030 || code == 1135 || code == 1147) {
    sky_color = SKY_FOG;
    if (isNight)
      land_color = LAND_NIGHT_CLEAR;
    else
      land_color = LAND_DAY_CLEAR;
  }
  // --- Mostly Cloudy / Partly Cloudy ---
  if (code == 1003 || code == 1006 || code == 1009) {
    sky_color = SKY_CLOUDY;
    if (isNight)
      land_color = LAND_NIGHT_CLEAR;
    else
      land_color = LAND_DAY_CLEAR;
  }
  // set weather background
  lv_obj_set_style_bg_color(ui_Info_Panel_Weather, lv_color_hex(sky_color), LV_PART_MAIN);
  lv_obj_set_style_bg_grad_color(ui_Info_Panel_Weather, lv_color_hex(land_color), LV_PART_MAIN);
}

//----------  get weather icon image from weather code
const lv_img_dsc_t *getWeatherIconImage(int code, bool isNight) {
  // --- Clear / Sunny ---
  if (code == 1000) return isNight ? &ui_img_weather_clearnight_png : &ui_img_weather_sunny_png;

  // --- Thunderstorm (ต้องตรวจสอบก่อน Rain) ---
  // 1087, 1273, 1276 (Patchy light/moderate/heavy rain with thunder)
  if (code == 1087 || (code >= 1273 && code <= 1276)) return &ui_img_weather_thunderstorm_png;

  // --- Snow / Sleet / Ice Pellets / Hail ---
  // โค้ดทั้งหมดที่เกี่ยวข้องกับหิมะ/น้ำแข็ง
  if (code == 1066 || code == 1069 || code == 1072 || // Sleet/Snow
      (code >= 1114 && code <= 1117) || // Blizzard/Snow
      (code >= 1168 && code <= 1171) || // Sleet
      (code >= 1198 && code <= 1201) || // Sleet/Snow showers
      (code >= 1210 && code <= 1225) || // Snow (all types)
      code == 1237 || // Ice pellets
      (code >= 1249 && code <= 1264) || // Light/Moderate/Heavy Sleet/Snow/Hail showers
      code == 1279 || code == 1282) // Snow with Thunder
    return &ui_img_weather_snow_png;

  // --- Rain ---
  // 1063, 1150, 1153, 1180-1195, 1240-1246 (Rain and Drizzle)
  if (code == 1063 || (code >= 1150 && code <= 1153) || (code >= 1180 && code <= 1195) || (code >= 1240 && code <= 1246)) return &ui_img_weather_rain_png;

  // --- Fog / Mist / Freezing Fog ---
  // 1030, 1135, 1147
  if (code == 1030 || code == 1135 || code == 1147) return &ui_img_weather_windy_png; // หรือใช้ &ui_img_weather_fog_png หากมี

  // --- Mostly Cloudy / Partly Cloudy ---
  if (code == 1003 || code == 1006) return isNight ? &ui_img_weather_cloudynight_png : &ui_img_weather_partlycloud_png;

  // --- Overcast (แยกออกมาจาก Mostly/Partly Cloudy) ---
  if (code == 1009) return &ui_img_weather_cloudy_png; // Overcast มักไม่เปลี่ยนตามกลางวัน/กลางคืน

  // Fallback — safe default (ใช้ Overcast)
  return &ui_img_weather_cloudy_png;
}
//----------  get home icon image from weather code
const lv_img_dsc_t *getHomeIconImage(int code, bool isNight, int current_month) {
  // *** หมายเหตุ: ต้องส่งค่า 'current_month' (1-12) เข้ามาในฟังก์ชันด้วย ***

  // 1. --- Snow / Sleet / Hail / Freezing Rain / Ice Pellets / Thunder-Snow ---
  // (โค้ดกลุ่ม Snow/Ice/Hail เหมือนเดิม เพราะสำคัญที่สุด)
  if (code == 1066 || code == 1069 || code == 1072 || (code >= 1114 && code <= 1117) || (code >= 1168 && code <= 1171) || (code >= 1198 && code <= 1201) || (code >= 1210 && code <= 1225) || code == 1237 || (code >= 1249 && code <= 1264) || code == 1279 || code == 1282) {
    return &ui_img_weather_home_snow_png;
  }

  // 2. --- Thunderstorm / Rain / Drizzle ---
  // (โค้ดกลุ่ม Rain/Thunderstorm เหมือนเดิม)
  if (code == 1087 || (code >= 1273 && code <= 1276) || code == 1063 || (code >= 1150 && code <= 1153) || (code >= 1180 && code <= 1195) || (code >= 1240 && code <= 1246)) {
    return &ui_img_weather_home_rain_png;
  }

  // 3. --- Fall / Spring Logic (สำหรับสภาพอากาศที่ไม่รุนแรง) ---
  // เราจะแสดงไอคอน Fall/Spring เฉพาะในกรณีที่สภาพอากาศเป็น Clear/Cloudy/Fog เท่านั้น

  // ตรวจสอบว่าโค้ดที่เหลืออยู่เป็น Clear/Cloudy/Mist/Fog หรือไม่
  // Codes: 1000, 1003, 1006, 1009, 1030, 1135, 1147
  if (code == 1000 || (code >= 1003 && code <= 1009) || code == 1030 || code == 1135 || code == 1147) {

    // ตรวจสอบว่าเป็นฤดูใบไม้ร่วง (Fall) หรือไม่
    if (current_month == 9 || current_month == 10 || current_month == 11) {
      // หากเป็นฤดูใบไม้ร่วง และสภาพอากาศไม่รุนแรง ให้แสดงไอคอน Fall
      return &ui_img_weather_home_fall_png;
    }

    // ตรวจสอบว่าเป็นฤดูใบไม้ผลิ (Spring) หรือไม่ (ตัวอย่าง: เดือน 3, 4, 5)
    // หากคุณมีไอคอน Spring (เช่น ui_img_weather_home_spring_png) ก็สามารถเพิ่มเงื่อนไขตรงนี้ได้
    // if (current_month == 3 || current_month == 4 || current_month == 5) {
    //     return &ui_img_weather_home_spring_png;
    // }
  }

  // 4. --- Clear / Sunny / Cloudy / Mist / Fog (Default) ---
  // หากไม่เข้าเงื่อนไข Fall/Spring หรือไม่รู้จักโค้ด ให้แสดง Sunny เป็นค่าตั้งต้น
  return &ui_img_weather_home_sunny_png;
}
//==================== Pollution ========================
// 1. DATA STRUCTURES & CONSTANTS
const int us_epa_index_colors[6] = {
    0x00E400, // Good
    0xFFD700, // Moderate
    0xFF7E00, // Unhealthy for Sensitive Groups
    0xff0000, // Unhealthy
    0x8F3F97, // Very Unhealthy
    0x7E0023 // Hazardous
};
const char *us_epa_index_names[6] = {"Good", "Moderate", "Unhealthy for Sensitive Groups", "Unhealthy", "Very Unhealthy", "Hazardous"};
const lv_img_dsc_t us_epa_index_icon[6] = { // image 0 - 9
    ui_img_aqi_good_png, ui_img_aqi_moderate_png, ui_img_aqi_sensitive_png, ui_img_aqi_unhealthy_png, ui_img_aqi_very_unhealthy_png, ui_img_aqi_hazardous_png};

struct AQI_Breakpoint {
  int I_Lo; // AQI Low (e.g., 51)
  int I_Hi; // AQI High (e.g., 100)
  float C_Lo; // Concentration Low (e.g., 12.1)
  float C_Hi; // Concentration High (e.g., 35.4)
};
struct AirQualityData {
  float co; // ug/m3
  float no2; // ug/m3
  float o3; // ug/m3
  float so2; // ug/m3
  float pm2_5; // ug/m3
  float pm10; // ug/m3
};

// ข้อมูล Breakpoints ของ US EPA (สำหรับ PM2.5)
const AQI_Breakpoint pm25_breakpoints[] = {{0, 50, 0.0, 12.0}, {51, 100, 12.1, 35.4}, {101, 150, 35.5, 55.4}, {151, 200, 55.5, 150.4}, {201, 300, 150.5, 250.4}, {301, 500, 250.5, 500.4}};
const int NUM_PM25_BP = 6;

// ข้อมูล Breakpoints ของ US EPA (สำหรับ PM10)
const AQI_Breakpoint pm10_breakpoints[] = {{0, 50, 0.0, 54.0}, {51, 100, 55.0, 154.0}, {101, 150, 155.0, 254.0}, {151, 200, 255.0, 354.0}, {201, 300, 355.0, 424.0}, {301, 500, 425.0, 604.0}};
const int NUM_PM10_BP = 6;

// ข้อมูล Breakpoints ของ US EPA (สำหรับ O3 - 8 Hour Average, หน่วย ppm)
const AQI_Breakpoint o3_8hr_breakpoints[] = {
    {0, 50, 0.000, 0.059}, {51, 100, 0.060, 0.075}, {101, 150, 0.076, 0.095}, {151, 200, 0.096, 0.115}, {201, 300, 0.116, 0.374},
    // Note: AQI > 300 ใช้อ้างอิง O3 1-hr แทน
};
const int NUM_O3_8HR_BP = 5;

// ข้อมูล Breakpoints ของ US EPA (สำหรับ CO - 8 Hour Average, หน่วย ppm)
const AQI_Breakpoint co_8hr_breakpoints[] = {{0, 50, 0.0, 4.4}, {51, 100, 4.5, 9.4}, {101, 150, 9.5, 12.4}, {151, 200, 12.5, 15.4}, {201, 300, 15.5, 30.4}, {301, 500, 30.5, 50.4}};
const int NUM_CO_8HR_BP = 6;

// ข้อมูล Breakpoints ของ US EPA (สำหรับ SO2 - 1 Hour Average, หน่วย ppb)
const AQI_Breakpoint so2_1hr_breakpoints[] = {{0, 50, 0, 34}, {51, 100, 35, 144}, {101, 150, 145, 224}, {151, 200, 225, 304}, {201, 300, 305, 604}, {301, 500, 605, 1004}};
const int NUM_SO2_1HR_BP = 6;

// ข้อมูล Breakpoints ของ US EPA (สำหรับ NO2 - 1 Hour Average, หน่วย ppb)
const AQI_Breakpoint no2_1hr_breakpoints[] = {{0, 50, 0, 53}, {51, 100, 54, 100}, {101, 150, 101, 360}, {151, 200, 361, 649}, {201, 300, 650, 1249}, {301, 500, 1250, 2049}};
const int NUM_NO2_1HR_BP = 6;

// 2. CONVERSION FUNCTIONS
float ugm3_to_ppm(float C_ug_m3, float MolecularWeight) {
  return C_ug_m3 * (24.45 / MolecularWeight) / 1000.0;
}

float ugm3_to_ppb(float C_ug_m3, float MolecularWeight) {
  // ppb คือ ppm * 1000
  return ugm3_to_ppm(C_ug_m3, MolecularWeight) * 1000.0;
}
// molegular weight (g/mol) CO: 28.01, NO2: 46.01, O3: 48.00, SO2: 64.06
const float MW_CO = 28.01;
const float MW_NO2 = 46.01;
const float MW_O3 = 48.00;
const float MW_SO2 = 64.06;

//---------------- 3. AQI CALCULATION FUNCTION
int calculate_IAQI(float Cp, const AQI_Breakpoint breakpoints[], int num_breakpoints) {
  // 1. ตรวจสอบค่าที่อยู่นอกช่วง Breakpoints
  if (Cp <= breakpoints[0].C_Lo) {
    return breakpoints[0].I_Lo;
  }
  if (Cp >= breakpoints[num_breakpoints - 1].C_Hi) {
    return breakpoints[num_breakpoints - 1].I_Hi;
  }
  // 2. ค้นหาช่วง Breakpoint
  for (int i = 0; i < num_breakpoints; i++) {
    // ใช้ < C_Hi แทน <= C_Hi เพื่อให้ครอบคลุมช่วงอย่างถูกต้องตามนิยามของ EPA
    if (Cp >= breakpoints[i].C_Lo && Cp < breakpoints[i].C_Hi) {

      // สูตร: Ii = [(IHi - ILo) / (CHi - CLo)] * (Cp - CLo) + ILo
      float I_Hi = (float)breakpoints[i].I_Hi;
      float I_Lo = (float)breakpoints[i].I_Lo;
      float C_Hi = breakpoints[i].C_Hi;
      float C_Lo = breakpoints[i].C_Lo;
      float Ii = ((I_Hi - I_Lo) / (C_Hi - C_Lo)) * (Cp - C_Lo) + I_Lo;
      // ปัดเศษให้เป็นจำนวนเต็มที่ใกล้ที่สุดตามมาตรฐาน EPA
      return (int)round(Ii);
    }
  }
  // กรณีที่ค่าความเข้มข้นเท่ากับค่าสูงสุดของช่วงสุดท้ายพอดี (เช่น PM2.5 = 12.0)
  // เราจะใช้ช่วงนั้นๆ ในการคำนวณ ยกเว้นช่วงสุดท้าย
  if (Cp == breakpoints[num_breakpoints - 1].C_Hi) {
    return breakpoints[num_breakpoints - 1].I_Hi;
  }
  return -1; // ข้อผิดพลาด
}

//---------------- parse AQI data from JSON and calculate US AQI and dominant pollutant
String USAQI = "";
String dominantPollutant = "";
void parseAQI(float co, float no2, float o3, float so2, float pm2_5, float pm10) {

  AirQualityData data = {
      // create data structure
      co, // CO ug/m3
      no2, // NO2 ug/m3
      o3, // O3 ug/m3
      so2, // SO2 ug/m3
      pm2_5, // PM2.5 ug/m3
      pm10 // PM10 ug/m3
  };

  // CONVERT UNITS (US EPA Breakpoints unit ppb and ppm for gas)
  float C_pm25 = data.pm2_5;
  float C_pm10 = data.pm10;
  float C_o3_ppm = ugm3_to_ppm(data.o3, MW_O3); // O3: ug/m3 -> ppm
  float C_co_ppm = ugm3_to_ppm(data.co, MW_CO); // CO: ug/m3 -> ppm
  float C_so2_ppb = ugm3_to_ppb(data.so2, MW_SO2); // SO2: ug/m3 -> ppb
  float C_no2_ppb = ugm3_to_ppb(data.no2, MW_NO2); // NO2: ug/m3 -> ppb

  // CALCULATE IAQI FOR EACH POLLUTANT (คำนวณ IAQI แต่ละตัว)
  struct PollutantResult {
    String name;
    float concentration;
    int aqi;
  };

  PollutantResult results[6];
  results[0] = {"PM2.5", C_pm25, calculate_IAQI(C_pm25, pm25_breakpoints, NUM_PM25_BP)};
  results[1] = {"PM10", C_pm10, calculate_IAQI(C_pm10, pm10_breakpoints, NUM_PM10_BP)};
  results[2] = {"O3", C_o3_ppm, calculate_IAQI(C_o3_ppm, o3_8hr_breakpoints, NUM_O3_8HR_BP)};
  results[3] = {"CO", C_co_ppm, calculate_IAQI(C_co_ppm, co_8hr_breakpoints, NUM_CO_8HR_BP)};
  results[4] = {"SO2", C_so2_ppb, calculate_IAQI(C_so2_ppb, so2_1hr_breakpoints, NUM_SO2_1HR_BP)};
  results[5] = {"NO2", C_no2_ppb, calculate_IAQI(C_no2_ppb, no2_1hr_breakpoints, NUM_NO2_1HR_BP)};

  // FIND DOMINANT POLLUTANT AND US AQI (find max value)
  int max_aqi = -1;
  String dominant_pollutant = "N/A";
  float dominant_concentration = 0.0;

  for (int i = 0; i < 6; i++) {
    if (results[i].aqi > max_aqi) {
      max_aqi = results[i].aqi;
      dominant_pollutant = results[i].name;
      dominant_concentration = results[i].concentration;
    }
  }
  // create dominant output text
  String dominant = dominant_pollutant + " | " + String(dominant_concentration, 2);
  String unit = "";
  if (dominant_pollutant == "PM2.5" || dominant_pollutant == "PM10") {
    unit = " ug/m3";
  } else if (dominant_pollutant == "CO" || dominant_pollutant == "O3") {
    unit = " ppm";
  } else {
    unit = " ppb";
  }
  // the result is here
  dominantPollutant = dominant + unit; // resutl to show
  USAQI = max_aqi; // resutl to show

  log_d("US AQI Overall: %s", USAQI.c_str());
  log_d("Dominant Pollutant: %s", dominantPollutant.c_str());
}

//=============   update state, US AQI, dominant pollutant icon and text ===========================
void updateWeatherPanelTask(void *parameter) {

  // 1. fetch weather condition data from weather API
  static char reqURL[MAX_URL_LENGTH];
  snprintf(reqURL, MAX_URL_LENGTH, weatherUrl, weatherApiKey, query_parameter.c_str());
  const String JSON_INPUT = fetchUrlData(reqURL); // fetch weather data
  // 2. convert to json object
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, JSON_INPUT);
  if (error) {
    log_e("deserializeJson() failed: ");
    vTaskDelete(NULL);
    return;
  }
  // check of error
  int errorCode = doc["error"]["code"].as<int>();
  if (errorCode != 0) { // error occure
    log_e("Error code: %d %s", errorCode, doc["error"]["message"].as<const char *>());
    lv_label_set_text(ui_Info_Label_AQIlocation, "No data available");
    lv_img_set_src(ui_Info_Image_AQIimage, ""); // icon
    lv_obj_set_style_bg_color(ui_Info_Panel_AQI, lv_color_hex(0x777777), LV_PART_MAIN);
    lv_label_set_text(ui_Info_Label_AQIrate, "Please try select another city");
    lv_label_set_text(ui_Info_Label_AQIvalue, "-");
    lv_label_set_text(ui_Info_Label_AQIdominant, "");

  } else { // dadta available

    Weather_JSON data;
    // parsing data
    data.last_updated = doc["current"]["last_updated"].as<String>();
    data.state = doc["location"]["name"].as<String>();
    // AQI
    data.usepa_index = doc["current"]["air_quality"]["us-epa-index"].as<uint8_t>();
    data.name = us_epa_index_names[data.usepa_index - 1]; // name of aqi level
    data.co = doc["current"]["air_quality"]["co"].as<float>();
    data.no2 = doc["current"]["air_quality"]["no2"].as<float>();
    data.o3 = doc["current"]["air_quality"]["o3"].as<float>();
    data.so2 = doc["current"]["air_quality"]["so2"].as<float>();
    data.pm2_5 = doc["current"]["air_quality"]["pm2_5"].as<float>();
    data.pm10 = doc["current"]["air_quality"]["pm10"].as<float>();
    // weather condition
    data.temp_c = doc["current"]["temp_c"].as<float>();
    data.temp_f = doc["current"]["temp_f"].as<float>();
    data.wind_kph = doc["current"]["wind_kph"].as<float>();
    data.wind_mph = doc["current"]["wind_mph"].as<float>();
    data.wind_degree = doc["current"]["wind_degree"].as<float>();
    data.wind_dir = doc["current"]["wind_dir"].as<String>();
    data.pressure_mb = doc["current"]["pressure_mb"].as<float>();
    data.pressure_in = doc["current"]["pressure_in"].as<float>();
    data.precip_mm = doc["current"]["precip_mm"].as<float>();
    data.precip_in = doc["current"]["precip_in"].as<float>();
    data.humidity = doc["current"]["humidity"].as<float>();
    data.cloud = doc["current"]["cloud"].as<float>();
    data.feelslike_c = doc["current"]["feelslike_c"].as<float>();
    data.feelslike_f = doc["current"]["feelslike_f"].as<float>();
    data.uv = doc["current"]["uv"].as<float>();
    // code
    data.code = doc["current"]["condition"]["code"].as<uint16_t>();
    data.is_day = doc["current"]["is_day"].as<uint8_t>();

    log_d("Weather code: %d  Is day: %d", data.code, data.is_day);

    // 3. calculate & update AQI values
    parseAQI(data.co, data.no2, data.o3, data.so2, data.pm2_5, data.pm10);

    // 4. update weather panel

    // 4.1 udpate AQI pollution widget
    lv_img_set_src(ui_Info_Image_AQIimage, &us_epa_index_icon[data.usepa_index - 1]); // icon
    lv_obj_set_style_bg_color(ui_Info_Panel_AQI, lv_color_hex(us_epa_index_colors[data.usepa_index - 1]), LV_PART_MAIN); // widget color
    // lv_obj_set_style_bg_opa(ui_Info_Panel_AQI, 150, LV_PART_MAIN); // widget transprent
    lv_label_set_text(ui_Info_Label_AQIlocation, data.state.c_str());
    lv_label_set_text(ui_Info_Label_AQIvalue, USAQI.c_str());
    lv_label_set_text(ui_Info_Label_AQIrate, data.name);
    lv_label_set_text(ui_Info_Label_AQIdominant, dominantPollutant.c_str());
    String lastupdated = "Last Updated: " + data.last_updated;
    lv_label_set_text(ui_Info_Label_LastUpdated, lastupdated.c_str());

    // 4.2 udpate weather condition widget
    bool isNight = (data.is_day == 0);
    const lv_img_dsc_t *iconImg = getWeatherIconImage(data.code, isNight);
    const lv_img_dsc_t *homeImg = getHomeIconImage(data.code, isNight, now.month);

    String temp = "";
    if (temp_unit == 0)
      temp = String(data.temp_c, 1) + "°c";
    else
      temp = String(data.temp_f, 1) + "°F";

    lv_label_set_text(ui_Info_Label_Temp, temp.c_str()); // temp
    lv_img_set_src(ui_Info_Image_WeatherIcon, iconImg); // icon
    lv_img_set_src(ui_Info_Image_Home, homeImg); // icon

    String details = "";
    if (temp_unit == 0) {
      details = "Feel like : " + String(data.feelslike_c, 1) + "°c" + "\nWind     : " + String(data.wind_kph, 0) + " km/h " + data.wind_dir + "\nHumidity : " + String(data.humidity, 0) + "%" + "\nPressure : " + String(data.pressure_mb, 0) + " mb" + "\nUV Index : " + String(data.uv, 0);
    } else {
      details = "Feel like : " + String(data.feelslike_f, 1) + "°F" + "\nWind     : " + String(data.wind_kph, 0) + " m/h " + data.wind_dir + "\nHumidity : " + String(data.humidity, 0) + "%" + "\nPressure : " + String(data.pressure_in, 0) + " in" + "\nUV Index : " + String(data.uv, 0);
    }
    lv_label_set_text(ui_Info_Label_Detail, details.c_str());
    setWeatherPanelBgColor(data.code, isNight); // set wallpaper
  }

  UBaseType_t hwm = uxTaskGetStackHighWaterMark(NULL);
  log_d("{ Task stack remaining MIN: %u bytes }", hwm);
  weatherTask = NULL;
  vTaskDelete(NULL);
}

// update weather panel function
void updateWeatherPanel() {
  if (weatherTask == NULL) xTaskCreatePinnedToCore(updateWeatherPanelTask, "To update weather panel", 6 * 1024, NULL, 3, &weatherTask, 1);
}

//=========================== custom animation task ===========================

// Dog and Cat walking animation
static ui_anim_user_data_t cat_sprite_ud;
static ui_anim_user_data_t cat_move_ud;
static ui_anim_user_data_t dog_sprite_ud;
static ui_anim_user_data_t dog_move_ud;

int last_x_cat = 221;//first cat pos in ui_Screen_Info.c
int last_x_dog = 179;//first dog pos in ui_Screen_Info.c
int target_x_cat = 0;
int target_x_dog = 0;
int travel_time = 0;

#define MIN_WALK_POS 150
#define MAX_WALK_POS 350
#define frame_speed 400

static const lv_img_dsc_t *ui_imgset_cat_left_const[] = {&ui_img_custom_cat_left1_png, &ui_img_custom_cat_left2_png};
static const lv_img_dsc_t *ui_imgset_cat_right_const[] = {&ui_img_custom_cat_right1_png, &ui_img_custom_cat_right2_png};
static const lv_img_dsc_t *ui_imgset_dog_left_const[] = {&ui_img_custom_dog_left1_png, &ui_img_custom_dog_left2_png};
static const lv_img_dsc_t *ui_imgset_dog_right_const[] = {&ui_img_custom_dog_right1_png, &ui_img_custom_dog_right2_png};

// Persistent LVGL animations (do NOT put on stack)
static lv_anim_t cat_sprite_anim;
static lv_anim_t cat_move_anim;
static lv_anim_t dog_sprite_anim;
static lv_anim_t dog_move_anim;

// Stop walking sprite animation when movement ends
static void walk_move_ready_cb(lv_anim_t *a) {
  ui_anim_user_data_t *move_ud = (ui_anim_user_data_t *)a->user_data;
  lv_obj_t *obj = move_ud->target;
  lv_anim_del(obj, NULL); // stop all animations on this obj

  ui_anim_user_data_t *sprite_ud;
  if (obj == ui_Info_Image_cat)
    sprite_ud = &cat_sprite_ud;
  else
    sprite_ud = &dog_sprite_ud;

  // Freeze sprite animation
  sprite_ud->imgset_size = 1;
  lv_img_set_src(obj, sprite_ud->imgset[0]);
}

void walk_animation(lv_obj_t *obj, int beginX, int endX, lv_img_dsc_t **imgset, uint8_t imgset_size, uint8_t frameBegin, uint8_t frameEnd, uint16_t time, uint16_t playbackDelay, uint16_t playbackTime) {
  lv_anim_del(obj, NULL); // stop previous animations

  ui_anim_user_data_t *sprite_ud;
  ui_anim_user_data_t *move_ud;
  lv_anim_t *sprite_anim;
  lv_anim_t *move_anim;

  if (obj == ui_Info_Image_cat) {
    sprite_ud = &cat_sprite_ud;
    move_ud = &cat_move_ud;
    sprite_anim = &cat_sprite_anim;
    move_anim = &cat_move_anim;
  } else {
    sprite_ud = &dog_sprite_ud;
    move_ud = &dog_move_ud;
    sprite_anim = &dog_sprite_anim;
    move_anim = &dog_move_anim;
  }

  // ---------- SPRITE ----------
  sprite_ud->target = obj;
  sprite_ud->imgset = imgset;
  sprite_ud->imgset_size = imgset_size;
  sprite_ud->val = -1;

  lv_anim_init(sprite_anim);
  lv_anim_set_var(sprite_anim, obj);
  lv_anim_set_user_data(sprite_anim, sprite_ud);
  lv_anim_set_time(sprite_anim, frame_speed);
  lv_anim_set_values(sprite_anim, frameBegin, frameEnd);
  lv_anim_set_custom_exec_cb(sprite_anim, _ui_anim_callback_set_image_frame);
  lv_anim_set_path_cb(sprite_anim, lv_anim_path_linear);
  // lv_anim_set_repeat_count(sprite_anim, LV_ANIM_REPEAT_INFINITE);
  lv_anim_set_repeat_count(sprite_anim, time / frame_speed);
  lv_anim_start(sprite_anim);

  // ---------- MOVE ----------
  move_ud->target = obj;
  move_ud->val = -1;

  lv_anim_init(move_anim);
  lv_anim_set_var(move_anim, obj);
  lv_anim_set_user_data(move_anim, move_ud);
  lv_anim_set_time(move_anim, time);
  lv_anim_set_values(move_anim, beginX, endX);
  lv_anim_set_custom_exec_cb(move_anim, _ui_anim_callback_set_x);
  lv_anim_set_path_cb(move_anim, lv_anim_path_linear);
  lv_anim_set_playback_time(move_anim, playbackTime);
  lv_anim_set_playback_delay(move_anim, playbackDelay);
  lv_anim_set_ready_cb(move_anim, walk_move_ready_cb);
  lv_anim_start(move_anim);
}

// ---------- Timer-based random walk ----------
static void random_walk_task(lv_timer_t *timer) {
  travel_time = random(1000, 4000);
  uint32_t next_delay = travel_time + random(1000, 3000);

  if (random(0, 2) == 0) { // CAT
    target_x_cat = random(MIN_WALK_POS, MAX_WALK_POS);
    lv_img_dsc_t **image_set_cat = (target_x_cat > last_x_cat) ? (lv_img_dsc_t **)ui_imgset_cat_right_const : (lv_img_dsc_t **)ui_imgset_cat_left_const;
    walk_animation(ui_Info_Image_cat, last_x_cat, target_x_cat, image_set_cat, 2, 0, 1, travel_time, 0, 0);
    last_x_cat = target_x_cat;
  } else { // DOG
    target_x_dog = random(MIN_WALK_POS, MAX_WALK_POS);
    lv_img_dsc_t **image_set_dog = (target_x_dog > last_x_dog) ? (lv_img_dsc_t **)ui_imgset_dog_right_const : (lv_img_dsc_t **)ui_imgset_dog_left_const;
    walk_animation(ui_Info_Image_dog, last_x_dog, target_x_dog, image_set_dog, 2, 0, 1, travel_time, 0, 0);
    last_x_dog = target_x_dog;
  }

  lv_timer_set_period(timer, next_delay);
}

// ---------- Timer handle ----------
static lv_timer_t *random_walk_timer = NULL;

void weatherAnimation(bool animate) {
  if (animate) {
    spin_Animation(ui_Info_Image_Propeller, 0);
    if (random_walk_timer == NULL) {
      random_walk_timer = lv_timer_create(random_walk_task, 1000, NULL);
    }
  } else {
    lv_anim_del(ui_Info_Image_Propeller, NULL);
    if (random_walk_timer != NULL) {
      lv_timer_del(random_walk_timer);
      random_walk_timer = NULL;
    }
  }
}

// force turn off weather animation
void stopWeatherAnimation(lv_event_t *e) {
  weatherAnimation(false);
}

//=============================================================================