// XDK 4928 XAPI entry points verified against the WhiteOut retail image.
//
// The heap and input bodies are unchanged from the neighboring 4361/4627/5233
// builds. Every input address was independently identified by XbSymbolDatabase;
// the heap addresses were verified against their matching archived-library
// bodies. Keep utility-drive and dashboard-transition functions native until
// their 4928 identities and semantics are verified separately.

SOOVPA<8> XInputPoll_1_0_4928 = {
    0, 8, -1, 0, { { 0x00, 0x53 }, { 0x24, 0x25 }, { 0x48, 0x00 }, { 0x6E, 0xFE }, { 0x90, 0xB8 }, { 0xB6, 0xC3 }, { 0xDA, 0x89 }, { 0xFF, 0xF3 } }
};

#ifdef _DEBUG_TRACE
#define XAPI_4928_TRACE_NAME(Name) , #Name
#else
#define XAPI_4928_TRACE_NAME(Name)
#endif

OOVPATable XAPI_1_0_4928[] = {
    { (OOVPA*)&RtlCreateHeap_1_0_4361, XTL::EmuRtlCreateHeap XAPI_4928_TRACE_NAME(EmuRtlCreateHeap) },
    { (OOVPA*)&RtlAllocateHeap_1_0_4361, XTL::EmuRtlAllocateHeap XAPI_4928_TRACE_NAME(EmuRtlAllocateHeap) },
    { (OOVPA*)&RtlFreeHeap_1_0_4627, XTL::EmuRtlFreeHeap XAPI_4928_TRACE_NAME(EmuRtlFreeHeap) },
    { (OOVPA*)&RtlSizeHeap_1_0_4627, XTL::EmuRtlSizeHeap XAPI_4928_TRACE_NAME(EmuRtlSizeHeap) },
    { (OOVPA*)&XInitDevices_1_0_3911, XTL::EmuXInitDevices XAPI_4928_TRACE_NAME(EmuXInitDevices) },
    { (OOVPA*)&XGetDevices_1_0_5233, XTL::EmuXGetDevices XAPI_4928_TRACE_NAME(EmuXGetDevices) },
    { (OOVPA*)&XGetDeviceChanges_1_0_5233, XTL::EmuXGetDeviceChanges XAPI_4928_TRACE_NAME(EmuXGetDeviceChanges) },
    { (OOVPA*)&XInputOpen_1_0_5233, XTL::EmuXInputOpen XAPI_4928_TRACE_NAME(EmuXInputOpen) },
    { (OOVPA*)&XInputGetCapabilities_1_0_5233, XTL::EmuXInputGetCapabilities XAPI_4928_TRACE_NAME(EmuXInputGetCapabilities) },
    { (OOVPA*)&XInputGetState_1_0_5233, XTL::EmuXInputGetState XAPI_4928_TRACE_NAME(EmuXInputGetState) },
    { (OOVPA*)&XInputSetState_1_0_5233, XTL::EmuXInputSetState XAPI_4928_TRACE_NAME(EmuXInputSetState) },
    { (OOVPA*)&XInputPoll_1_0_4928, XTL::EmuXInputPoll XAPI_4928_TRACE_NAME(EmuXInputPoll) },
    { (OOVPA*)&XInputCloseInternal_1_0_5233, 0 XAPI_4928_TRACE_NAME(XInputCloseInternal) },
    { (OOVPA*)&XInputClose_1_0_5233, XTL::EmuXInputClose XAPI_4928_TRACE_NAME(EmuXInputClose) },
    { (OOVPA*)&XapiThreadStartup_1_0_4361, XTL::EmuXapiThreadStartup XAPI_4928_TRACE_NAME(EmuXapiThreadStartup) }
};

#undef XAPI_4928_TRACE_NAME

uint32 XAPI_1_0_4928_SIZE = sizeof(XAPI_1_0_4928);
