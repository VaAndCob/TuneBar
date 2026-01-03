#pragma once


extern bool firmware_checked;
extern const char* compile_date;
extern const char* current_version;


const char *newFirmwareAvailable();
void ota_show_popup();
void notifyUpdate(const char *latestVer);
void memoryInfo(char *buf, size_t len);
void systemInfo(char *buf, size_t len);
