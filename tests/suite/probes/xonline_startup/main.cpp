// XOnline 5849 startup regression probe (non-secure xonline.lib variant).

#include "xdk_xtrace.h"
#include <winsockx.h>

void __cdecl main()
{
    xt_begin("xonline_startup");

    const int xnet_patched = xt_is_hle_patched((const void*)XNetStartup);
    const int wsa_patched = xt_is_hle_patched((const void*)WSAStartup);
    xt_chk("xonline.xnet_startup_hle", 1, xnet_patched);
    xt_chk("xonline.wsa_startup_hle", 1, wsa_patched);
    if(!xnet_patched || !wsa_patched)
    {
        xt_emit("NOTE XONLINE 1.0.5849 startup entry points are not HLE-patched");
        xt_end_and_exit();
    }

    const INT xnet_result = XNetStartup(NULL);
    xt_chk_u32("xonline.xnet_startup_result", 0, (DWORD)xnet_result);

    WSADATA data;
    ZeroMemory(&data, sizeof(data));
    const INT wsa_result = WSAStartup(MAKEWORD(2, 2), &data);
    xt_chk_u32("xonline.wsa_startup_result", 0, (DWORD)wsa_result);
    xt_chk_u32("xonline.wsa_version", MAKEWORD(2, 2), data.wVersion);

    xt_end_and_exit();
}
