#pragma once
#include <Arduino.h>

struct WifiEntry {
  String ssid;
  String password;
  int32_t rssi;
};

extern WifiEntry wifiList[];


int loadWifiList(WifiEntry list[]);
void saveWifiList(WifiEntry list[], int count);
int addOrUpdateWifi(const char *ssid, const char *password, WifiEntry list[], int count);
void scanWiFi(bool updateList);
void wifiConnect();

String fetchUrlData(const char* url);