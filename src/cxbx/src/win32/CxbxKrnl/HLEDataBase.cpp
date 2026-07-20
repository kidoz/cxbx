// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   cxbx->win32->cxbxkrnl->HLEDataBase.cpp
// *
// *  This file is part of the cxbx project.
// *
// *  cxbx and cxbe are free software; you can redistribute them
// *  and/or modify them under the terms of the GNU General Public
// *  License as published by the Free Software Foundation; either
// *  version 2 of the license, or (at your option) any later version.
// *
// *  This program is distributed in the hope that it will be useful,
// *  but WITHOUT ANY WARRANTY; without even the implied warranty of
// *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// *  GNU General Public License for more details.
// *
// *  You should have recieved a copy of the GNU General Public License
// *  along with this program; see the file LICENSE.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************
#define _CXBXKRNL_INTERNAL
#define _XBOXKRNL_LOCAL_

#undef FIELD_OFFSET     // prevent macro redefinition warnings
#define POINTER_64 __ptr64

#include <windows.h>

#include "Emu.h"

// ******************************************************************
// * prevent name collisions
// ******************************************************************
namespace XTL
{
    #include "EmuXTL.h"
};

#include "HLEDataBase.h"

#include "Xapi.1.0.3911.inl"
#include "Xapi.1.0.4034.inl"
#include "Xapi.1.0.4134.inl"
#include "Xapi.1.0.4361.inl"
#include "Xapi.1.0.4627.inl"
#include "Xapi.1.0.5233.inl"
#include "Xapi.1.0.5849.inl"
#include "Xapi.1.0.5659.inl"
#include "D3D8.1.0.3925.inl"
#include "D3D8.1.0.4034.inl"
#include "D3D8.1.0.4134.inl"
#include "D3D8.1.0.4361.inl"
#include "D3D8.1.0.4627.inl"
#include "D3D8.1.0.5558.inl"
#include "D3D8.1.0.5344.inl"
#include "D3D8.1.0.5849.inl"
#include "D3D8.1.0.5659.inl"
#include "D3D8.1.0.5788.inl"
#include "D3D8.1.0.3911.inl"
#include "D3D8.1.0.5233.inl"
#include "D3D8D.1.0.5849.inl"
#include "D3D8I.1.0.5849.inl"
#include "DSound.1.0.3936.inl"
#include "DSound.1.0.4361.inl"
#include "DSound.1.0.4627.inl"
#include "DSound.1.0.5849.inl"
#include "DSound.1.0.5933.inl"
#include "DSound.1.0.5233.inl"
#include "DSound.1.0.5344.inl"
#include "DSound.1.0.5558.inl"
#include "DSound.1.0.5659.inl"
#include "DSound.1.0.5788.inl"
#include "DSound.1.0.3911.inl"
#include "Xact.1.0.5849.inl"
#include "XG.1.0.4361.inl"
#include "XG.1.0.4627.inl"
#include "XG.1.0.5659.inl"
#include "XG.1.0.5849.inl"
#include "XNet.1.0.3911.inl"
#include "XOnline.1.0.4361.inl"
#include "XOnline.1.0.5849.inl"

// ******************************************************************
// * HLEDataBase
// ******************************************************************
HLEData HLEDataBase[] =
{
    // Xapilib Version 1.0.3911
    {
        "XAPILIB",
        1, 0, 3911,
        XAPI_1_0_3911,
        XAPI_1_0_3911_SIZE
    },
    // Xapilib Version 1.0.4034
    {
        "XAPILIB",
        1, 0, 4034,
        XAPI_1_0_4034,
        XAPI_1_0_4034_SIZE
    },
    // Xapilib Version 1.0.4134
    {
        "XAPILIB",
        1, 0, 4134,
        XAPI_1_0_4134,
        XAPI_1_0_4134_SIZE
    },
    // Xapilib Version 1.0.4361
    {
        "XAPILIB",
        1, 0, 4361,
        XAPI_1_0_4361,
        XAPI_1_0_4361_SIZE
    },
    // Xapilib Version 1.0.4627
    {
        "XAPILIB",
        1, 0, 4627,
        XAPI_1_0_4627,
        XAPI_1_0_4627_SIZE
    },
    // Xapilib Version 1.0.5233
    {
        "XAPILIB",
        1, 0, 5233,
        XAPI_1_0_5233,
        XAPI_1_0_5233_SIZE
    },
    // Xapilib Version 1.0.5659
    {
        "XAPILIB",
        1, 0, 5659,
        XAPI_1_0_5659,
        XAPI_1_0_5659_SIZE
    },
    // Xapilib Version 1.0.5849 (generated from XDK 5849 xapilib.lib; the
    // XInput/device family only -- enables headless input via
    // CXBX_INPUT_STATE. XInitDevices deliberately unhooked, see the .inl.)
    {
        "XAPILIB",
        1, 0, 5849,
        XAPI_1_0_5849,
        XAPI_1_0_5849_SIZE
    },
    // Xapilib Version 1.0.5933 (NestopiaX 1.3): adjacent build, reuse 5849 table
    {
        "XAPILIB",
        1, 0, 5933,
        XAPI_1_0_5849,
        XAPI_1_0_5849_SIZE
    },
    // D3D8 Version 1.0.3911 (early retail era, generated from XDK 3911 d3d8.lib)
    {
        "D3D8",
        1, 0, 3911,
        D3D8_1_0_3911,
        D3D8_1_0_3911_SIZE
    },
    // D3D8 Version 1.0.3925
    {
        "D3D8",
        1, 0, 3925,
        D3D8_1_0_3925,
        D3D8_1_0_3925_SIZE
    },
    // D3D8 Version 1.0.4034
    {
        "D3D8",
        1, 0, 4034,
        D3D8_1_0_4034,
        D3D8_1_0_4034_SIZE
    },
    // D3D8 Version 1.0.4134
    {
        "D3D8",
        1, 0, 4134,
        D3D8_1_0_4134,
        D3D8_1_0_4134_SIZE
    },
    // D3D8 Version 1.0.4361
    {
        "D3D8",
        1, 0, 4361,
        D3D8_1_0_4361,
        D3D8_1_0_4361_SIZE
    },
    // D3D8 Version 1.0.4627
    {
        "D3D8",
        1, 0, 4627,
        D3D8_1_0_4627,
        D3D8_1_0_4627_SIZE
    },
    // D3D8 Version 1.0.5233
    {
        "D3D8",
        1, 0, 5233,
        D3D8_1_0_5233,
        D3D8_1_0_5233_SIZE
    },
    // D3D8 Version 1.0.5344 (generated from XDK 5344 d3d8.lib; fills the
    // mid-cycle gap between 4627 and 5558 that many 2003-era titles link)
    {
        "D3D8",
        1, 0, 5344,
        D3D8_1_0_5344,
        D3D8_1_0_5344_SIZE
    },
    // D3D8 Version 1.0.5558 (generated from XDK 5558 d3d8.lib -- the
    // other/xbox-sdks archive; unlocks FCEUltra's HLE route, which was
    // previously native-only and stalled on the PFIFO GET-writeback wait)
    {
        "D3D8",
        1, 0, 5558,
        D3D8_1_0_5558,
        D3D8_1_0_5558_SIZE
    },
    // D3D8 Version 1.0.5659 (generated from XDK 5659 d3d8.lib; adjacent to 5849)
    {
        "D3D8",
        1, 0, 5659,
        D3D8_1_0_5659,
        D3D8_1_0_5659_SIZE
    },
    // D3D8 Version 1.0.5788 (generated from XDK 5788 d3d8.lib; adjacent to 5849)
    {
        "D3D8",
        1, 0, 5788,
        D3D8_1_0_5788,
        D3D8_1_0_5788_SIZE
    },
    // D3D8 Version 1.0.5849 (generated from XDK 5849 d3d8.lib; unlocks z26x + 5849 samples)
    {
        "D3D8",
        1, 0, 5849,
        D3D8_1_0_5849,
        D3D8_1_0_5849_SIZE
    },
    // D3D8 debug library Version 1.0.5849
    {
        "D3D8D",
        1, 0, 5849,
        D3D8D_1_0_5849,
        D3D8D_1_0_5849_SIZE
    },
    // D3D8 profile library Version 1.0.5849
    {
        "D3D8I",
        1, 0, 5849,
        D3D8I_1_0_5849,
        D3D8I_1_0_5849_SIZE
    },
    // D3D8 Version 1.0.5933 (NestopiaX 1.3): adjacent build to 5849 -- reuse its
    // table; each OOVPA still has to byte-match, so only genuinely unchanged
    // functions resolve.
    {
        "D3D8",
        1, 0, 5933,
        D3D8_1_0_5849,
        D3D8_1_0_5849_SIZE
    },
    // DSound Version 1.0.3936
    {
        "DSOUND",
        1, 0, 3936,
        DSound_1_0_3936,
        DSound_1_0_3936_SIZE
    },
    // DSound Version 1.0.4361
    {
        "DSOUND",
        1, 0, 4361,
        DSound_1_0_4361,
        DSound_1_0_4361_SIZE
    },
    // DSound Version 1.0.4627
    {
        "DSOUND",
        1, 0, 4627,
        DSound_1_0_4627,
        DSound_1_0_4627_SIZE
    },
    // DSound Version 1.0.5233
    {
        "DSOUND",
        1, 0, 5233,
        DSound_1_0_5233,
        DSound_1_0_5233_SIZE
    },
    // DSound Version 1.0.3911 (generated from XDK 3911 dsound.lib)
    {
        "DSOUND",
        1, 0, 3911,
        DSound_1_0_3911,
        DSound_1_0_3911_SIZE
    },
    // DSound Version 1.0.5344 (generated from XDK 5344 dsound.lib)
    {
        "DSOUND",
        1, 0, 5344,
        DSound_1_0_5344,
        DSound_1_0_5344_SIZE
    },
    // DSound Version 1.0.5558 (generated from XDK 5558 dsound.lib)
    {
        "DSOUND",
        1, 0, 5558,
        DSound_1_0_5558,
        DSound_1_0_5558_SIZE
    },
    // DSound Version 1.0.5659 (generated from XDK 5659 dsound.lib)
    {
        "DSOUND",
        1, 0, 5659,
        DSound_1_0_5659,
        DSound_1_0_5659_SIZE
    },
    // DSound Version 1.0.5788 (generated from XDK 5788 dsound.lib)
    {
        "DSOUND",
        1, 0, 5788,
        DSound_1_0_5788,
        DSound_1_0_5788_SIZE
    },
    // DSound Version 1.0.5849 (generated from XDK 5849 dsound.lib; the
    // create/setter/buffer-method surface plus DoWork -- see
    // DSound.1.0.5849.inl).
    {
        "DSOUND",
        1, 0, 5849,
        DSound_1_0_5849,
        DSound_1_0_5849_SIZE
    },
    // DSound Version 1.0.5933: exact static-buffer lifecycle signatures from
    // the 5933 SDK. This keeps NestopiaX's PCM ring entirely inside HLE state.
    {
        "DSOUND",
        1, 0, 5933,
        DSound_1_0_5933,
        DSound_1_0_5933_SIZE
    },
    // XACT engine Version 1.0.5849
    {
        "XACTENG",
        1, 0, 5849,
        XACTENG_1_0_5849,
        XACTENG_1_0_5849_SIZE
    },
    // XG Version 1.0.4361
    {
        "XGRAPHC",
        1, 0, 4361,
        XG_1_0_4361,
        XG_1_0_4361_SIZE
    },
    // XG Version 1.0.4627
    {
        "XGRAPHC",
        1, 0, 4627,
        XG_1_0_4627,
        XG_1_0_4627_SIZE
    },
    // XG Version 1.0.5659
    {
        "XGRAPHC",
        1, 0, 5659,
        XG_1_0_5659,
        XG_1_0_5659_SIZE
    },
    // XG Version 1.0.5849 (generated from XDK 5849 xgraphics.lib)
    {
        "XGRAPHC",
        1, 0, 5849,
        XG_1_0_5849,
        XG_1_0_5849_SIZE
    },
    // XG LTCG Version 1.0.5849
    {
        "XGRAPHCL",
        1, 0, 5849,
        XG_LTCG_1_0_5849,
        XG_LTCG_1_0_5849_SIZE
    },
    // XG Version 1.0.5933 (NestopiaX 1.3): adjacent build to 5849 -- reuse its
    // table; the OOVPA byte-matches, so only genuinely unchanged functions
    // resolve. XGRAPHC is a stateless utility library, so a partial hook is
    // safe (unlike the DSOUND 5933 reuse above).
    {
        "XGRAPHC",
        1, 0, 5933,
        XG_1_0_5849,
        XG_1_0_5849_SIZE
    },
    // XNet Version 1.0.3911
    {
        "XNETS",
        1, 0, 3911,
        XNet_1_0_3911,
        XNet_1_0_3911_SIZE
    },
    // XOnline Version 1.0.4361
    {
        "XONLINE",
        1, 0, 4361,
        XOnline_1_0_4361,
        XOnline_1_0_4361_SIZE
    },
    // XOnline Version 1.0.5849
    {
        "XONLINE",
        1, 0, 5849,
        XOnline_1_0_5849,
        XOnline_1_0_5849_SIZE
    },
    // Secure XOnline Version 1.0.5849. XNetStartup and WSAStartup have the
    // same bodies in the secure and non-secure archives for this build.
    {
        "XONLINES",
        1, 0, 5849,
        XOnline_1_0_5849,
        XOnline_1_0_5849_SIZE
    },
};

// ******************************************************************
// * HLEDataBaseSize
// ******************************************************************
extern uint32 HLEDataBaseSize = sizeof(HLEDataBase);

// ******************************************************************
// * XRefDataBase
// ******************************************************************
extern uint32 XRefDataBase[] = {
    -1, // XREF_XNINIT
    -1, // XREF_FCLOSEDEVICE
    -1, // XREF_CLEARSTATEBLOCKFLAGS
    -1, // XREF_RECORDSTATEBLOCK
    -1, // XREF_SETDISTANCEFACTORA
    -1, // XREF_SETDISTANCEFACTORB
    -1, // XREF_SETROLLOFFFACTORA
    -1, // XREF_SETROLLOFFFACTORB
    -1, // XREF_SETDOPPLERFACTOR
    -1, // XREF_SETBUFFERDATA
    -1, // XREF_SETCURRENTPOSITION
    -1, // XREF_SETCURRENTPOSITION2
    -1, // XREF_GETCURRENTPOSITION
    -1, // XREF_GETCURRENTPOSITION2
    -1, // XREF_DSOUNDPLAY
    -1, // XREF_DSOUNDPLAY2
    -1, // XREF_DSOUNDSTOP
    -1, // XREF_DSOUNDSTOP2
    -1, // XREF_DSSETBUFFERDATA
    -1, // XREF_DSSETBUFFERDATA2
    -1, // XREF_DSCREATESOUNDBUFFER
    -1, // XREF_DSCREATESOUNDSTREAM
    -1, // XREF_DSSTREAMPAUSE
    -1, // XREF_DSSTREAMSETVOLUME
    -1, // XREF_DSSETI3DL2LISTENER
    -1, // XREF_DSSETMIXBINHEADROOMA
    -1, // XREF_DSSETMIXBINHEADROOMB
    -1, // XREF_DSSETPOSITIONA
    -1, // XREF_DSSETPOSITIONB
    -1, // XREF_DSSETVELOCITYA
    -1, // XREF_DSSETVELOCITYB
    -1, // XREF_DSSETALLPARAMETERSA
    -1, // XREF_DSSETALLPARAMETERSB
    -1, // XREF_DSSETHEADROOMA
    -1, // XREF_DSSETI3DL2SOURCE1A (Stream)
    -1, // XREF_DSSETI3DL2SOURCE2A (Buffer)
    -1, // XREF_DSBUFFERSETLOOPREGIONA (Buffer)
    -1, // XREF_DSSTREAMSETMAXDISTANCE1A (Stream)
    -1, // XREF_DSSTREAMSETMAXDISTANCE1B
    -1, // XREF_DSSTREAMSETMAXDISTANCE1C
    -1, // XREF_DSSTREAMSETMINDISTANCE1A (Stream)
    -1, // XREF_DSSTREAMSETMINDISTANCE1B
    -1, // XREF_DSSTREAMSETMINDISTANCE1C
    -1, // XREF_DSSTREAMSETCONEANGLES1A
    -1, // XREF_DSSTREAMSETCONEANGLES1B
    -1, // XREF_DSSTREAMSETCONEOUTSIDEVOLUME1A (Stream)
    -1, // XREF_DSSTREAMSETCONEOUTSIDEVOLUME1B
    -1, // XREF_DSSTREAMSETALLPARAMETERS1A (Stream)
    -1, // XREF_DSSTREAMSETALLPARAMETERS1B
    -1, // XREF_DSSTREAMSETALLPARAMETERS1C
    -1, // XREF_DSSTREAMSETVELOCITY1A (Stream)
    -1, // XREF_DSSTREAMSETVELOCITY1B
    -1, // XREF_DSSTREAMSETVELOCITY1C
    -1, // XREF_DSSTREAMSETCONEORIENTATION1A (Stream)
    -1, // XREF_DSSTREAMSETCONEORIENTATION1B
    -1, // XREF_DSSTREAMSETCONEORIENTATION1C
    -1, // XREF_DSSTREAMSETPOSITION1A (Stream)
    -1, // XREF_DSSTREAMSETPOSITION1B
    -1, // XREF_DSSTREAMSETPOSITION1C
    -1, // XREF_DSSTREAMSETFREQUENCY1A (Stream)
    -1, // XREF_DSSTREAMSETFREQUENCY1B
    -1, // XREF_DSSTREAMSETROLLOFFFACTOR1A (Stream)
    -1, // XREF_DSSTREAMSETROLLOFFFACTOR1B
    -1, // XREF_GET2DSURFACEDESCB
    -1, // XREF_COMMITDEFERREDSETTINGSA
    -1, // XREF_COMMITDEFERREDSETTINGSB
    -1, // XREF_DS5849_CREATESOUNDBUFFER
    -1, // XREF_DS5849_CREATESOUNDSTREAM
    -1, // XREF_DS5849_SETI3DL2LISTENER
    -1, // XREF_DS5849_SETMIXBINHEADROOM
    -1, // XREF_DS5849_SETDISTANCEFACTOR
    -1, // XREF_DS5849_SETROLLOFFFACTOR
    -1, // XREF_DS5849_SETDOPPLERFACTOR
    -1, // XREF_DS5849_SETPOSITION
    -1, // XREF_DS5849_SETVELOCITY
    -1, // XREF_DS5849_COMMITDEFERRED
    -1, // XREF_DS5849_STR_PAUSE_T
    -1, // XREF_DS5849_BUF_SETBUFFERDATA
    -1, // XREF_DS5849_BUF_PLAY_T
    -1, // XREF_DS5849_BUF_PLAY
    -1, // XREF_DS5849_BUF_STOP
    -1, // XREF_DS5849_BUF_SETPLAYREGION
    -1, // XREF_DS5849_BUF_SETLOOPREGION
    -1, // XREF_DS5849_BUF_SETVOLUME_T
    -1, // XREF_DS5849_BUF_SETVOLUME
    -1, // XREF_DS5849_BUF_SETCURRENTPOS_T
    -1, // XREF_DS5849_BUF_SETCURRENTPOS
    -1, // XREF_DS5849_BUF_GETCURRENTPOS
    -1, // XREF_DS5849_BUF_LOCK
    -1, // XREF_DS5849_BUF_SETMIXBINS_T
    -1, // XREF_DS5849_BUF_SETMIXBINS
    -1, // XREF_DS5849_BUF_GETSTATUS_T
    -1, // XREF_DS5849_BUF_GETSTATUS
    -1, // XREF_DS5849_DOWORK
    -1, // XREF_DS5849_STR_FLUSHEX
    -1, // XREF_DS5849_BUF_SETOUTPUTBUFFER_T
    -1, // XREF_DS5849_BUF_SETOUTPUTBUFFER
    -1, // XREF_DS5849_BUF_USE3DVOICEDATA
    -1, // XREF_DS5849_VOICE_SETPITCH
    -1, // XREF_DS5849_MCPX_GETVOICEPROPS
    -1, // XREF_DS5849_VOICE_GETVOICEPROPS
    -1, // XREF_DS5849_BUF_SETFORMAT_T
    -1, // XREF_DS5849_BUF_SETFORMAT
    -1, // XREF_DS5849_BUF_STOPEX
    -1, // XREF_DS5659_STREAM_PAUSE
    -1, // XREF_DS5659_SYNCHPLAYBACK
    -1, // XREF_DS5659_BUFFER_SETOUTPUTBUFFER_T
    -1, // XREF_DS5659_BUFFER_SETOUTPUTBUFFER
    -1, // XREF_DS5659_BUFFER_USE3DVOICEDATA
    -1, // XREF_DS5659_BUFFER_SETFORMAT_T
    -1, // XREF_DS5659_BUFFER_SETFORMAT
    -1, // XREF_DS5659_BUFFER_STOPEX
    -1, // XREF_DS5659_GETOUTPUTLEVELS
    -1, // XREF_DS5659_BUFFER_SETNOTIFICATIONPOSITIONS
    -1, // XREF_XAPI5849_XINPUTCLOSE
    -1, // XREF_D3D5558_MAKESPACE
    -1, // XREF_DS4627_GETCAPS
    -1, // XREF_DS4627_DS_SETPOSITION
    -1, // XREF_DS4627_DS_SETVELOCITY
    -1, // XREF_DS4627_BUF_SETVOLUME_T
    -1, // XREF_DS4627_BUF_SETVOLUME
    -1, // XREF_DS4627_BUF_SETMIXBINS_T_T
    -1, // XREF_DS4627_BUF_SETMIXBINS_T
    -1, // XREF_DS4627_BUF_SETMIXBINS
    -1, // XREF_DS4627_BUF_SETMIXBINVOLUMES_T_T
    -1, // XREF_DS4627_BUF_SETMIXBINVOLUMES_T
    -1, // XREF_DS4627_BUF_SETMIXBINVOLUMES
    -1, // XREF_DS4627_BUF_PLAY_T
    -1, // XREF_DS4627_BUF_PLAY
    -1, // XREF_DS4627_BUF_GETSTATUS_T
    -1, // XREF_DS4627_BUF_GETSTATUS
    -1, // XREF_DS4627_BUF_GETCURPOS
    -1, // XREF_DS4627_BUF_SETCURPOS_T
    -1, // XREF_DS4627_BUF_SETCURPOS
    -1, // XREF_DS4627_BUF_SETFREQUENCY_T
    -1, // XREF_DS4627_BUF_SETFREQUENCY
    -1, // XREF_DS4627_BUF_SETCONEANGLES
    -1, // XREF_DS4627_BUF_SETI3DL2SRC_T
    -1, // XREF_DS4627_BUF_SETI3DL2SRC
    -1, // XREF_DS4627_BUF_STOP
    -1, // XREF_SETFENCE
    -1, // XREF_BLOCKONTIME
    -1, // XREF_KICKOFF
    -1, // XREF_KICKOFFANDWAITFORIDLE
    -1, // XREF_XAPI5233_XINPUTCLOSE
    -1, // XREF_DS5933_CREATESOUNDBUFFER
    -1, // XREF_DS5933_BUF_SETMIXBINS_T
    -1, // XREF_DS5933_BUF_SETMIXBINS
    -1, // XREF_DS5933_BUF_PLAY_T
    -1, // XREF_DS5933_BUF_PLAY
    -1, // XREF_DS5933_BUF_SETVOLUME_T
    -1, // XREF_DS5933_BUF_SETVOLUME
    -1, // XREF_DS5933_BUF_GETCURRENTPOSITION
    -1, // XREF_DS5933_BUF_GETSTATUS_T
    -1, // XREF_DS5933_BUF_GETSTATUS
    -1, // XREF_DS5933_BUF_SETBUFFERDATA
    -1, // XREF_DS5933_BUF_SETCURRENTPOSITION_T
    -1, // XREF_DS5933_BUF_SETCURRENTPOSITION
    -1, // XREF_DS5933_BUF_SETPLAYREGION
    -1, // XREF_DS5933_BUF_SETLOOPREGION
    -1, // XREF_DS5933_BUF_STOPEX_T_T
    -1, // XREF_DS5933_BUF_STOPEX_T
    -1, // XREF_DS5933_BUF_STOPEX
    -1, // XREF_DS5933_BUF_PLAYEX_T
    -1, // XREF_DS5933_BUF_PLAYEX
    -1, // XREF_DS5933_BUF_SETFORMAT_T
    -1, // XREF_DS5933_BUF_SETFORMAT
    -1, // XREF_XACT5849_ENGINE_RELEASE
    -1, // XREF_XACT5849_CREATE_SOUNDBANK
    -1, // XREF_XACT5849_REGISTER_WAVEBANK
    -1, // XREF_XACT5849_SOUNDBANK_ADDREF
    -1, // XREF_XACT5849_SOUNDBANK_DESTRUCTOR
    -1, // XREF_XACT5849_SOUNDBANK_DELETING_DESTRUCTOR
    -1, // XREF_XACT5849_SOUNDBANK_RELEASE
    -1, // XREF_XACT5849_SOUNDBANK_PREPARE
    -1, // XREF_XACT5849_SOUNDBANK_PLAY
    -1, // XREF_XACT5849_REGISTER_NOTIFICATION
    -1, // XREF_XACT5849_UNREGISTER_NOTIFICATION
    -1, // XREF_XACT5849_FLUSH_NOTIFICATION
};

// ******************************************************************
// * track XRef location
// ******************************************************************
extern uint32 UnResolvedXRefs = sizeof(XRefDataBase)/sizeof(uint32);

// ******************************************************************
// * Search Speed Optimization
// ******************************************************************
extern bool bXRefFirstPass = true;
