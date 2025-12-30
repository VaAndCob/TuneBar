#pragma once


extern bool firmware_checked;
extern const char* compile_date;
extern const char* current_version;


bool newFirmwareAvailable();
bool performOnlineUpdate();

void memoryInfo(char *buf, size_t len);
void systemInfo(char *buf, size_t len);
