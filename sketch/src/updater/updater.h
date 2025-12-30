#pragma once

bool newFirmwareAvailable();
bool performOnlineUpdate();

extern bool firmware_checked;
extern const char* compile_date;
extern const char* current_version;
