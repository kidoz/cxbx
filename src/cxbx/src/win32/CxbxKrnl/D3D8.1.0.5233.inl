// D3D8 Version 1.0.5233 OOVPA table.
//
// Existing signatures are reused only where they resolve uniquely inside the
// 5233 D3D section at the address independently identified by the symbol
// database. The lifecycle signatures below were generated from adjacent XDK
// libraries and verified against a retail 5233 image. CreateTexture2 and Swap
// changed across both adjacent libraries, so their relocation-free pairs were
// selected from the independently located 5233 bodies and collision-checked
// against the local title and conformance corpus.

SOOVPA<13> IDirect3DDevice8_CreateTexture2_1_0_5233 =
{
    0, 13, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x03, 0xD2 },
        { 0x0D, 0x83 },
        { 0x18, 0x33 },
        { 0x2E, 0x6A },
        { 0x44, 0xF7 },
        { 0x53, 0x6A },
        { 0x67, 0x68 },
        { 0x74, 0xFF },
        { 0x7A, 0x85 },
        { 0x84, 0x5F },
        { 0x88, 0xC2 },
        { 0x89, 0x1C }
    }
};

SOOVPA<12> IDirect3DDevice8_CreateIndexBuffer2_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x05, 0xC0 },
        { 0x0A, 0xE8 },
        { 0x11, 0x75 },
        { 0x16, 0x33 },
        { 0x1C, 0x89 },
        { 0x22, 0x8D },
        { 0x28, 0x00 },
        { 0x2D, 0x04 },
        { 0x33, 0x90 },
        { 0x39, 0x90 },
        { 0x3F, 0x90 }
    }
};

SOOVPA<12> IDirect3DDevice8_SetViewport_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x83 },
        { 0x17, 0x0C },
        { 0x2E, 0x5C },
        { 0x45, 0x1B },
        { 0x5C, 0xE1 },
        { 0x73, 0x03 },
        { 0x8B, 0x01 },
        { 0xA2, 0x24 },
        { 0xB9, 0x81 },
        { 0xD0, 0x15 },
        { 0xE7, 0xE8 },
        { 0xFF, 0x00 }
    }
};

SOOVPA<12> IDirect3DDevice8_GetViewportOffsetAndScale_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0xA1 },
        { 0x17, 0x06 },
        { 0x2E, 0xA8 },
        { 0x45, 0x00 },
        { 0x5C, 0x10 },
        { 0x73, 0xDB },
        { 0x8B, 0x9C },
        { 0xA2, 0x06 },
        { 0xB9, 0xD9 },
        { 0xD0, 0x80 },
        { 0xE7, 0x44 },
        { 0xFF, 0xD8 }
    }
};

SOOVPA<15> IDirect3DDevice8_Swap_1_0_5233 =
{
    0, 15, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x07, 0x8B },
        { 0x0D, 0x85 },
        { 0x20, 0x53 },
        { 0x25, 0x85 },
        { 0x2F, 0x8B },
        { 0x3C, 0x6A },
        { 0x6C, 0x8B },
        { 0x73, 0xEB },
        { 0x7C, 0x6A },
        { 0xAB, 0x8B },
        { 0xB2, 0x85 },
        { 0xD1, 0x8B },
        { 0xD8, 0xC2 },
        { 0xD9, 0x04 }
    }
};

SOOVPA<12> IDirect3DDevice8_BlockUntilVerticalBlank_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0xA1 },
        { 0x05, 0x6A },
        { 0x08, 0x00 },
        { 0x0C, 0x80 },
        { 0x11, 0x00 },
        { 0x15, 0x6A },
        { 0x19, 0x19 },
        { 0x1D, 0xFF },
        { 0x23, 0xC3 },
        { 0x26, 0x90 },
        { 0x2A, 0x90 },
        { 0x2F, 0x90 }
    }
};

// Generated from the 4627 d3d8.lib body and verified unique in the local XBE
// corpus. The 5233 body changes a device-state offset, so the older signature
// does not resolve even though the relocation-free instruction shape remains.
SOOVPA<12> IDirect3DDevice8_SetSoftDisplayFilter_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x08, 0xA1 },
        { 0x11, 0x08 },
        { 0x1A, 0x33 },
        { 0x22, 0xD1 },
        { 0x2B, 0x6A },
        { 0x32, 0x15 },
        { 0x3C, 0x95 },
        { 0x45, 0x00 },
        { 0x4D, 0x5E },
        { 0x56, 0x90 },
        { 0x5F, 0x90 }
    }
};

SOOVPA<12> IDirect3DDevice8_SetTileNoWait_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x11, 0xF0 },
        { 0x22, 0x08 },
        { 0x34, 0x24 },
        { 0x45, 0x06 },
        { 0x56, 0x24 },
        { 0x68, 0x81 },
        { 0x7B, 0x5B },
        { 0x8A, 0x40 },
        { 0x9C, 0xE8 },
        { 0xAD, 0xC0 },
        { 0xBF, 0x90 }
    }
};

SOOVPA<12> IDirect3DDevice8_GetDepthStencilSurface2_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0xA1 },
        { 0x05, 0x56 },
        { 0x06, 0x8B },
        { 0x08, 0xB8 },
        { 0x0B, 0x00 },
        { 0x0E, 0x75 },
        { 0x10, 0x33 },
        { 0x13, 0xC3 },
        { 0x15, 0xE8 },
        { 0x1A, 0x8B },
        { 0x1C, 0x5E },
        { 0x1F, 0x90 }
    }
};

SOOVPA<12> IDirect3DDevice8_SetPixelShaderConstant_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x17, 0x0C },
        { 0x2E, 0x57 },
        { 0x45, 0x00 },
        { 0x5C, 0x0F },
        { 0x73, 0xF3 },
        { 0x8B, 0x08 },
        { 0xA2, 0x8B },
        { 0xB8, 0x35 },
        { 0xCF, 0x35 },
        { 0xE6, 0x35 },
        { 0xFD, 0x35 }
    }
};

SOOVPA<12> IDirect3DDevice8_SetPixelShaderProgram_1_0_5233 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x05, 0xD2 },
        { 0x0B, 0x74 },
        { 0x11, 0x00 },
        { 0x16, 0x00 },
        { 0x1C, 0x04 },
        { 0x22, 0x00 },
        { 0x28, 0x00 },
        { 0x2D, 0xE9 },
        { 0x33, 0x44 },
        { 0x39, 0x00 },
        { 0x3F, 0x90 }
    }
};

#ifdef _DEBUG_TRACE
#define D3D8_5233_TRACE_NAME(Name) , #Name
#else
#define D3D8_5233_TRACE_NAME(Name)
#endif

OOVPATable D3D8_1_0_5233[] =
{
    { (OOVPA*)&IDirect3DDevice8_CreateTexture2_1_0_5233, XTL::EmuIDirect3DDevice8_CreateTexture2 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_CreateTexture2) },
    { (OOVPA*)&IDirect3DTexture8_GetSurfaceLevel2_1_0_5558, XTL::EmuIDirect3DTexture8_GetSurfaceLevel2 D3D8_5233_TRACE_NAME(EmuIDirect3DTexture8_GetSurfaceLevel2) },
    { (OOVPA*)&IDirect3DTexture8_LockRect_1_0_5558, XTL::EmuIDirect3DTexture8_LockRect D3D8_5233_TRACE_NAME(EmuIDirect3DTexture8_LockRect) },
    { (OOVPA*)&IDirect3DDevice8_CreateIndexBuffer2_1_0_5233, XTL::EmuIDirect3DDevice8_CreateIndexBuffer2 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_CreateIndexBuffer2) },
    { (OOVPA*)&IDirect3DDevice8_CreateVertexBuffer2_1_0_4627, XTL::EmuIDirect3DDevice8_CreateVertexBuffer2 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_CreateVertexBuffer2) },
    { (OOVPA*)&IDirect3DVertexBuffer8_Lock2_1_0_5558, XTL::EmuIDirect3DVertexBuffer8_Lock2 D3D8_5233_TRACE_NAME(EmuIDirect3DVertexBuffer8_Lock2) },
    { (OOVPA*)&IDirect3DResource8_AddRef_1_0_3925, XTL::EmuIDirect3DResource8_AddRef D3D8_5233_TRACE_NAME(EmuIDirect3DResource8_AddRef) },
    { (OOVPA*)&IDirect3DResource8_Release_1_0_5558, XTL::EmuIDirect3DResource8_Release D3D8_5233_TRACE_NAME(EmuIDirect3DResource8_Release) },
    { (OOVPA*)&IDirect3DResource8_Register_1_0_5558, XTL::EmuIDirect3DResource8_Register D3D8_5233_TRACE_NAME(EmuIDirect3DResource8_Register) },
    { (OOVPA*)&IDirect3D8_CreateDevice_1_0_5344, XTL::EmuIDirect3D8_CreateDevice D3D8_5233_TRACE_NAME(EmuIDirect3D8_CreateDevice) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_PSTextureModes_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_PSTextureModes D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_PSTextureModes) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_Simple_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_Simple D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_Simple) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderStateNotInline_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderStateNotInline D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderStateNotInline) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_EdgeAntiAlias_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_ShadowFunc_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_ShadowFunc D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_ShadowFunc) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_FogColor_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_FogColor D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_FogColor) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_CullMode_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_CullMode D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_CullMode) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_FrontFace_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_FrontFace D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_FrontFace) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_NormalizeNormals_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_NormalizeNormals D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_NormalizeNormals) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_TextureFactor_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_TextureFactor D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_TextureFactor) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_LineWidth_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_LineWidth D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_LineWidth) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_Dxt1NoiseEnable_1_0_5344, XTL::EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_ZBias_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_ZBias D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_ZBias) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_LogicOp_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_LogicOp D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_LogicOp) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_FillMode_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_FillMode D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_FillMode) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_BackFillMode_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_BackFillMode D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_BackFillMode) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_TwoSidedLighting_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_TwoSidedLighting) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_VertexBlend_1_0_4361, XTL::EmuIDirect3DDevice8_SetRenderState_VertexBlend D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_VertexBlend) },
    { (OOVPA*)&IDirect3DDevice8_SetTextureState_TexCoordIndex_1_0_5558, XTL::EmuIDirect3DDevice8_SetTextureState_TexCoordIndex D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetTextureState_TexCoordIndex) },
    { (OOVPA*)&IDirect3DDevice8_SetTextureState_BumpEnv_1_0_4627, XTL::EmuIDirect3DDevice8_SetTextureState_BumpEnv D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetTextureState_BumpEnv) },
    { (OOVPA*)&IDirect3DDevice8_SetTextureState_BorderColor_1_0_4627, XTL::EmuIDirect3DDevice8_SetTextureState_BorderColor D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetTextureState_BorderColor) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_ZEnable_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_ZEnable D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_ZEnable) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_StencilEnable_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_StencilEnable D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_StencilEnable) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_StencilFail_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_StencilFail D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_StencilFail) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_YuvEnable_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_YuvEnable D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_YuvEnable) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_MultiSampleAntiAlias_1_0_5558, XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_MultiSampleMask_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleMask D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_MultiSampleMask) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderState_SampleAlpha_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderState_SampleAlpha D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderState_SampleAlpha) },
    { (OOVPA*)&IDirect3DDevice8_CreateVertexShader_1_0_4361, XTL::EmuIDirect3DDevice8_CreateVertexShader D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_CreateVertexShader) },
    { (OOVPA*)&IDirect3DDevice8_SetVertexShaderConstant1_1_0_5558, XTL::EmuIDirect3DDevice8_SetVertexShaderConstant1 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetVertexShaderConstant1) },
    { (OOVPA*)&IDirect3DDevice8_SetVertexShaderConstant4_1_0_5558, XTL::EmuIDirect3DDevice8_SetVertexShaderConstant4 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetVertexShaderConstant4) },
    { (OOVPA*)&IDirect3DDevice8_SetVertexShaderConstantNotInlineFast_1_0_4627, XTL::EmuIDirect3DDevice8_SetVertexShaderConstantNotInline D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetVertexShaderConstantNotInline) },
    { (OOVPA*)&IDirect3DDevice8_SetStreamSource_1_0_5558, XTL::EmuIDirect3DDevice8_SetStreamSource D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetStreamSource) },
    { (OOVPA*)&IDirect3DDevice8_SelectVertexShader_1_0_4627, XTL::EmuIDirect3DDevice8_SelectVertexShader D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SelectVertexShader) },
    { (OOVPA*)&IDirect3DDevice8_SetShaderConstantMode_1_0_5558, XTL::EmuIDirect3DDevice8_SetShaderConstantMode D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetShaderConstantMode) },
    { (OOVPA*)&IDirect3DDevice8_SetVertexShader_1_0_5344, XTL::EmuIDirect3DDevice8_SetVertexShader D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetVertexShader) },
    { (OOVPA*)&IDirect3DDevice8_SetRenderTarget_1_0_4627, XTL::EmuIDirect3DDevice8_SetRenderTarget D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetRenderTarget) },
    { (OOVPA*)&D3DDevice_SetGammaRamp_1_0_5659, XTL::EmuIDirect3DDevice8_SetGammaRamp D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetGammaRamp) },
    { (OOVPA*)&IDirect3DDevice8_SetTransform_1_0_4134, XTL::EmuIDirect3DDevice8_SetTransform D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetTransform) },
    { (OOVPA*)&IDirect3DDevice8_GetVisibilityTestResult_1_0_5659, XTL::EmuIDirect3DDevice8_GetVisibilityTestResult D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_GetVisibilityTestResult) },
    { (OOVPA*)&IDirect3DDevice8_SetFlickerFilter_1_0_4627, XTL::EmuIDirect3DDevice8_SetFlickerFilter D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetFlickerFilter) },
    { (OOVPA*)&IDirect3DDevice8_SetSoftDisplayFilter_1_0_5233, XTL::EmuIDirect3DDevice8_SetSoftDisplayFilter D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetSoftDisplayFilter) },
    { (OOVPA*)&IDirect3DDevice8_GetBackBuffer2_1_0_5558, XTL::EmuIDirect3DDevice8_GetBackBuffer2 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_GetBackBuffer2) },
    { (OOVPA*)&IDirect3DDevice8_GetRenderTarget2_1_0_5558, XTL::EmuIDirect3DDevice8_GetRenderTarget2 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_GetRenderTarget2) },
    { (OOVPA*)&IDirect3DDevice8_GetDepthStencilSurface2_1_0_5233, XTL::EmuIDirect3DDevice8_GetDepthStencilSurface2 D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_GetDepthStencilSurface2) },
    { (OOVPA*)&IDirect3DDevice8_SetViewport_1_0_5233, XTL::EmuIDirect3DDevice8_SetViewport D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetViewport) },
    { (OOVPA*)&IDirect3DDevice8_GetViewportOffsetAndScale_1_0_5233, XTL::EmuIDirect3DDevice8_GetViewportOffsetAndScale D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_GetViewportOffsetAndScale) },
    { (OOVPA*)&IDirect3DDevice8_SetTexture_1_0_5558, XTL::EmuIDirect3DDevice8_SetTexture D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetTexture) },
    { (OOVPA*)&IDirect3DDevice8_SetIndices_1_0_5558, XTL::EmuIDirect3DDevice8_SetIndices D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetIndices) },
    { (OOVPA*)&IDirect3DDevice8_BeginVisibilityTest_1_0_4627, XTL::EmuIDirect3DDevice8_BeginVisibilityTest D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_BeginVisibilityTest) },
    { (OOVPA*)&IDirect3DDevice8_EndVisibilityTest_1_0_5659, XTL::EmuIDirect3DDevice8_EndVisibilityTest D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_EndVisibilityTest) },
    { (OOVPA*)&IDirect3DDevice8_SetTileNoWait_1_0_5233, XTL::EmuIDirect3DDevice8_SetTileNoWait D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetTileNoWait) },
    { (OOVPA*)&IDirect3DDevice8_BlockUntilVerticalBlank_1_0_5233, XTL::EmuIDirect3DDevice8_BlockUntilVerticalBlank D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_BlockUntilVerticalBlank) },
    { (OOVPA*)&IDirect3DDevice8_Swap_1_0_5233, XTL::EmuIDirect3DDevice8_Swap D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_Swap) },
    { (OOVPA*)&IDirect3DSurface8_GetDesc_1_0_5558, XTL::EmuIDirect3DSurface8_GetDesc D3D8_5233_TRACE_NAME(EmuIDirect3DSurface8_GetDesc) },
    { (OOVPA*)&IDirect3DSurface8_LockRect_1_0_5558, XTL::EmuIDirect3DSurface8_LockRect D3D8_5233_TRACE_NAME(EmuIDirect3DSurface8_LockRect) },
    { (OOVPA*)&IDirect3DDevice8_Clear_1_0_5558, XTL::EmuIDirect3DDevice8_Clear D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_Clear) },
    { (OOVPA*)&IDirect3DDevice8_DrawVertices_1_0_5558, XTL::EmuIDirect3DDevice8_DrawVertices D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_DrawVertices) },
    { (OOVPA*)&IDirect3DDevice8_DrawIndexedVertices_1_0_5558, XTL::EmuIDirect3DDevice8_DrawIndexedVertices D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_DrawIndexedVertices) },
    { (OOVPA*)&IDirect3DDevice8_SetVertexData2f_1_0_5558, XTL::EmuIDirect3DDevice8_SetVertexData2f D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetVertexData2f) },
    { (OOVPA*)&IDirect3DDevice8_Begin_1_0_5558, XTL::EmuIDirect3DDevice8_Begin D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_Begin) },
    { (OOVPA*)&IDirect3DDevice8_End_1_0_4627, XTL::EmuIDirect3DDevice8_End D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_End) },
    { (OOVPA*)&IDirect3DDevice8_SetPixelShaderProgram_1_0_5233, XTL::EmuIDirect3DDevice8_SetPixelShaderProgram D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetPixelShaderProgram) },
    { (OOVPA*)&IDirect3DDevice8_SetPixelShader_1_0_5558, XTL::EmuIDirect3DDevice8_SetPixelShader D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetPixelShader) },
    { (OOVPA*)&IDirect3DDevice8_SetPixelShaderConstant_1_0_5233, XTL::EmuIDirect3DDevice8_SetPixelShaderConstant D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_SetPixelShaderConstant) },
    { (OOVPA*)&Get2DSurfaceDesc_1_0_5344, XTL::EmuGet2DSurfaceDesc D3D8_5233_TRACE_NAME(EmuGet2DSurfaceDesc) },
    { (OOVPA*)&IDirect3DCubeTexture8_LockRect_1_0_3925, XTL::EmuIDirect3DCubeTexture8_LockRect D3D8_5233_TRACE_NAME(EmuIDirect3DCubeTexture8_LockRect) },
    { (OOVPA*)&IDirect3DDevice8_MakeSpace_1_0_4627, XTL::EmuIDirect3DDevice8_MakeSpace D3D8_5233_TRACE_NAME(EmuIDirect3DDevice8_MakeSpace) },
    { (OOVPA*)&IDirect3D8_KickOffAndWaitForIdle_1_0_5558, XTL::EmuIDirect3D8_KickOffAndWaitForIdle D3D8_5233_TRACE_NAME(EmuIDirect3D8_KickOffAndWaitForIdle) }
};

#undef D3D8_5233_TRACE_NAME

uint32 D3D8_1_0_5233_SIZE = sizeof(D3D8_1_0_5233);
