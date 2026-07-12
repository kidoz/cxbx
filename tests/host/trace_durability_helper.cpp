// Child process used to validate trace anchors under forced termination.
#include "core/trace.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#ifndef POINTER_64
#define POINTER_64 __ptr64
#endif
#include <windows.h>

int main(int argc, char** argv)
{
    if(argc != 5)
    {
        return 2;
    }
    if(_putenv_s("CXBX_TRACE_EVT_FILE", argv[1]) != 0)
    {
        return 3;
    }
    std::FILE* text = nullptr;
    if(fopen_s(&text, argv[2], "wb") != 0 || text == nullptr)
    {
        return 4;
    }
    cxbx::trace::Initialize(text);
    cxbx::trace::SetEnabled(cxbx::trace::Channel::Mmio, true);

    std::FILE* ready = nullptr;
    if(fopen_s(&ready, argv[3], "wb") != 0 || ready == nullptr)
    {
        return 5;
    }
    std::fputs("ready\n", ready);
    std::fclose(ready);

    if(std::strcmp(argv[4], "initialized") == 0)
    {
        Sleep(INFINITE);
    }
    for(std::uint32_t value = 0;; ++value)
    {
        cxbx::trace::RecordBinary(cxbx::trace::Event::MmioAccess, 0xFD000000U + value);
        if((value & 0x3FFU) == 0)
        {
            Sleep(1);
        }
    }
}
