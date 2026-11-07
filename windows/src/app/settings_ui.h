#pragma once

#include <windows.h>

#include "settings.h"

/// Modal native settings window. Changes are written to the settings file on OK.
void showSettingsDialog(HWND owner, lz::Settings& settings);
