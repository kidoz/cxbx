// Policy for selecting the safe LDT-less FS content-swap mode.
#pragma once

inline bool EmuFsContentSwapEnabledByOverride(const char* overrideValue)
{
    return overrideValue == nullptr ||
           overrideValue[0] != '0' ||
           overrideValue[1] != '\0';
}
