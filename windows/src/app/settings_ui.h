#pragma once

#include <windows.h>

#include "settings.h"

/// Modal native settings window. Returns true when the user pressed OK and changes were saved.
bool showSettingsDialog(HWND owner, lz::Settings& settings);
