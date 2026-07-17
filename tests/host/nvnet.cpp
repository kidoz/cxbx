#include "core/nvnet.h"

#include <cstdio>

int main()
{
    if(cxbx::nvnet::RegisterValueAfterWrite(
           cxbx::nvnet::TxRxControl,
           cxbx::nvnet::TxRxControlBit2) !=
       cxbx::nvnet::TxRxControlIdle)
    {
        std::fputs("NVNET TxRx bit 2 must complete in the idle state\n", stderr);
        return 1;
    }

    if(cxbx::nvnet::RegisterValueAfterWrite(0x110u, 0x00010064u) !=
           0x00010064u ||
       cxbx::nvnet::RegisterValueAfterWrite(
           cxbx::nvnet::TxRxControl, 0x00000003u) != 0x00000003u)
    {
        std::fputs("ordinary NVNET register writes must retain their value\n",
                   stderr);
        return 1;
    }

    return 0;
}
