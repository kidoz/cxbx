// D3D8 Version 1.0.5659 OOVPA table.
// Generated from XDK 5659 d3d8.lib via reuse-then-generate.
// 74 reused, 12 fresh, 5 skipped. Some 5659.4 titles use the 5788 alias
// bodies noted below while still reporting library version 5659.

// Fresh signature definitions
SOOVPA<8> D3DDevice_RunPushBuffer_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x83 },
        { 0x24, 0x8B },
        { 0x48, 0x04 },
        { 0x6D, 0x89 },
        { 0x91, 0x3B },
        { 0xB6, 0x8B },
        { 0xDA, 0x50 },
        { 0xFF, 0x48 }
    }
};

// _D3DDevice_Swap@4 (d3d8.lib 5659, 229 bytes)
SOOVPA<12> IDirect3DDevice8_Swap_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x14, 0x08 },
        { 0x29, 0x05 },
        { 0x3E, 0xE8 },
        { 0x52, 0x00 },
        { 0x67, 0xE8 },
        { 0x7C, 0x6A },
        { 0x91, 0x19 },
        { 0xA5, 0x00 },
        { 0xB9, 0x0D },
        { 0xCF, 0x08 },
        { 0xE4, 0x00 }
    }
};

// _D3DDevice_GetDisplayFieldStatus@4 (d3d8.lib 5659, 58 bytes).
// Native XMV calls this every frame to pace decode against the vblank tally.
SOOVPA<8> IDirect3DDevice8_GetDisplayFieldStatus_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0xA1 },
        { 0x05, 0x8B },
        { 0x07, 0xE8 },
        { 0x0B, 0x8B },
        { 0x0F, 0x89 },
        { 0x12, 0xF7 },
        { 0x1C, 0x74 },
        { 0x30, 0xC2 }
    }
};

// _D3DDevice_DrawIndexedVerticesUP@20 (from the Arx Fatalis 5659 image at
// 0x00143610 -- the shipped body differs from the archived d3d8.lib copy, so
// the pairs were picked from the title and verified unique there and absent
// from the 4627/5849-era titles). Discriminated from DrawVerticesUP by the
// esi-based device load at +0x08 and the 5th-argument read at +0x31.
SOOVPA<10> IDirect3DDevice8_DrawIndexedVerticesUP_1_0_5659 =
{
    0, 10, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x06, 0x53 },
        { 0x08, 0x8B },
        { 0x09, 0x35 },
        { 0x0E, 0x57 },
        { 0x28, 0xC7 },
        { 0x2A, 0xFC },
        { 0x2B, 0x17 },
        { 0x31, 0x8B },
        { 0x33, 0x14 }
    }
};

// _D3DDevice_SetTextureState_BorderColor@8 (d3d8.lib 5659, 61 bytes)
SOOVPA<8> IDirect3DDevice8_SetTextureState_BorderColor_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x08, 0x06 },
        { 0x13, 0x8B },
        { 0x19, 0xC1 },
        { 0x22, 0x89 },
        { 0x2A, 0x04 },
        { 0x33, 0x89 },
        { 0x3C, 0x00 }
    }
};

// _D3DDevice_BeginVisibilityTest@0 (d3d8.lib 5659, 43 bytes)
SOOVPA<8> IDirect3DDevice8_BeginVisibilityTest_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x07, 0x8B },
        { 0x0C, 0x72 },
        { 0x13, 0xC7 },
        { 0x18, 0x00 },
        { 0x1E, 0x89 },
        { 0x24, 0x83 },
        { 0x2A, 0xC3 }
    }
};

// _D3DDevice_EndVisibilityTest@4 (d3d8.lib 5659, 90 bytes)
SOOVPA<8> IDirect3DDevice8_EndVisibilityTest_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x0C, 0xF0 },
        { 0x19, 0x00 },
        { 0x26, 0x8B },
        { 0x32, 0xC7 },
        { 0x3F, 0x40 },
        { 0x4C, 0x70 },
        { 0x59, 0x00 }
    }
};

// _D3DDevice_GetVisibilityTestResult@12 (d3d8.lib 5659, 97 bytes)
SOOVPA<8> IDirect3DDevice8_GetVisibilityTestResult_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x0D, 0xEA },
        { 0x1B, 0x00 },
        { 0x29, 0x75 },
        { 0x34, 0xE8 },
        { 0x44, 0x8B },
        { 0x52, 0x8B },
        { 0x60, 0x00 }
    }
};

// PixelJar::Get2DSurfaceDesc (5659.4/5788 alias body, 177 bytes)
SOOVPA<8> Get2DSurfaceDesc_1_0_5659 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x83 },
        { 0x19, 0x8B },
        { 0x30, 0x82 },
        { 0x4B, 0x00 },
        { 0x64, 0x6C },
        { 0x7D, 0x50 },
        { 0x96, 0x24 },
        { 0xB0, 0x00 }
    }
};

// D3D::SetFence (5659.4/5788 alias body, 184 bytes)
SOOVPA<7> SetFence_1_0_5659 =
{
    0, 7, XREF_SETFENCE, 0,
    {
        { 0x00, 0x56 },
        { 0x1E, 0xD7 },
        { 0x3D, 0xC7 },
        { 0x5B, 0x1C },
        { 0x7A, 0x4E },
        { 0x98, 0x83 },
        { 0xB7, 0x00 }
    }
};

// _D3DDevice_InsertFence@0 (d3d8.lib 5659, 8 bytes)
SOOVPA<5> D3DDevice_InsertFence_1_0_5659 =
{
    0, 5, -1, 1,
    {
        { 0x03, XREF_SETFENCE },
        { 0x00, 0x6A },
        { 0x01, 0x00 },
        { 0x02, 0xE8 },
        { 0x07, 0xC3 }
    }
};

// D3D::BlockOnTime (d3d8.lib 5659, 362 bytes)
SOOVPA<7> BlockOnTime_1_0_5659 =
{
    0, 7, XREF_BLOCKONTIME, 0,
    {
        { 0x00, 0x56 },
        { 0x2A, 0xE8 },
        { 0x55, 0x3B },
        { 0x7F, 0x0F },
        { 0xAA, 0x8B },
        { 0xD2, 0xE8 },
        { 0xFF, 0x18 }
    }
};

// _D3DDevice_BlockOnFence@4 (d3d8.lib 5659, 15 bytes)
SOOVPA<8> D3DDevice_BlockOnFence_1_0_5659 =
{
    0, 8, -1, 1,
    {
        { 0x08, XREF_BLOCKONTIME },
        { 0x00, 0x8B },
        { 0x01, 0x44 },
        { 0x02, 0x24 },
        { 0x04, 0x6A },
        { 0x07, 0xE8 },
        { 0x0C, 0xC2 },
        { 0x0E, 0x00 }
    }
};

// _D3DDevice_SetGammaRamp@8 (d3d8.lib 5659, 131 bytes)
SOOVPA<12> D3DDevice_SetGammaRamp_1_0_5659 =
{
    0, 12, -1, 0,
    {
        { 0x00, 0x53 },
        { 0x0D, 0x8B },
        { 0x17, 0x00 },
        { 0x23, 0xE6 },
        { 0x2F, 0xFD },
        { 0x3B, 0x74 },
        { 0x46, 0xDC },
        { 0x52, 0xFB },
        { 0x5E, 0x00 },
        { 0x6A, 0x5F },
        { 0x76, 0x00 },
        { 0x82, 0x00 }
    }
};


OOVPATable D3D8_1_0_5659[] =
{
    // D3DDevice::CreatePushBuffer2
    {
        (OOVPA*)&D3DDevice_CreatePushBuffer2_1_0_5849,
        XTL::EmuIDirect3DDevice8_CreatePushBuffer2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_CreatePushBuffer2"
        #endif
    },
    // D3DDevice::BeginPushBuffer
    {
        (OOVPA*)&D3DDevice_BeginPushBuffer_1_0_5849,
        XTL::EmuIDirect3DDevice8_BeginPushBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_BeginPushBuffer"
        #endif
    },
    // D3DDevice::EndPushBuffer
    {
        (OOVPA*)&D3DDevice_EndPushBuffer_1_0_5849,
        XTL::EmuIDirect3DDevice8_EndPushBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_EndPushBuffer"
        #endif
    },
    // D3DDevice::RunPushBuffer
    {
        (OOVPA*)&D3DDevice_RunPushBuffer_1_0_5659,
        XTL::EmuIDirect3DDevice8_RunPushBuffer,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_RunPushBuffer"
        #endif
    },
    // D3D::CDevice::KickOff. The 5659 body is byte-identical to 5849 and
    // otherwise polls the NV2A writeback register forever under HLE.
    {
        (OOVPA*)&KickOff_1_0_5849,
        XTL::EmuIDirect3DDevice8_KickPushBuffer,
        #ifdef _DEBUG_TRACE
        "D3D::CDevice::KickOff"
        #endif
    },
    // D3DDevice::GetPushBufferOffset
    {
        (OOVPA*)&D3DDevice_GetPushBufferOffset_1_0_5849,
        XTL::EmuIDirect3DDevice8_GetPushBufferOffset,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetPushBufferOffset"
        #endif
    },
    // IDirect3DDevice8::GetDisplayFieldStatus
    {
        (OOVPA*)&IDirect3DDevice8_GetDisplayFieldStatus_1_0_5659,
        XTL::EmuIDirect3DDevice8_GetDisplayFieldStatus,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetDisplayFieldStatus"
        #endif
    },
    // D3DDevice::InsertFence (XRef chain: SetFence -> InsertFence)
    {
        (OOVPA*)&SetFence_1_0_5659,
        XTL::EmuIDirect3DDevice8_InsertFence,
        #ifdef _DEBUG_TRACE
        "SetFence (XRef save)"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_InsertFence_1_0_5659,
        XTL::EmuIDirect3DDevice8_InsertFence,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_InsertFence"
        #endif
    },
    // D3DDevice::BlockOnFence (XRef chain: BlockOnTime -> BlockOnFence)
    {
        (OOVPA*)&BlockOnTime_1_0_5659,
        XTL::EmuIDirect3DDevice8_BlockOnFence,
        #ifdef _DEBUG_TRACE
        "BlockOnTime (XRef save)"
        #endif
    },
    {
        (OOVPA*)&D3DDevice_BlockOnFence_1_0_5659,
        XTL::EmuIDirect3DDevice8_BlockOnFence,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_BlockOnFence"
        #endif
    },
    // IDirect3D8::CreateDevice
    {
        (OOVPA*)&IDirect3D8_CreateDevice_1_0_5558,
        XTL::EmuIDirect3D8_CreateDevice,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3D8_CreateDevice"
        #endif
    },
    // IDirect3D8::GetAdapterDisplayMode
    {
        (OOVPA*)&IDirect3D8_GetAdapterDisplayMode_1_0_5849,
        XTL::EmuIDirect3D8_GetAdapterDisplayMode,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3D8_GetAdapterDisplayMode"
        #endif
    },
    // IDirect3D8::KickOffAndWaitForIdle
    {
        (OOVPA*)&IDirect3D8_KickOffAndWaitForIdle_1_0_5558,
        XTL::EmuIDirect3D8_KickOffAndWaitForIdle,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3D8_KickOffAndWaitForIdle"
        #endif
    },
    // IDirect3DDevice8::GetBackBuffer2
    {
        (OOVPA*)&IDirect3DDevice8_GetBackBuffer2_1_0_5558,
        XTL::EmuIDirect3DDevice8_GetBackBuffer2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetBackBuffer2"
        #endif
    },
    // IDirect3DDevice8::SetViewport
    {
        (OOVPA*)&IDirect3DDevice8_SetViewport_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetViewport,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetViewport"
        #endif
    },
    // IDirect3DDevice8::SetShaderConstantMode
    {
        (OOVPA*)&IDirect3DDevice8_SetShaderConstantMode_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetShaderConstantMode,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetShaderConstantMode"
        #endif
    },
    // IDirect3DDevice8::GetRenderTarget2
    {
        (OOVPA*)&IDirect3DDevice8_GetRenderTarget2_1_0_5849,
        XTL::EmuIDirect3DDevice8_GetRenderTarget2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetRenderTarget2"
        #endif
    },
    // IDirect3DDevice8::GetDepthStencilSurface2
    {
        (OOVPA*)&IDirect3DDevice8_GetDepthStencilSurface2_1_0_5558,
        XTL::EmuIDirect3DDevice8_GetDepthStencilSurface2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetDepthStencilSurface2"
        #endif
    },
    // IDirect3DDevice8::GetTile
    {
        (OOVPA*)&IDirect3DDevice8_GetTile_1_0_5849,
        XTL::EmuIDirect3DDevice8_GetTile,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetTile"
        #endif
    },
    // IDirect3DDevice8::SetTileNoWait
    {
        (OOVPA*)&IDirect3DDevice8_SetTileNoWait_1_0_5558,
        XTL::EmuIDirect3DDevice8_SetTileNoWait,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetTileNoWait"
        #endif
    },
    // IDirect3DDevice8::CreateVertexShader (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_CreateVertexShader_1_0_5849,
        XTL::EmuIDirect3DDevice8_CreateVertexShader,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_CreateVertexShader"
        #endif
    },
    // IDirect3DDevice8::SetVertexShaderConstant1
    {
        (OOVPA*)&IDirect3DDevice8_SetVertexShaderConstant1_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetVertexShaderConstant1,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVertexShaderConstant1"
        #endif
    },
    // IDirect3DDevice8::SetVertexShaderConstant4
    {
        (OOVPA*)&IDirect3DDevice8_SetVertexShaderConstant4_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetVertexShaderConstant4,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVertexShaderConstant4"
        #endif
    },
    // IDirect3DDevice8::CreatePixelShader (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_CreatePixelShader_1_0_5849,
        XTL::EmuIDirect3DDevice8_CreatePixelShader,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_CreatePixelShader"
        #endif
    },
    // IDirect3DDevice8::SetPixelShader
    {
        (OOVPA*)&IDirect3DDevice8_SetPixelShader_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetPixelShader,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetPixelShader"
        #endif
    },
    // IDirect3DDevice8::CreateTexture2
    {
        (OOVPA*)&IDirect3DDevice8_CreateTexture2_1_0_5849,
        XTL::EmuIDirect3DDevice8_CreateTexture2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_CreateTexture2"
        #endif
    },
    // IDirect3DDevice8::SetIndices (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetIndices_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetIndices,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetIndices"
        #endif
    },
    // IDirect3DDevice8::SetTexture (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetTexture_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetTexture,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetTexture"
        #endif
    },
    // IDirect3DDevice8::GetDisplayMode
    {
        (OOVPA*)&IDirect3DDevice8_GetDisplayMode_1_0_5558,
        XTL::EmuIDirect3DDevice8_GetDisplayMode,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetDisplayMode"
        #endif
    },
    // IDirect3DDevice8::Clear
    {
        (OOVPA*)&IDirect3DDevice8_Clear_1_0_5849,
        XTL::EmuIDirect3DDevice8_Clear,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_Clear"
        #endif
    },
    // IDirect3DDevice8::Swap
    {
        (OOVPA*)&IDirect3DDevice8_Swap_1_0_5659,
        XTL::EmuIDirect3DDevice8_Swap,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_Swap"
        #endif
    },
    // IDirect3DDevice8::SetRenderTarget
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderTarget_1_0_5558,
        XTL::EmuIDirect3DDevice8_SetRenderTarget,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderTarget"
        #endif
    },
    // IDirect3DDevice8::MakeSpace
    {
        (OOVPA*)&IDirect3DDevice8_MakeSpace_1_0_5849,
        XTL::EmuIDirect3DDevice8_MakeSpace,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_MakeSpace"
        #endif
    },
    // IDirect3DDevice8::Begin
    {
        (OOVPA*)&IDirect3DDevice8_Begin_1_0_5849,
        XTL::EmuIDirect3DDevice8_Begin,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_Begin"
        #endif
    },
    // IDirect3DDevice8::SetVertexData2f
    {
        (OOVPA*)&IDirect3DDevice8_SetVertexData2f_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetVertexData2f,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVertexData2f"
        #endif
    },
    // IDirect3DDevice8::SetVertexData4f
    {
        (OOVPA*)&IDirect3DDevice8_SetVertexData4f_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetVertexData4f,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVertexData4f"
        #endif
    },
    // IDirect3DDevice8::SetVertexDataColor
    {
        (OOVPA*)&IDirect3DDevice8_SetVertexDataColor_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetVertexDataColor,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVertexDataColor"
        #endif
    },
    // IDirect3DDevice8::End
    {
        (OOVPA*)&IDirect3DDevice8_End_1_0_5849,
        XTL::EmuIDirect3DDevice8_End,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_End"
        #endif
    },
    // IDirect3DDevice8::EnableOverlay (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_EnableOverlay_1_0_5558,
        XTL::EmuIDirect3DDevice8_EnableOverlay,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_EnableOverlay"
        #endif
    },
    // IDirect3DDevice8::CreateVertexBuffer2
    {
        (OOVPA*)&IDirect3DDevice8_CreateVertexBuffer2_1_0_5849,
        XTL::EmuIDirect3DDevice8_CreateVertexBuffer2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_CreateVertexBuffer2"
        #endif
    },
    // IDirect3DDevice8::UpdateOverlay
    {
        (OOVPA*)&IDirect3DDevice8_UpdateOverlay_1_0_5849,
        XTL::EmuIDirect3DDevice8_UpdateOverlay,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_UpdateOverlay"
        #endif
    },
    // IDirect3DDevice8::GetOverlayUpdateStatus
    {
        (OOVPA*)&IDirect3DDevice8_GetOverlayUpdateStatus_1_0_5849,
        XTL::EmuIDirect3DDevice8_GetOverlayUpdateStatus,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetOverlayUpdateStatus"
        #endif
    },
    // IDirect3DDevice8::BlockUntilVerticalBlank
    {
        (OOVPA*)&IDirect3DDevice8_BlockUntilVerticalBlank_1_0_5849,
        XTL::EmuIDirect3DDevice8_BlockUntilVerticalBlank,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_BlockUntilVerticalBlank"
        #endif
    },
    // IDirect3DDevice8::SetVerticalBlankCallback
    {
        (OOVPA*)&IDirect3DDevice8_SetVerticalBlankCallback_1_0_5558,
        XTL::EmuIDirect3DDevice8_SetVerticalBlankCallback,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVerticalBlankCallback"
        #endif
    },
    // IDirect3DDevice8::SetTextureState_TexCoordIndex
    {
        (OOVPA*)&IDirect3DDevice8_SetTextureState_TexCoordIndex_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetTextureState_TexCoordIndex,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetTextureState_TexCoordIndex"
        #endif
    },
    // IDirect3DDevice8::SetTextureState_BorderColor
    {
        (OOVPA*)&IDirect3DDevice8_SetTextureState_BorderColor_1_0_5659,
        XTL::EmuIDirect3DDevice8_SetTextureState_BorderColor,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetTextureState_BorderColor"
        #endif
    },
    // IDirect3DDevice8::BeginVisibilityTest
    {
        (OOVPA*)&IDirect3DDevice8_BeginVisibilityTest_1_0_5659,
        XTL::EmuIDirect3DDevice8_BeginVisibilityTest,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_BeginVisibilityTest"
        #endif
    },
    // IDirect3DDevice8::EndVisibilityTest
    {
        (OOVPA*)&IDirect3DDevice8_EndVisibilityTest_1_0_5659,
        XTL::EmuIDirect3DDevice8_EndVisibilityTest,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_EndVisibilityTest"
        #endif
    },
    // IDirect3DDevice8::GetVisibilityTestResult
    {
        (OOVPA*)&IDirect3DDevice8_GetVisibilityTestResult_1_0_5659,
        XTL::EmuIDirect3DDevice8_GetVisibilityTestResult,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetVisibilityTestResult"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_CullMode (* unchanged since 4134 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_CullMode_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_CullMode,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_CullMode"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_NormalizeNormals
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_NormalizeNormals_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_NormalizeNormals,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_NormalizeNormals"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_TextureFactor (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_TextureFactor_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_TextureFactor,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_TextureFactor"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_ZBias
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_ZBias_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_ZBias,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_ZBias"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_EdgeAntiAlias (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_EdgeAntiAlias_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_EdgeAntiAlias"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_FillMode (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_FillMode_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_FillMode,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_FillMode"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_FogColor (* unchanged since 4134 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_FogColor_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_FogColor,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_FogColor"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_Dxt1NoiseEnable
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_Dxt1NoiseEnable_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_Dxt1NoiseEnable"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_Simple (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_Simple_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_Simple,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_Simple"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_ZEnable
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_ZEnable_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_ZEnable,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_ZEnable"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_StencilEnable (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_StencilEnable_1_0_5558,
        XTL::EmuIDirect3DDevice8_SetRenderState_StencilEnable,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_StencilEnable"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_MultiSampleAntiAlias
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_MultiSampleAntiAlias_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_MultiSampleAntiAlias"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_ShadowFunc
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_ShadowFunc_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_ShadowFunc,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_ShadowFunc"
        #endif
    },
    // IDirect3DDevice8::SetRenderState_YuvEnable
    {
        (OOVPA*)&IDirect3DDevice8_SetRenderState_YuvEnable_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetRenderState_YuvEnable,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetRenderState_YuvEnable"
        #endif
    },
    // IDirect3DDevice8::SetTransform (* unchanged since 4134 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetTransform_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetTransform,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetTransform"
        #endif
    },
    // IDirect3DDevice8::SetGammaRamp
    {
        (OOVPA*)&D3DDevice_SetGammaRamp_1_0_5659,
        XTL::EmuIDirect3DDevice8_SetGammaRamp,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetGammaRamp"
        #endif
    },
    // IDirect3DDevice8::GetTransform (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_GetTransform_1_0_5849,
        XTL::EmuIDirect3DDevice8_GetTransform,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_GetTransform"
        #endif
    },
    // IDirect3DDevice8::SetStreamSource (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetStreamSource_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetStreamSource,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetStreamSource"
        #endif
    },
    // IDirect3DDevice8::SetVertexShader (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_SetVertexShader_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetVertexShader,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetVertexShader"
        #endif
    },
    // IDirect3DDevice8::DrawVertices (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DDevice8_DrawVertices_1_0_5849,
        XTL::EmuIDirect3DDevice8_DrawVertices,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_DrawVertices"
        #endif
    },
    // IDirect3DDevice8::DrawVerticesUP
    {
        (OOVPA*)&IDirect3DDevice8_DrawVerticesUP_1_0_5849,
        XTL::EmuIDirect3DDevice8_DrawVerticesUP,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_DrawVerticesUP"
        #endif
    },
    // IDirect3DDevice8::SetLight
    {
        (OOVPA*)&IDirect3DDevice8_SetLight_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetLight,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetLight"
        #endif
    },
    // IDirect3DDevice8::DrawIndexedVertices
    {
        (OOVPA*)&IDirect3DDevice8_DrawIndexedVertices_1_0_5849,
        XTL::EmuIDirect3DDevice8_DrawIndexedVertices,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_DrawIndexedVertices"
        #endif
    },
    // IDirect3DDevice8::DrawIndexedVerticesUP
    {
        (OOVPA*)&IDirect3DDevice8_DrawIndexedVerticesUP_1_0_5659,
        XTL::EmuIDirect3DDevice8_DrawIndexedVerticesUP,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_DrawIndexedVerticesUP"
        #endif
    },
    // IDirect3DDevice8::SetMaterial
    {
        (OOVPA*)&IDirect3DDevice8_SetMaterial_1_0_5849,
        XTL::EmuIDirect3DDevice8_SetMaterial,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_SetMaterial"
        #endif
    },
    // IDirect3DDevice8::LightEnable
    {
        (OOVPA*)&IDirect3DDevice8_LightEnable_1_0_5849,
        XTL::EmuIDirect3DDevice8_LightEnable,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DDevice8_LightEnable"
        #endif
    },
    // IDirect3DVertexBuffer8::Lock2
    {
        (OOVPA*)&IDirect3DVertexBuffer8_Lock2_1_0_5558,
        XTL::EmuIDirect3DVertexBuffer8_Lock2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DVertexBuffer8_Lock2"
        #endif
    },
    // IDirect3DResource8::Register (* unchanged since 3925 *)
    {
        (OOVPA*)&IDirect3DResource8_Register_1_0_5558,
        XTL::EmuIDirect3DResource8_Register,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DResource8_Register"
        #endif
    },
    // IDirect3DResource8::Release (* unchanged since 3925 *)
    {
        (OOVPA*)&IDirect3DResource8_Release_1_0_5558,
        XTL::EmuIDirect3DResource8_Release,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DResource8_Release"
        #endif
    },
    // IDirect3DResource8::IsBusy (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DResource8_IsBusy_1_0_5558,
        XTL::EmuIDirect3DResource8_IsBusy,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DResource8_IsBusy"
        #endif
    },
    // Get2DSurfaceDesc
    {
        (OOVPA*)&Get2DSurfaceDesc_1_0_5659,
        XTL::EmuGet2DSurfaceDesc,
        #ifdef _DEBUG_TRACE
        "EmuGet2DSurfaceDesc"
        #endif
    },
    // IDirect3DSurface8::GetDesc (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DSurface8_GetDesc_1_0_5558,
        XTL::EmuIDirect3DSurface8_GetDesc,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DSurface8_GetDesc"
        #endif
    },
    // IDirect3DSurface8::LockRect (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DSurface8_LockRect_1_0_5558,
        XTL::EmuIDirect3DSurface8_LockRect,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DSurface8_LockRect"
        #endif
    },
    // IDirect3DBaseTexture8::GetLevelCount (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirect3DBaseTexture8_GetLevelCount_1_0_5558,
        XTL::EmuIDirect3DBaseTexture8_GetLevelCount,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DBaseTexture8_GetLevelCount"
        #endif
    },
    // IDirect3DTexture8::GetSurfaceLevel2
    {
        (OOVPA*)&IDirect3DTexture8_GetSurfaceLevel2_1_0_5558,
        XTL::EmuIDirect3DTexture8_GetSurfaceLevel2,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DTexture8_GetSurfaceLevel2"
        #endif
    },
    // IDirect3DTexture8::LockRect (* unchanged since 3925 *)
    {
        (OOVPA*)&IDirect3DTexture8_LockRect_1_0_5558,
        XTL::EmuIDirect3DTexture8_LockRect,
        #ifdef _DEBUG_TRACE
        "EmuIDirect3DTexture8_LockRect"
        #endif
    },
};

uint32 D3D8_1_0_5659_SIZE = sizeof(D3D8_1_0_5659);
