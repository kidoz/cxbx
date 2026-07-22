// ******************************************************************
// *
// *    .,-:::::    .,::      .::::::::.    .,::      .:
// *  ,;;;'````'    `;;;,  .,;;  ;;;'';;'   `;;;,  .,;; 
// *  [[[             '[[,,[['   [[[__[[\.    '[[,,[['  
// *  $$$              Y$$$P     $$""""Y$$     Y$$$P    
// *  `88bo,__,o,    oP"``"Yo,  _88o,,od8P   oP"``"Yo,  
// *    "YUMMMMMP",m"       "Mm,""YUMMMP" ,m"       "Mm,
// *
// *   Cxbx->Win32->CxbxKrnl->DSound.1.0.4627.cpp
// *
// *  This file is part of the Cxbx project.
// *
// *  Cxbx and Cxbe are free software; you can redistribute them
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
// *  along with this program; see the file COPYING.
// *  If not, write to the Free Software Foundation, Inc.,
// *  59 Temple Place - Suite 330, Bostom, MA 02111-1307, USA.
// *
// *  (c) 2002-2003 Aaron Robinson <caustik@caustik.com>
// *
// *  All rights reserved
// *
// ******************************************************************

// ******************************************************************
// * CDirectSound::CreateSoundBuffer
// ******************************************************************
SOOVPA<14> CDirectSound_CreateSoundBuffer_1_0_4627 =
{
    0,  // Large == 0
    14, // Count == 14

    XREF_DSCREATESOUNDBUFFER,   // XRef Is  Saved
    0,                          // XRef Not Used

    {
        // CDirectSound_CreateSoundBuffer+0x23 : mov eax, 0x80004005
        { 0x23, 0xB8 }, // (Offset,Value)-Pair #1
        { 0x24, 0x05 }, // (Offset,Value)-Pair #2
        { 0x25, 0x40 }, // (Offset,Value)-Pair #3
        { 0x27, 0x80 }, // (Offset,Value)-Pair #4

        // CDirectSound_CreateSoundBuffer+0x2A : push 0x24
        { 0x2A, 0x6A }, // (Offset,Value)-Pair #5
        { 0x2B, 0x24 }, // (Offset,Value)-Pair #6

        // CDirectSound_CreateSoundBuffer+0x4A : add esi, 0x7FF8FFF2
        { 0x4A, 0x81 }, // (Offset,Value)-Pair #7
        { 0x4B, 0xE6 }, // (Offset,Value)-Pair #8
        { 0x4C, 0xF2 }, // (Offset,Value)-Pair #9
        { 0x4D, 0xFF }, // (Offset,Value)-Pair #10
        { 0x4E, 0xF8 }, // (Offset,Value)-Pair #11
        { 0x4F, 0x7F }, // (Offset,Value)-Pair #12

        // CDirectSound_CreateSoundBuffer+0x99 : retn 0x10
        { 0x99, 0xC2 }, // (Offset,Value)-Pair #13
        { 0x9A, 0x10 }, // (Offset,Value)-Pair #14
    }
};

// ******************************************************************
// * CDirectSound::SetI3DL2Listener
// ******************************************************************
SOOVPA<12> CDirectSound_SetI3DL2Listener_1_0_4627 =
{
    0,  // Large == 0
    12, // Count == 12

    XREF_DSSETI3DL2LISTENER,    // XRef Is  Saved
    0,                          // XRef Not Used

    {
        // CDirectSound_SetI3DL2Listener+0x3A : mov edi, 0x88780032
        { 0x3A, 0xBF }, // (Offset,Value)-Pair #1
        { 0x3B, 0x32 }, // (Offset,Value)-Pair #2
        { 0x3C, 0x00 }, // (Offset,Value)-Pair #3
        { 0x3D, 0x78 }, // (Offset,Value)-Pair #4
        { 0x3E, 0x88 }, // (Offset,Value)-Pair #5

        // CDirectSound_SetI3DL2Listener+0xA2 : fstp dword ptr[edx+0x94]
        { 0xA2, 0xD9 }, // (Offset,Value)-Pair #6
        { 0xA3, 0x9A }, // (Offset,Value)-Pair #7
        { 0xA4, 0x94 }, // (Offset,Value)-Pair #8

        // CDirectSound_SetI3DL2Listener+0xDC : jnz +0x06
        { 0xDC, 0x75 }, // (Offset,Value)-Pair #9
        { 0xDD, 0x06 }, // (Offset,Value)-Pair #10

        // CDirectSound_SetI3DL2Listener+0xF7 : retn 0x0C
        { 0xF7, 0xC2 }, // (Offset,Value)-Pair #11
        { 0xF8, 0x0C }, // (Offset,Value)-Pair #12
    }
};

// ******************************************************************
// * CDirectSound::SetMixBinHeadroom
// ******************************************************************
SOOVPA<15> CDirectSound_SetMixBinHeadroom_1_0_4627 =
{
    0,  // Large == 0
    15, // Count == 15

    XREF_DSSETMIXBINHEADROOMA,  // XRef Is  Saved
    0,                          // XRef Not Used

    {
        // CDirectSound_SetMixBinHeadroom+0x21 : mov eax, 0x80004005
        { 0x21, 0xB8 }, // (Offset,Value)-Pair #1
        { 0x22, 0x05 }, // (Offset,Value)-Pair #2
        { 0x23, 0x40 }, // (Offset,Value)-Pair #3
        { 0x24, 0x00 }, // (Offset,Value)-Pair #4
        { 0x25, 0x80 }, // (Offset,Value)-Pair #5

        // CDirectSound_SetMixBinHeadroom+0x34 : mov bl, [esp+0x14]
        { 0x34, 0x8A }, // (Offset,Value)-Pair #6
        { 0x35, 0x5C }, // (Offset,Value)-Pair #7
        { 0x36, 0x24 }, // (Offset,Value)-Pair #8
        { 0x37, 0x14 }, // (Offset,Value)-Pair #9

        // CDirectSound_SetMixBinHeadroom+0x39 : mov [edx+ecx+0x14], bl
        { 0x39, 0x88 }, // (Offset,Value)-Pair #10
        { 0x3A, 0x5C }, // (Offset,Value)-Pair #11
        { 0x3B, 0x0A }, // (Offset,Value)-Pair #12
        { 0x3C, 0x14 }, // (Offset,Value)-Pair #13

        // CDirectSound_SetMixBinHeadroom+0x5C : retn 0x0C
        { 0x5C, 0xC2 }, // (Offset,Value)-Pair #14
        { 0x5D, 0x0C }, // (Offset,Value)-Pair #15
    }
};

// ******************************************************************
// * DirectSoundCreateBuffer
// ******************************************************************
SOOVPA<12> DirectSoundCreateBuffer_1_0_4627 =
{
    0,  // Large == 0
    12, // Count == 12

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // DirectSoundCreateBuffer+0x2F : call [CDirectSound::CreateSoundBuffer]
        { 0x2F, XREF_DSCREATESOUNDBUFFER }, // (Offset,Value)-Pair #1

        // DirectSoundCreateBuffer+0x04 : and [ebp-0x04], 0
        { 0x04, 0x83 }, // (Offset,Value)-Pair #2
        { 0x05, 0x65 }, // (Offset,Value)-Pair #3
        { 0x06, 0xFC }, // (Offset,Value)-Pair #4

        // DirectSoundCreateBuffer+0x08 : push ebx; push esi; push edi
        { 0x08, 0x53 }, // (Offset,Value)-Pair #5
        { 0x09, 0x56 }, // (Offset,Value)-Pair #6
        { 0x0A, 0x57 }, // (Offset,Value)-Pair #7

        // DirectSoundCreateBuffer+0x3C : call dword ptr [eax+8]
        { 0x3C, 0xFF }, // (Offset,Value)-Pair #8
        { 0x3D, 0x50 }, // (Offset,Value)-Pair #9
        { 0x3E, 0x08 }, // (Offset,Value)-Pair #10

        // DirectSoundCreateBuffer+0x54 : retn 0x08
        { 0x54, 0xC2 }, // (Offset,Value)-Pair #11
        { 0x55, 0x08 }, // (Offset,Value)-Pair #12
    }
};

// ******************************************************************
// * IDirectSound8_SetI3DL2Listener
// ******************************************************************
SOOVPA<12> IDirectSound8_SetI3DL2Listener_1_0_4627 =
{
    0,  // Large == 0
    12, // Count == 12

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // IDirectSound8_SetI3DL2Listener+0x19 : call [CDirectSound::SetI3DL2Listener]
        { 0x19, XREF_DSSETI3DL2LISTENER }, // (Offset,Value)-Pair #1

        // IDirectSound8_SetI3DL2Listener+0x04 : push [esp+0x0C]
        { 0x04, 0xFF }, // (Offset,Value)-Pair #2
        { 0x05, 0x74 }, // (Offset,Value)-Pair #3
        { 0x06, 0x24 }, // (Offset,Value)-Pair #4
        { 0x07, 0x0C }, // (Offset,Value)-Pair #5

        // IDirectSound8_SetI3DL2Listener+0x0E : add eax, 0xFFFFFFF8
        { 0x0E, 0x83 }, // (Offset,Value)-Pair #6
        { 0x0F, 0xC0 }, // (Offset,Value)-Pair #7
        { 0x10, 0xF8 }, // (Offset,Value)-Pair #8

        // IDirectSound8_SetI3DL2Listener+0x13 : sbb ecx, ecx
        { 0x13, 0x1B }, // (Offset,Value)-Pair #9
        { 0x14, 0xC9 }, // (Offset,Value)-Pair #10

        // IDirectSound8_SetI3DL2Listener+0x15 : and ecx, eax
        { 0x15, 0x23 }, // (Offset,Value)-Pair #11
        { 0x16, 0xC8 }, // (Offset,Value)-Pair #12
    }
};

// ******************************************************************
// * IDirectSound8_SetMixBinHeadroom
// ******************************************************************
SOOVPA<12> IDirectSound8_SetMixBinHeadroom_1_0_4627 =
{
    0,  // Large == 0
    12, // Count == 12

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // IDirectSound8_SetMixBinHeadroom+0x19 : call [CDirectSound::SetMixBinHeadroom]
        { 0x19, XREF_DSSETMIXBINHEADROOMA }, // (Offset,Value)-Pair #1

        // IDirectSound8_SetMixBinHeadroom+0x04 : push [esp+0x0C]
        { 0x04, 0xFF }, // (Offset,Value)-Pair #2
        { 0x05, 0x74 }, // (Offset,Value)-Pair #3
        { 0x06, 0x24 }, // (Offset,Value)-Pair #4
        { 0x07, 0x0C }, // (Offset,Value)-Pair #5

        // IDirectSound8_SetMixBinHeadroom+0x0E : add eax, 0xFFFFFFF8
        { 0x0E, 0x83 }, // (Offset,Value)-Pair #6
        { 0x0F, 0xC0 }, // (Offset,Value)-Pair #7
        { 0x10, 0xF8 }, // (Offset,Value)-Pair #8

        // IDirectSound8_SetMixBinHeadroom+0x13 : sbb ecx, ecx
        { 0x13, 0x1B }, // (Offset,Value)-Pair #9
        { 0x14, 0xC9 }, // (Offset,Value)-Pair #10

        // IDirectSound8_SetMixBinHeadroom+0x15 : and ecx, eax
        { 0x15, 0x23 }, // (Offset,Value)-Pair #11
        { 0x16, 0xC8 }, // (Offset,Value)-Pair #12
    }
};

// ******************************************************************
// * IDirectSound8_CreateSoundBuffer
// ******************************************************************
SOOVPA<12> IDirectSound8_CreateSoundBuffer_1_0_4627 =
{
    0,  // Large == 0
    12, // Count == 12

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // IDirectSound8_CreateSoundBuffer+0x1D : call [CDirectSound::CreateSoundBuffer]
        { 0x1D, XREF_DSCREATESOUNDBUFFER }, // (Offset,Value)-Pair #1

        // IDirectSound8_CreateSoundBuffer+0x04 : mov eax, [esp+8]
        { 0x04, 0x8B }, // (Offset,Value)-Pair #2
        { 0x05, 0x44 }, // (Offset,Value)-Pair #3
        { 0x06, 0x24 }, // (Offset,Value)-Pair #4
        { 0x07, 0x08 }, // (Offset,Value)-Pair #5

        // IDirectSound8_CreateSoundBuffer+0x12 : add eax, 0xFFFFFFF8
        { 0x12, 0x83 }, // (Offset,Value)-Pair #6
        { 0x13, 0xC0 }, // (Offset,Value)-Pair #7
        { 0x14, 0xF8 }, // (Offset,Value)-Pair #8

        // IDirectSound8_CreateSoundBuffer+0x17 : sbb ecx, ecx
        { 0x17, 0x1B }, // (Offset,Value)-Pair #9
        { 0x18, 0xC9 }, // (Offset,Value)-Pair #10

        // IDirectSound8_CreateSoundBuffer+0x21 : retn 0x10
        { 0x21, 0xC2 }, // (Offset,Value)-Pair #11
        { 0x22, 0x10 }, // (Offset,Value)-Pair #12
    }
};

// ******************************************************************
// * CMcpxVoiceClient_SetVolume
// ******************************************************************
SOOVPA<13> CMcpxVoiceClient_SetVolume_1_0_4361 =
{
    0,  // Large == 0
    13, // Count == 13

    XREF_DSSTREAMSETVOLUME, // XRef Is  Saved
    0,                      // XRef Not Used

    {
        // CMcpxVoiceClient_SetVolume+0x2A : lea eax, [ecx+ecx*2]
        { 0x2A, 0x8D }, // (Offset,Value)-Pair #1
        { 0x2B, 0x04 }, // (Offset,Value)-Pair #2
        { 0x2C, 0x49 }, // (Offset,Value)-Pair #3

        // CMcpxVoiceClient_SetVolume+0x45 : movzx edx, word ptr [ecx]
        { 0x45, 0x0F }, // (Offset,Value)-Pair #4
        { 0x46, 0xB7 }, // (Offset,Value)-Pair #5
        { 0x47, 0x11 }, // (Offset,Value)-Pair #6

        // CMcpxVoiceClient_SetVolume+0x6C : mov edx, [ebp+eax*4-0x14]
        { 0x6C, 0x8B }, // (Offset,Value)-Pair #7
        { 0x6D, 0x54 }, // (Offset,Value)-Pair #8
        { 0x6E, 0x85 }, // (Offset,Value)-Pair #9
        { 0x6F, 0xEC }, // (Offset,Value)-Pair #10

        // CMcpxVoiceClient_SetVolume+0x84 : inc eax; inc ecx, inc ecx
        { 0x84, 0x40 }, // (Offset,Value)-Pair #11
        { 0x85, 0x41 }, // (Offset,Value)-Pair #12
        { 0x86, 0x41 }, // (Offset,Value)-Pair #13
    }
};

// ******************************************************************
// * CDirectSoundStream_SetVolume
// ******************************************************************
SOOVPA<11> CDirectSoundStream_SetVolume_1_0_4361 =
{
    0,  // Large == 0
    11, // Count == 11

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // CDirectSoundBuffer_SetBufferData+0x15 : call [CMcpxVoiceClient::SetVolume]
        { 0x15, XREF_DSSTREAMSETVOLUME },  // (Offset,Value)-Pair #1

        // CDirectSoundBuffer_SetBufferData+0x00 : mov ecx, [esp+0x04]
        { 0x00, 0x8B }, // (Offset,Value)-Pair #2
        { 0x01, 0x4C }, // (Offset,Value)-Pair #3
        { 0x02, 0x24 }, // (Offset,Value)-Pair #4
        { 0x03, 0x04 }, // (Offset,Value)-Pair #5

        // CDirectSoundBuffer_SetBufferData+0x0B : sub edx, [eax+0x20]
        { 0x0B, 0x2B }, // (Offset,Value)-Pair #6
        { 0x0C, 0x50 }, // (Offset,Value)-Pair #7
        { 0x0D, 0x20 }, // (Offset,Value)-Pair #8

        // CDirectSoundBuffer_SetBufferData+0x11 : mov ecx, [ecx+0x0C]
        { 0x11, 0x8B }, // (Offset,Value)-Pair #9
        { 0x12, 0x49 }, // (Offset,Value)-Pair #10
        { 0x13, 0x0C }, // (Offset,Value)-Pair #11
    }
};

// ******************************************************************
// * IDirectSound8_Release
// ******************************************************************
SOOVPA<10> IDirectSound8_Release_1_0_4627 =
{
    0,  // Large == 0
    10, // Count == 10

    -1, // XRef Not Saved
    0,  // XRef Not Used

    {
        // IDirectSound8_Release+0x04 : lea ecx, [eax-8]
        { 0x04, 0x8D }, // (Offset,Value)-Pair #1
        { 0x05, 0x48 }, // (Offset,Value)-Pair #2
        { 0x06, 0xF8 }, // (Offset,Value)-Pair #3

        // IDirectSound8_Release+0x07 : neg eax
        { 0x07, 0xF7 }, // (Offset,Value)-Pair #4
        { 0x08, 0xD8 }, // (Offset,Value)-Pair #5

        // IDirectSound8_Release+0x10 : call dword ptr [ecx+8]
        { 0x10, 0xFF }, // (Offset,Value)-Pair #6
        { 0x11, 0x51 }, // (Offset,Value)-Pair #7
        { 0x12, 0x08 }, // (Offset,Value)-Pair #8

        // IDirectSound8_Release+0x13 : retn 0x04
        { 0x13, 0xC2 }, // (Offset,Value)-Pair #9
        { 0x14, 0x04 }, // (Offset,Value)-Pair #10
    }
};

// ******************************************************************
// * CDirectSound::SetDistanceFactorA
// ******************************************************************
SOOVPA<11> CDirectSound_SetDistanceFactorA_1_0_4627 =
{
    0,  // Large == 0
    11, // Count == 11

    XREF_SETDISTANCEFACTORA,// XRef Is Saved
    0,                      // XRef Not Used

    {
        // CDirectSound_SetDistanceFactorA+0x21 : mov eax, 0x80004005
        { 0x21, 0xB8 }, // (Offset,Value)-Pair #1
        { 0x22, 0x05 }, // (Offset,Value)-Pair #2
        { 0x23, 0x40 }, // (Offset,Value)-Pair #3
        { 0x24, 0x00 }, // (Offset,Value)-Pair #4
        { 0x25, 0x80 }, // (Offset,Value)-Pair #5

        // CDirectSound_SetDistanceFactorA+0x39 : or byte ptr[eax+0xA4], 0xE0
        { 0x39, 0x80 }, // (Offset,Value)-Pair #6
        { 0x3A, 0x88 }, // (Offset,Value)-Pair #7
        { 0x3B, 0xA4 }, // (Offset,Value)-Pair #8
        { 0x3F, 0xE0 }, // (Offset,Value)-Pair #9

        // CDirectSound_SetDistanceFactorA+0x4F : jz +0x0B
        { 0x4F, 0x74 }, // (Offset,Value)-Pair #10
        { 0x50, 0x0B }, // (Offset,Value)-Pair #11
    }
};

// ******************************************************************
// * IDirectSound8_SetDistanceFactor
// ******************************************************************
SOOVPA<11> IDirectSound8_SetDistanceFactor_1_0_4627 =
{
    0,  // Large == 0
    11, // Count == 11

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // IDirectSound8_SetDistanceFactor+0x1D : call [CDirectSound::SetDistanceFactor]
        { 0x1D, XREF_SETDISTANCEFACTORA },  // (Offset,Value)-Pair #1

        // IDirectSound8_SetDistanceFactor+0x04 : fld [esp+0x0C]
        { 0x04, 0xD9 }, // (Offset,Value)-Pair #2
        { 0x05, 0x44 }, // (Offset,Value)-Pair #3
        { 0x06, 0x24 }, // (Offset,Value)-Pair #4
        { 0x07, 0x0C }, // (Offset,Value)-Pair #5

        // IDirectSound8_SetDistanceFactor+0x0C : push ecx
        { 0x0C, 0x51 }, // (Offset,Value)-Pair #6

        // IDirectSound8_SetDistanceFactor+0x12 : add eax, 0xFFFFFFF8
        { 0x12, 0x83 }, // (Offset,Value)-Pair #7
        { 0x13, 0xC0 }, // (Offset,Value)-Pair #8
        { 0x14, 0xF8 }, // (Offset,Value)-Pair #9

        // IDirectSound8_SetDistanceFactor+0x21 : retn 0x0C
        { 0x21, 0xC2 }, // (Offset,Value)-Pair #10
        { 0x22, 0x0C }, // (Offset,Value)-Pair #11
    }
};

// ******************************************************************
// * CDirectSound::SetRolloffFactor
// ******************************************************************
SOOVPA<11> CDirectSound_SetRolloffFactor_1_0_4627 =
{
    0,  // Large == 0
    11, // Count == 11

    XREF_SETROLLOFFFACTORA, // XRef Is Saved
    0,                      // XRef Not Used

    {
        // CDirectSound_SetRolloffFactor+0x21 : mov eax, 0x80004005
        { 0x21, 0xB8 }, // (Offset,Value)-Pair #1
        { 0x22, 0x05 }, // (Offset,Value)-Pair #2
        { 0x23, 0x40 }, // (Offset,Value)-Pair #3
        { 0x24, 0x00 }, // (Offset,Value)-Pair #4
        { 0x25, 0x80 }, // (Offset,Value)-Pair #5

        // CDirectSound_SetRolloffFactor+0x39 : or dword ptr[eax+0xA4], 0x04
        { 0x39, 0x83 }, // (Offset,Value)-Pair #6
        { 0x3A, 0x88 }, // (Offset,Value)-Pair #7
        { 0x3B, 0xA4 }, // (Offset,Value)-Pair #8
        { 0x3F, 0x04 }, // (Offset,Value)-Pair #9

        // CDirectSound_SetRolloffFactor+0x4F : jz +0x0B
        { 0x4F, 0x74 }, // (Offset,Value)-Pair #10
        { 0x50, 0x0B }, // (Offset,Value)-Pair #11
    }
};

// ******************************************************************
// * IDirectSound8_SetRolloffFactor
// ******************************************************************
SOOVPA<11> IDirectSound8_SetRolloffFactor_1_0_4627 =
{
    0,  // Large == 0
    11, // Count == 11

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // IDirectSound8_SetRolloffFactor+0x1D : call [CDirectSound::SetRolloffFactor]
        { 0x1D, XREF_SETROLLOFFFACTORA },  // (Offset,Value)-Pair #1

        // IDirectSound8_SetRolloffFactor+0x04 : fld [esp+0x0C]
        { 0x04, 0xD9 }, // (Offset,Value)-Pair #2
        { 0x05, 0x44 }, // (Offset,Value)-Pair #3
        { 0x06, 0x24 }, // (Offset,Value)-Pair #4
        { 0x07, 0x0C }, // (Offset,Value)-Pair #5

        // IDirectSound8_SetRolloffFactor+0x0C : push ecx
        { 0x0C, 0x51 }, // (Offset,Value)-Pair #6

        // IDirectSound8_SetRolloffFactor+0x12 : add eax, 0xFFFFFFF8
        { 0x12, 0x83 }, // (Offset,Value)-Pair #7
        { 0x13, 0xC0 }, // (Offset,Value)-Pair #8
        { 0x14, 0xF8 }, // (Offset,Value)-Pair #9

        // IDirectSound8_SetRolloffFactor+0x21 : retn 0x0C
        { 0x21, 0xC2 }, // (Offset,Value)-Pair #10
        { 0x22, 0x0C }, // (Offset,Value)-Pair #11
    }
};

// ******************************************************************
// * CDirectSound::SetDopplerFactor
// ******************************************************************
SOOVPA<14> CDirectSound_SetDopplerFactor_1_0_4627 =
{
    0,  // Large == 0
    14, // Count == 14

    XREF_SETDOPPLERFACTOR,  // XRef Is Saved
    0,                      // XRef Not Used

    {
        // CDirectSound_SetDopplerFactor+0x21 : mov eax, 0x80004005
        { 0x21, 0xB8 }, // (Offset,Value)-Pair #1
        { 0x22, 0x05 }, // (Offset,Value)-Pair #2
        { 0x23, 0x40 }, // (Offset,Value)-Pair #3
        { 0x24, 0x00 }, // (Offset,Value)-Pair #4
        { 0x25, 0x80 }, // (Offset,Value)-Pair #5

        // CDirectSound_SetDopplerFactor+0x33 : mov [eax+0x70], edx
        { 0x33, 0x89 }, // (Offset,Value)-Pair #6
        { 0x34, 0x50 }, // (Offset,Value)-Pair #7
        { 0x35, 0x70 }, // (Offset,Value)-Pair #8

        // CDirectSound_SetDopplerFactor+0x39 : or byte ptr[eax+0xA4], 0x80
        { 0x39, 0x80 }, // (Offset,Value)-Pair #9
        { 0x3A, 0x88 }, // (Offset,Value)-Pair #10
        { 0x3B, 0xA4 }, // (Offset,Value)-Pair #11
        { 0x3F, 0x80 }, // (Offset,Value)-Pair #12

        // CDirectSound_SetDopplerFactor+0x4F : jz +0x0B
        { 0x4F, 0x74 }, // (Offset,Value)-Pair #13
        { 0x50, 0x0B }, // (Offset,Value)-Pair #14
    }
};

// ******************************************************************
// * IDirectSound8_SetDopplerFactor
// ******************************************************************
SOOVPA<11> IDirectSound8_SetDopplerFactor_1_0_4627 =
{
    0,  // Large == 0
    11, // Count == 11

    -1, // XRef Not Saved
    1,  // XRef Is  Used

    {
        // IDirectSound8_SetDopplerFactor+0x1D : call [CDirectSound::SetDopplerFactor]
        { 0x1D, XREF_SETDOPPLERFACTOR},  // (Offset,Value)-Pair #1

        // IDirectSound8_SetDopplerFactor+0x04 : fld [esp+0x0C]
        { 0x04, 0xD9 }, // (Offset,Value)-Pair #2
        { 0x05, 0x44 }, // (Offset,Value)-Pair #3
        { 0x06, 0x24 }, // (Offset,Value)-Pair #4
        { 0x07, 0x0C }, // (Offset,Value)-Pair #5

        // IDirectSound8_SetDopplerFactor+0x0C : push ecx
        { 0x0C, 0x51 }, // (Offset,Value)-Pair #6

        // IDirectSound8_SetDopplerFactor+0x12 : add eax, 0xFFFFFFF8
        { 0x12, 0x83 }, // (Offset,Value)-Pair #7
        { 0x13, 0xC0 }, // (Offset,Value)-Pair #8
        { 0x14, 0xF8 }, // (Offset,Value)-Pair #9

        // IDirectSound8_SetDopplerFactor+0x21 : retn 0x0C
        { 0x21, 0xC2 }, // (Offset,Value)-Pair #10
        { 0x22, 0x0C }, // (Offset,Value)-Pair #11
    }
};

// ******************************************************************
// * CDirectSound::CommitDeferredSettings
// ******************************************************************
SOOVPA<11> CDirectSound_CommitDeferredSettings_1_0_4627 =
{
    0,  // Large == 0
    11, // Count == 11

    -1, // XRef Not Saved
    0,  // XRef Not Used

    {
        // CDirectSound_CommitDeferredSettings+0x10 : movzx eax, al
        { 0x10, 0x0F }, // (Offset,Value)-Pair #1
        { 0x11, 0xB6 }, // (Offset,Value)-Pair #2
        { 0x12, 0xC0 }, // (Offset,Value)-Pair #3

        // CDirectSound_CommitDeferredSettings+0x27 : mov eax, 0x80004005
        { 0x27, 0xB8 }, // (Offset,Value)-Pair #4
        { 0x28, 0x05 }, // (Offset,Value)-Pair #5
        { 0x29, 0x40 }, // (Offset,Value)-Pair #6
        { 0x2B, 0x80 }, // (Offset,Value)-Pair #7

        // CDirectSound_CommitDeferredSettings+0x5C : and [eax+0xA4], esi
        { 0x5C, 0x21 }, // (Offset,Value)-Pair #8
        { 0x5D, 0xB0 }, // (Offset,Value)-Pair #9
        { 0x5E, 0xA4 }, // (Offset,Value)-Pair #10

        // CDirectSound_CommitDeferredSettings+0x78 : leave
        { 0x78, 0xC9 }, // (Offset,Value)-Pair #11
    }
};


// ******************************************************************
// * Turok Evolution coverage (generated by tools/oovpa/gen_oovpa.py from the
// * 4627 dsound.lib; every signature verified unique against the Turok XBE
// * and the other in-repo title/probe images). The C* signatures are
// * XRef-saves for the internals that discriminate byte-identical thin
// * wrappers; the IDirectSound*_* signatures are the hooked wrappers.
// ******************************************************************

// _DirectSoundDoWork@0 (dsound.lib, 41 bytes)
SOOVPA<8> DirectSoundDoWork_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x56 },
        { 0x06, 0x0F },
        { 0x09, 0xA1 },
        { 0x11, 0x06 },
        { 0x18, 0x85 },
        { 0x1C, 0x0B },
        { 0x22, 0xFF },
        { 0x28, 0xC3 }
    }
};

// _IDirectSoundBuffer_Release@4 (dsound.lib, 22 bytes)
SOOVPA<8> IDirectSoundBuffer_Release_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x06, 0xE4 },
        { 0x09, 0x1B },
        { 0x0C, 0xC1 },
        { 0x0F, 0x50 },
        { 0x12, 0x08 },
        { 0x15, 0x00 }
    }
};

// _IDirectSoundBuffer_Lock@32 (dsound.lib, 48 bytes)
SOOVPA<8> IDirectSoundBuffer_Lock_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x06, 0x8B },
        { 0x0D, 0xC8 },
        { 0x14, 0xFF },
        { 0x1A, 0x75 },
        { 0x21, 0x23 },
        { 0x27, 0xE8 },
        { 0x2F, 0x00 }
    }
};

// ?GetCaps@CDirectSound@DirectSound@@QAGJPAU_DSCAPS@@@Z (dsound.lib, 108 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CGetCaps_1_0_4627 =
{
    0, 8, XREF_DS4627_GETCAPS, 0,
    {
        { 0x00, 0xE8 },
        { 0x0F, 0x74 },
        { 0x20, 0xB8 },
        { 0x2D, 0x8B },
        { 0x3D, 0x15 },
        { 0x4C, 0x8B },
        { 0x5B, 0x0B },
        { 0x6B, 0x00 }
    }
};

// _IDirectSound_GetCaps@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_GETCAPS)
SOOVPA<9> IDirectSound_GetCaps_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_GETCAPS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetPosition@CDirectSound@DirectSound@@QAGJMMMK@Z (dsound.lib, 120 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetPosition_1_0_4627 =
{
    0, 8, XREF_DS4627_DS_SETPOSITION, 0,
    {
        { 0x00, 0x55 },
        { 0x11, 0xB6 },
        { 0x24, 0xB8 },
        { 0x33, 0x57 },
        { 0x44, 0x8B },
        { 0x55, 0xF6 },
        { 0x66, 0x68 },
        { 0x77, 0x00 }
    }
};

// _IDirectSound_SetPosition@20 (dsound.lib, 53 bytes; call@0x2D -> XREF_DS4627_DS_SETPOSITION)
SOOVPA<9> IDirectSound_SetPosition_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x2D, XREF_DS4627_DS_SETPOSITION },
        { 0x00, 0x55 },
        { 0x07, 0x45 },
        { 0x0E, 0x0C },
        { 0x16, 0x45 },
        { 0x1D, 0xD9 },
        { 0x25, 0xC9 },
        { 0x2C, 0xE8 },
        { 0x34, 0x00 }
    }
};

// ?SetVelocity@CDirectSound@DirectSound@@QAGJMMMK@Z (dsound.lib, 118 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetVelocity_1_0_4627 =
{
    0, 8, XREF_DS4627_DS_SETVELOCITY, 0,
    {
        { 0x00, 0x55 },
        { 0x10, 0x0F },
        { 0x1F, 0x15 },
        { 0x32, 0x10 },
        { 0x42, 0x8B },
        { 0x53, 0xF6 },
        { 0x64, 0x68 },
        { 0x75, 0x00 }
    }
};

// _IDirectSound_SetVelocity@20 (dsound.lib, 53 bytes; call@0x2D -> XREF_DS4627_DS_SETVELOCITY)
SOOVPA<9> IDirectSound_SetVelocity_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x2D, XREF_DS4627_DS_SETVELOCITY },
        { 0x00, 0x55 },
        { 0x07, 0x45 },
        { 0x0E, 0x0C },
        { 0x16, 0x45 },
        { 0x1D, 0xD9 },
        { 0x25, 0xC9 },
        { 0x2C, 0xE8 },
        { 0x34, 0x00 }
    }
};

// ?SetVolume@CDirectSoundVoice@DirectSound@@QAGJJ@Z (dsound.lib, 28 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetVolumeT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETVOLUME_T, 0,
    {
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x8B },
        { 0x0B, 0x2B },
        { 0x0F, 0x50 },
        { 0x13, 0x0C },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetVolume@CDirectSoundBuffer@DirectSound@@QAGJJ@Z (dsound.lib, 78 bytes; call@0x32 ?SetVolume@CDirectSoundVoice@DirectSound@@QAGJJ@Z -> XREF_DS4627_BUF_SETVOLUME_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETVOLUME, discriminated by callee.
SOOVPA<9> CSetVolume_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETVOLUME, 1,
    {
        { 0x32, XREF_DS4627_BUF_SETVOLUME_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x10 },
        { 0x37, 0xF6 },
        { 0x42, 0x15 },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetVolume@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_BUF_SETVOLUME)
SOOVPA<9> IDirectSoundBuffer_SetVolume_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETVOLUME },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetMixBins@CMcpxVoiceClient@DirectSound@@QAEJXZ (dsound.lib, 193 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetMixBinsTT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETMIXBINS_T_T, 0,
    {
        { 0x00, 0x55 },
        { 0x1B, 0x95 },
        { 0x35, 0xE8 },
        { 0x52, 0x85 },
        { 0x6D, 0x00 },
        { 0x89, 0x03 },
        { 0xA4, 0x00 },
        { 0xC0, 0xC3 }
    }
};

// ?SetMixBins@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z (dsound.lib, 29 bytes; call@0x15 ?SetMixBins@CMcpxVoiceClient@DirectSound@@QAEJXZ -> XREF_DS4627_BUF_SETMIXBINS_T_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETMIXBINS_T, discriminated by callee.
SOOVPA<9> CSetMixBinsT_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETMIXBINS_T, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETMIXBINS_T_T },
        { 0x00, 0x56 },
        { 0x04, 0x08 },
        { 0x08, 0x0C },
        { 0x0C, 0xE8 },
        { 0x11, 0x8B },
        { 0x14, 0xE8 },
        { 0x19, 0x5E },
        { 0x1C, 0x00 }
    }
};

// ?SetMixBins@CDirectSoundBuffer@DirectSound@@QAGJPBU_DSMIXBINS@@@Z (dsound.lib, 78 bytes; call@0x32 ?SetMixBins@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z -> XREF_DS4627_BUF_SETMIXBINS_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETMIXBINS, discriminated by callee.
SOOVPA<9> CSetMixBins_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETMIXBINS, 1,
    {
        { 0x32, XREF_DS4627_BUF_SETMIXBINS_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x10 },
        { 0x37, 0xF6 },
        { 0x42, 0x15 },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetMixBins@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_BUF_SETMIXBINS)
SOOVPA<9> IDirectSoundBuffer_SetMixBins_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETMIXBINS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetVolume@CMcpxVoiceClient@DirectSound@@QAEJXZ (dsound.lib, 152 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetMixBinVolumesTT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETMIXBINVOLUMES_T_T, 0,
    {
        { 0x00, 0x55 },
        { 0x15, 0xF6 },
        { 0x2B, 0x04 },
        { 0x40, 0x76 },
        { 0x56, 0x82 },
        { 0x6B, 0xFE },
        { 0x81, 0xB6 },
        { 0x97, 0xC3 }
    }
};

// ?SetMixBinVolumes@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z (dsound.lib, 29 bytes; call@0x15 ?SetVolume@CMcpxVoiceClient@DirectSound@@QAEJXZ -> XREF_DS4627_BUF_SETMIXBINVOLUMES_T_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETMIXBINVOLUMES_T, discriminated by callee.
SOOVPA<9> CSetMixBinVolumesT_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETMIXBINVOLUMES_T, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETMIXBINVOLUMES_T_T },
        { 0x00, 0x56 },
        { 0x04, 0x08 },
        { 0x08, 0x0C },
        { 0x0C, 0xE8 },
        { 0x11, 0x8B },
        { 0x14, 0xE8 },
        { 0x19, 0x5E },
        { 0x1C, 0x00 }
    }
};

// ?SetMixBinVolumes@CDirectSoundBuffer@DirectSound@@QAGJPBU_DSMIXBINS@@@Z (dsound.lib, 78 bytes; call@0x32 ?SetMixBinVolumes@CDirectSoundVoice@DirectSound@@QAGJPBU_DSMIXBINS@@@Z -> XREF_DS4627_BUF_SETMIXBINVOLUMES_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETMIXBINVOLUMES, discriminated by callee.
SOOVPA<9> CSetMixBinVolumes_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETMIXBINVOLUMES, 1,
    {
        { 0x32, XREF_DS4627_BUF_SETMIXBINVOLUMES_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x10 },
        { 0x37, 0xF6 },
        { 0x42, 0x15 },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetMixBinVolumes@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_BUF_SETMIXBINVOLUMES)
SOOVPA<9> IDirectSoundBuffer_SetMixBinVolumes_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETMIXBINVOLUMES },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?Play@CMcpxBuffer@DirectSound@@QAEJK@Z (dsound.lib, 124 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CPlayT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_PLAY_T, 0,
    {
        { 0x00, 0x53 },
        { 0x11, 0x62 },
        { 0x25, 0xEB },
        { 0x34, 0x75 },
        { 0x46, 0x01 },
        { 0x55, 0xE8 },
        { 0x69, 0x06 },
        { 0x7B, 0x00 }
    }
};

// ?Play@CDirectSoundBuffer@DirectSound@@QAGJKKK@Z (dsound.lib, 81 bytes; call@0x35 ?Play@CMcpxBuffer@DirectSound@@QAEJK@Z -> XREF_DS4627_BUF_PLAY_T)
// XRef chain level: saved to XREF_DS4627_BUF_PLAY, discriminated by callee.
SOOVPA<9> CPlay_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_PLAY, 1,
    {
        { 0x35, XREF_DS4627_BUF_PLAY_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2D, 0x48 },
        { 0x39, 0x85 },
        { 0x44, 0xFF },
        { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_Play@16 (dsound.lib, 36 bytes; call@0x1D -> XREF_DS4627_BUF_PLAY)
SOOVPA<9> IDirectSoundBuffer_Play_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x1D, XREF_DS4627_BUF_PLAY },
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0x74 },
        { 0x14, 0xE4 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// ?GetStatus@CMcpxBuffer@DirectSound@@QAEJPAK@Z (dsound.lib, 73 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CGetStatusT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_GETSTATUS_T, 0,
    {
        { 0x00, 0x55 },
        { 0x0A, 0x8B },
        { 0x14, 0x8B },
        { 0x1E, 0x4E },
        { 0x29, 0x10 },
        { 0x33, 0x06 },
        { 0x3D, 0xE8 },
        { 0x48, 0x00 }
    }
};

// ?GetStatus@CDirectSoundBuffer@DirectSound@@QAGJPAK@Z (dsound.lib, 81 bytes; call@0x35 ?GetStatus@CMcpxBuffer@DirectSound@@QAEJPAK@Z -> XREF_DS4627_BUF_GETSTATUS_T)
// XRef chain level: saved to XREF_DS4627_BUF_GETSTATUS, discriminated by callee.
SOOVPA<9> CGetStatus_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_GETSTATUS, 1,
    {
        { 0x35, XREF_DS4627_BUF_GETSTATUS_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2D, 0x48 },
        { 0x39, 0x85 },
        { 0x44, 0xFF },
        { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_GetStatus@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_BUF_GETSTATUS)
SOOVPA<9> IDirectSoundBuffer_GetStatus_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_BUF_GETSTATUS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?GetCurrentPosition@CDirectSoundBuffer@DirectSound@@QAGJPAK0@Z (dsound.lib, 85 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CGetCurrentPosition_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_GETCURPOS, 0,
    {
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x24, 0x00 },
        { 0x30, 0xFF },
        { 0x3D, 0x85 },
        { 0x48, 0xFF },
        { 0x54, 0x00 }
    }
};

// _IDirectSoundBuffer_GetCurrentPosition@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS4627_BUF_GETCURPOS)
SOOVPA<9> IDirectSoundBuffer_GetCurrentPosition_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS4627_BUF_GETCURPOS },
        { 0x00, 0x8B },
        { 0x04, 0xFF },
        { 0x08, 0x8B },
        { 0x0D, 0x0C },
        { 0x11, 0xF7 },
        { 0x16, 0xC8 },
        { 0x18, 0xE8 },
        { 0x1F, 0x00 }
    }
};

// ?SetCurrentPosition@CMcpxBuffer@DirectSound@@QAEJK@Z (dsound.lib, 246 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetCurrentPositionT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETCURPOS_T, 0,
    {
        { 0x00, 0x55 },
        { 0x23, 0xB7 },
        { 0x46, 0x03 },
        { 0x6A, 0x85 },
        { 0x8C, 0xC1 },
        { 0xAF, 0x05 },
        { 0xD2, 0xD2 },
        { 0xF5, 0x00 }
    }
};

// ?SetCurrentPosition@CDirectSoundBuffer@DirectSound@@QAGJK@Z (dsound.lib, 81 bytes; call@0x35 ?SetCurrentPosition@CMcpxBuffer@DirectSound@@QAEJK@Z -> XREF_DS4627_BUF_SETCURPOS_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETCURPOS, discriminated by callee.
SOOVPA<9> CSetCurrentPosition_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETCURPOS, 1,
    {
        { 0x35, XREF_DS4627_BUF_SETCURPOS_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2D, 0x48 },
        { 0x39, 0x85 },
        { 0x44, 0xFF },
        { 0x50, 0x00 }
    }
};

// _IDirectSoundBuffer_SetCurrentPosition@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_BUF_SETCURPOS)
SOOVPA<9> IDirectSoundBuffer_SetCurrentPosition_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETCURPOS },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetFrequency@CDirectSoundVoice@DirectSound@@QAGJK@Z (dsound.lib, 36 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetFrequencyT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETFREQUENCY_T, 0,
    {
        { 0x00, 0x8B },
        { 0x05, 0xC0 },
        { 0x0A, 0x08 },
        { 0x0F, 0x10 },
        { 0x14, 0xE8 },
        { 0x19, 0x50 },
        { 0x20, 0x5E },
        { 0x23, 0x00 }
    }
};

// ?SetFrequency@CDirectSoundBuffer@DirectSound@@QAGJK@Z (dsound.lib, 78 bytes; call@0x32 ?SetFrequency@CDirectSoundVoice@DirectSound@@QAGJK@Z -> XREF_DS4627_BUF_SETFREQUENCY_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETFREQUENCY, discriminated by callee.
SOOVPA<9> CSetFrequency_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETFREQUENCY, 1,
    {
        { 0x32, XREF_DS4627_BUF_SETFREQUENCY_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x10 },
        { 0x37, 0xF6 },
        { 0x42, 0x15 },
        { 0x4D, 0x00 }
    }
};

// _IDirectSoundBuffer_SetFrequency@8 (dsound.lib, 28 bytes; call@0x15 -> XREF_DS4627_BUF_SETFREQUENCY)
SOOVPA<9> IDirectSoundBuffer_SetFrequency_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x15, XREF_DS4627_BUF_SETFREQUENCY },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ?SetConeAngles@CDirectSoundBuffer@DirectSound@@QAGJKKK@Z (dsound.lib, 86 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetConeAngles_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETCONEANGLES, 0,
    {
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x24, 0x00 },
        { 0x30, 0x18 },
        { 0x3E, 0x85 },
        { 0x49, 0xFF },
        { 0x55, 0x00 }
    }
};

// _IDirectSoundBuffer_SetConeAngles@16 (dsound.lib, 36 bytes; call@0x1D -> XREF_DS4627_BUF_SETCONEANGLES)
SOOVPA<9> IDirectSoundBuffer_SetConeAngles_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x1D, XREF_DS4627_BUF_SETCONEANGLES },
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0x74 },
        { 0x14, 0xE4 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// ?SetI3DL2Source@CDirectSoundVoice@DirectSound@@QAGJPBU_DSI3DL2BUFFER@@K@Z (dsound.lib, 175 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CSetI3DL2SourceT_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_SETI3DL2SRC_T, 0,
    {
        { 0x00, 0x8B },
        { 0x18, 0x71 },
        { 0x31, 0x00 },
        { 0x4A, 0x8B },
        { 0x63, 0x41 },
        { 0x7C, 0x00 },
        { 0x95, 0x00 },
        { 0xAE, 0x00 }
    }
};

// ?SetI3DL2Source@CDirectSoundBuffer@DirectSound@@QAGJPBU_DSI3DL2BUFFER@@K@Z (dsound.lib, 82 bytes; call@0x36 ?SetI3DL2Source@CDirectSoundVoice@DirectSound@@QAGJPBU_DSI3DL2BUFFER@@K@Z -> XREF_DS4627_BUF_SETI3DL2SRC_T)
// XRef chain level: saved to XREF_DS4627_BUF_SETI3DL2SRC, discriminated by callee.
SOOVPA<9> CSetI3DL2Source_1_0_4627 =
{
    0, 9, XREF_DS4627_BUF_SETI3DL2SRC, 1,
    {
        { 0x36, XREF_DS4627_BUF_SETI3DL2SRC_T },
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x22, 0x05 },
        { 0x2E, 0x74 },
        { 0x3A, 0x85 },
        { 0x45, 0xFF },
        { 0x51, 0x00 }
    }
};

// _IDirectSoundBuffer_SetI3DL2Source@12 (dsound.lib, 32 bytes; call@0x19 -> XREF_DS4627_BUF_SETI3DL2SRC)
SOOVPA<9> IDirectSoundBuffer_SetI3DL2Source_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x19, XREF_DS4627_BUF_SETI3DL2SRC },
        { 0x00, 0x8B },
        { 0x04, 0xFF },
        { 0x08, 0x8B },
        { 0x0D, 0x0C },
        { 0x11, 0xF7 },
        { 0x16, 0xC8 },
        { 0x18, 0xE8 },
        { 0x1F, 0x00 }
    }
};

// ?Stop@CDirectSoundBuffer@DirectSound@@QAGJXZ (dsound.lib, 79 bytes)
// XRef-save signature (chain leaf, byte-unique).
SOOVPA<8> CStop_1_0_4627 =
{
    0, 8, XREF_DS4627_BUF_STOP, 0,
    {
        { 0x00, 0x56 },
        { 0x0C, 0x00 },
        { 0x16, 0x68 },
        { 0x21, 0xB8 },
        { 0x2C, 0x8B },
        { 0x37, 0x85 },
        { 0x42, 0xFF },
        { 0x4E, 0x00 }
    }
};

// _IDirectSoundBuffer_Stop@4 (dsound.lib, 24 bytes; call@0x11 -> XREF_DS4627_BUF_STOP)
SOOVPA<9> IDirectSoundBuffer_Stop_1_0_4627 =
{
    0, 9, -1, 1,
    {
        { 0x11, XREF_DS4627_BUF_STOP },
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x06, 0x83 },
        { 0x09, 0xF7 },
        { 0x0D, 0x23 },
        { 0x10, 0xE8 },
        { 0x15, 0xC2 },
        { 0x17, 0x00 }
    }
};

// _IDirectSoundBuffer_SetPosition@20 family shape (dsound.lib, 53 bytes) -- PATCH-ALL entry: every
// still-unpatched twin gets the shared accept-and-ignore impl.
SOOVPA<8> IDirectSoundBuffer_Deferred3dVector_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x55 },
        { 0x07, 0x45 },
        { 0x0E, 0x0C },
        { 0x16, 0x45 },
        { 0x1D, 0xD9 },
        { 0x25, 0xC9 },
        { 0x2C, 0xE8 },
        { 0x34, 0x00 }
    }
};

// _IDirectSoundBuffer_SetConeOutsideVolume@12 family shape (dsound.lib, 32 bytes) -- PATCH-ALL entry: every
// still-unpatched twin gets the shared accept-and-ignore impl.
SOOVPA<8> IDirectSoundBuffer_Deferred3dPair_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x04, 0xFF },
        { 0x08, 0x8B },
        { 0x0D, 0x0C },
        { 0x11, 0xF7 },
        { 0x16, 0xC8 },
        { 0x18, 0xE8 },
        { 0x1F, 0x00 }
    }
};

// _IDirectSoundBuffer_SetMaxDistance@12 family shape (dsound.lib, 36 bytes) -- PATCH-ALL entry: every
// still-unpatched twin gets the shared accept-and-ignore impl.
SOOVPA<8> IDirectSoundBuffer_Deferred3dFloat_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0xFF },
        { 0x05, 0x44 },
        { 0x0A, 0x24 },
        { 0x0F, 0xD9 },
        { 0x14, 0xE4 },
        { 0x19, 0x23 },
        { 0x1C, 0xE8 },
        { 0x23, 0x00 }
    }
};

// _IDirectSoundBuffer_* @8 wrapper family shape (28 bytes) -- PATCH-ALL
// FALLBACK: members whose specific XRef chains resolved are already E9-
// patched and skipped; the rest (SetVolume / SetMixBinVolumes and the
// siblings Turok never calls) get the @8 accept-and-ignore impl, which is
// also the intended behavior for both called members.
SOOVPA<8> IDirectSoundBuffer_Setter8_1_0_4627 =
{
    0, 8, -1, 0,
    {
        { 0x00, 0x8B },
        { 0x03, 0x04 },
        { 0x07, 0x08 },
        { 0x0B, 0xC0 },
        { 0x0F, 0x1B },
        { 0x13, 0x51 },
        { 0x19, 0xC2 },
        { 0x1B, 0x00 }
    }
};

// ******************************************************************
// * DirectSoundCreate_1_0_4627
// ******************************************************************
OOVPATable DSound_1_0_4627[] =
{
    // DirectSoundCreate (* unchanged since 4361 *)
    {
        (OOVPA*)&DirectSoundCreate_1_0_4361,

        XTL::EmuDirectSoundCreate,

        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreate" 
        #endif
    },
    // CDirectSound_CreateSoundBuffer
    {
        (OOVPA*)&CDirectSound_CreateSoundBuffer_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound::CreateSoundBuffer (XREF)" 
        #endif
    },
    // CDirectSound_SetI3DL2Listener
    {
        (OOVPA*)&CDirectSound_SetI3DL2Listener_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound::SetI3DL2Listener (XREF)" 
        #endif
    },
    // CDirectSound_SetMixBinHeadroom
    {
        (OOVPA*)&CDirectSound_SetMixBinHeadroom_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound::SetMixBinHeadroom (XREF)" 
        #endif
    },
    // DirectSoundCreateBuffer
    {
        (OOVPA*)&DirectSoundCreateBuffer_1_0_4627,

        XTL::EmuDirectSoundCreateBuffer,

        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreateBuffer" 
        #endif
    },
    // IDirectSound8::CreateSoundBuffer
    {
        (OOVPA*)&IDirectSound8_CreateSoundBuffer_1_0_4627,

        XTL::EmuIDirectSound8_CreateSoundBuffer,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_CreateSoundBuffer" 
        #endif
    },
    // IDirectSound8_SetI3DL2Listener
    {
        (OOVPA*)&IDirectSound8_SetI3DL2Listener_1_0_4627,

        XTL::EmuIDirectSound8_SetI3DL2Listener,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetI3DL2Listener" 
        #endif
    },
    // IDirectSound8_SetMixBinHeadroom
    {
        (OOVPA*)&IDirectSound8_SetMixBinHeadroom_1_0_4627,

        XTL::EmuIDirectSound8_SetMixBinHeadroom,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetMixBinHeadroom"
        #endif
    },
    // CMcpxVoiceClient_SetVolume
    {
        (OOVPA*)&CMcpxVoiceClient_SetVolume_1_0_4361, 0,

        #ifdef _DEBUG_TRACE
        "CMcpxVoiceClient::SetVolume (XREF)" 
        #endif
    },
    // CDirectSoundStream_SetVolume
    {
        (OOVPA*)&CDirectSoundStream_SetVolume_1_0_4361, 
            
        XTL::EmuCDirectSoundStream_SetVolume,

        #ifdef _DEBUG_TRACE
        "EmuCDirectSoundStream_SetVolume" 
        #endif
    },
    // CDirectSound_CreateSoundStream (* unchanged since 4361 *)
    {
        (OOVPA*)&CDirectSound_CreateSoundStream_1_0_4361, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound::CreateSoundStream (XREF)" 
        #endif
    },
    // DirectSoundCreateStream (* unchanged since 4361 *)
    {
        (OOVPA*)&DirectSoundCreateStream_1_0_4361,

        XTL::EmuDirectSoundCreateStream,

        #ifdef _DEBUG_TRACE
        "EmuDirectSoundCreateStream" 
        #endif
    },
    // CMcpxBuffer::SetBufferData (* unchanged since 4361 *)
    {
        (OOVPA*)&CMcpxBuffer_SetBufferData_1_0_4361, 0,

        #ifdef _DEBUG_TRACE
        "CMcpxBuffer_SetBufferData (XREF)"
        #endif
    },
    // CDirectSoundBuffer::SetBufferData (* unchanged since 4361 *)
    {
        (OOVPA*)&CDirectSoundBuffer_SetBufferData_1_0_4361, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSoundBuffer_SetBufferData (XREF)"
        #endif
    },
    // IDirectSoundBuffer8::SetBufferData (* unchanged since 4361 *)
    {
        (OOVPA*)&IDirectSoundBuffer8_SetBufferData_1_0_4361, 
            
        XTL::EmuIDirectSoundBuffer8_SetBufferData,

        #ifdef _DEBUG_TRACE
        "IDirectSoundBuffer8_SetBufferData"
        #endif
    },
    // IDirectSound8::Release
    {
        (OOVPA*)&IDirectSound8_Release_1_0_4627,

        XTL::EmuIDirectSound8_Release,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_Release" 
        #endif
    },
    // IDirectSound8::DownloadEffectsImage (* unchanged since 3936 *)
    {
        (OOVPA*)&IDirectSound8_DownloadEffectsImage_1_0_3936,

        XTL::EmuIDirectSound8_DownloadEffectsImage,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_DownloadEffectsImage" 
        #endif
    },
    // IDirectSound8::SetOrientation (* unchanged since 3936 *)
    {
        (OOVPA*)&IDirectSound8_SetOrientation_1_0_3936,

        XTL::EmuIDirectSound8_SetOrientation,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetOrientation" 
        #endif
    },
    // CDirectSound::SetDistanceFactorA (XREF)
    {
        (OOVPA*)&CDirectSound_SetDistanceFactorA_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound_SetDistanceFactorA (XRef)"
        #endif
    },
    // IDirectSound8::SetDistanceFactor
    {
        (OOVPA*)&IDirectSound8_SetDistanceFactor_1_0_4627,

        XTL::EmuIDirectSound8_SetDistanceFactor,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetDistanceFactor" 
        #endif
    },
    // CDirectSound::SetRolloffFactor (XREF)
    {
        (OOVPA*)&CDirectSound_SetRolloffFactor_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound_SetRolloffFactor (XRef)"
        #endif
    },
    // IDirectSound8::SetRolloffFactor
    {
        (OOVPA*)&IDirectSound8_SetRolloffFactor_1_0_4627,

        XTL::EmuIDirectSound8_SetRolloffFactor,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetRolloffFactor" 
        #endif
    },
    // CDirectSound::SetDopplerFactor (XREF)
    {
        (OOVPA*)&CDirectSound_SetDopplerFactor_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CDirectSound_SetDopplerFactor (XRef)"
        #endif
    },
    // IDirectSound8::SetDopplerFactor
    {
        (OOVPA*)&IDirectSound8_SetDopplerFactor_1_0_4627,

        XTL::EmuIDirectSound8_SetDopplerFactor,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetDopplerFactor" 
        #endif
    },
    // CDirectSound::CommitDeferredSettings
    {
        (OOVPA*)&CDirectSound_CommitDeferredSettings_1_0_4627,

        XTL::EmuCDirectSound_CommitDeferredSettings,

        #ifdef _DEBUG_TRACE
        "EmuCDirectSound_CommitDeferredSettings" 
        #endif
    },
    // ---- Turok Evolution coverage: XRef saves first (table order
    // ---- resolves chains leaf-first within the first pass), then the
    // ---- hooked wrappers, then the patch-all twin families LAST (an
    // ---- already-patched prologue no longer matches, so specific
    // ---- hooks always win over the family sweep).
    // CGetCaps_1_0_4627 (XRef save)
    {
        (OOVPA*)&CGetCaps_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CGetCaps_1_0_4627 (XREF)"
        #endif
    },
    // CSetPosition_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetPosition_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetPosition_1_0_4627 (XREF)"
        #endif
    },
    // CSetVelocity_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetVelocity_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetVelocity_1_0_4627 (XREF)"
        #endif
    },
    // CSetVolumeT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetVolumeT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetVolumeT_1_0_4627 (XREF)"
        #endif
    },
    // CSetVolume_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetVolume_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetVolume_1_0_4627 (XREF)"
        #endif
    },
    // CSetMixBinsTT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetMixBinsTT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetMixBinsTT_1_0_4627 (XREF)"
        #endif
    },
    // CSetMixBinsT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetMixBinsT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetMixBinsT_1_0_4627 (XREF)"
        #endif
    },
    // CSetMixBins_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetMixBins_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetMixBins_1_0_4627 (XREF)"
        #endif
    },
    // CSetMixBinVolumesTT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetMixBinVolumesTT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetMixBinVolumesTT_1_0_4627 (XREF)"
        #endif
    },
    // CSetMixBinVolumesT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetMixBinVolumesT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetMixBinVolumesT_1_0_4627 (XREF)"
        #endif
    },
    // CSetMixBinVolumes_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetMixBinVolumes_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetMixBinVolumes_1_0_4627 (XREF)"
        #endif
    },
    // CPlayT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CPlayT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CPlayT_1_0_4627 (XREF)"
        #endif
    },
    // CPlay_1_0_4627 (XRef save)
    {
        (OOVPA*)&CPlay_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CPlay_1_0_4627 (XREF)"
        #endif
    },
    // CGetStatusT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CGetStatusT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CGetStatusT_1_0_4627 (XREF)"
        #endif
    },
    // CGetStatus_1_0_4627 (XRef save)
    {
        (OOVPA*)&CGetStatus_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CGetStatus_1_0_4627 (XREF)"
        #endif
    },
    // CGetCurrentPosition_1_0_4627 (XRef save)
    {
        (OOVPA*)&CGetCurrentPosition_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CGetCurrentPosition_1_0_4627 (XREF)"
        #endif
    },
    // CSetCurrentPositionT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetCurrentPositionT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetCurrentPositionT_1_0_4627 (XREF)"
        #endif
    },
    // CSetCurrentPosition_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetCurrentPosition_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetCurrentPosition_1_0_4627 (XREF)"
        #endif
    },
    // CSetFrequencyT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetFrequencyT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetFrequencyT_1_0_4627 (XREF)"
        #endif
    },
    // CSetFrequency_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetFrequency_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetFrequency_1_0_4627 (XREF)"
        #endif
    },
    // CSetConeAngles_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetConeAngles_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetConeAngles_1_0_4627 (XREF)"
        #endif
    },
    // CSetI3DL2SourceT_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetI3DL2SourceT_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetI3DL2SourceT_1_0_4627 (XREF)"
        #endif
    },
    // CSetI3DL2Source_1_0_4627 (XRef save)
    {
        (OOVPA*)&CSetI3DL2Source_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CSetI3DL2Source_1_0_4627 (XREF)"
        #endif
    },
    // CStop_1_0_4627 (XRef save)
    {
        (OOVPA*)&CStop_1_0_4627, 0,

        #ifdef _DEBUG_TRACE
        "CStop_1_0_4627 (XREF)"
        #endif
    },
    // DirectSoundDoWork_1_0_4627
    {
        (OOVPA*)&DirectSoundDoWork_1_0_4627,

        XTL::EmuDirectSoundDoWork,

        #ifdef _DEBUG_TRACE
        "EmuDirectSoundDoWork"
        #endif
    },
    // IDirectSoundBuffer_Release_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_Release_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_Release,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Release"
        #endif
    },
    // IDirectSoundBuffer_Lock_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_Lock_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_Lock,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Lock"
        #endif
    },
    // IDirectSound_GetCaps_1_0_4627
    {
        (OOVPA*)&IDirectSound_GetCaps_1_0_4627,

        XTL::EmuIDirectSound8_GetCaps,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_GetCaps"
        #endif
    },
    // IDirectSound_SetPosition_1_0_4627
    {
        (OOVPA*)&IDirectSound_SetPosition_1_0_4627,

        XTL::EmuIDirectSound8_SetPosition,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetPosition"
        #endif
    },
    // IDirectSound_SetVelocity_1_0_4627
    {
        (OOVPA*)&IDirectSound_SetVelocity_1_0_4627,

        XTL::EmuIDirectSound8_SetVelocity,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSound8_SetVelocity"
        #endif
    },
    // IDirectSoundBuffer_SetVolume_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetVolume_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetVolume,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetVolume"
        #endif
    },
    // IDirectSoundBuffer_SetMixBins_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetMixBins_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetMixBins,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetMixBins"
        #endif
    },
    // IDirectSoundBuffer_SetMixBinVolumes_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetMixBinVolumes_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetMixBinVolumes,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetMixBinVolumes"
        #endif
    },
    // IDirectSoundBuffer_Play_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_Play_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_Play,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Play"
        #endif
    },
    // IDirectSoundBuffer_GetStatus_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_GetStatus_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_GetStatus,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_GetStatus"
        #endif
    },
    // IDirectSoundBuffer_GetCurrentPosition_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_GetCurrentPosition_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_GetCurrentPosition,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_GetCurrentPosition"
        #endif
    },
    // IDirectSoundBuffer_SetCurrentPosition_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetCurrentPosition_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetCurrentPosition,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetCurrentPosition"
        #endif
    },
    // IDirectSoundBuffer_SetFrequency_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetFrequency_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetFrequency,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetFrequency"
        #endif
    },
    // IDirectSoundBuffer_SetConeAngles_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetConeAngles_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetConeAngles,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetConeAngles"
        #endif
    },
    // IDirectSoundBuffer_SetI3DL2Source_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_SetI3DL2Source_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetI3DL2Source,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetI3DL2Source"
        #endif
    },
    // IDirectSoundBuffer_Stop_1_0_4627
    {
        (OOVPA*)&IDirectSoundBuffer_Stop_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_Stop,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_Stop"
        #endif
    },
    // IDirectSoundBuffer_Deferred3dVector_1_0_4627 (patch-all twin family)
    {
        (OOVPA*)&IDirectSoundBuffer_Deferred3dVector_1_0_4627,

        XTL::EmuCDirectSoundBuffer_SetDeferred3dVector,

        #ifdef _DEBUG_TRACE
        "EmuCDirectSoundBuffer_SetDeferred3dVector",
        #endif

        OOVPA_FLAG_PATCH_ALL
    },
    // IDirectSoundBuffer_Deferred3dPair_1_0_4627 (patch-all twin family)
    {
        (OOVPA*)&IDirectSoundBuffer_Deferred3dPair_1_0_4627,

        XTL::EmuCDirectSoundBuffer_SetDeferred3dParam,

        #ifdef _DEBUG_TRACE
        "EmuCDirectSoundBuffer_SetDeferred3dParam",
        #endif

        OOVPA_FLAG_PATCH_ALL
    },
    // IDirectSoundBuffer_Deferred3dFloat_1_0_4627 (patch-all twin family)
    {
        (OOVPA*)&IDirectSoundBuffer_Deferred3dFloat_1_0_4627,

        XTL::EmuCDirectSoundBuffer_SetDeferred3dParam,

        #ifdef _DEBUG_TRACE
        "EmuCDirectSoundBuffer_SetDeferred3dParam",
        #endif

        OOVPA_FLAG_PATCH_ALL
    },
    // IDirectSoundBuffer @8 wrapper family (patch-all FALLBACK; must stay
    // the LAST DSOUND entry so every specifically-hooked member is already
    // E9-patched and skipped)
    {
        (OOVPA*)&IDirectSoundBuffer_Setter8_1_0_4627,

        XTL::EmuIDirectSoundBuffer8_SetMixBins,

        #ifdef _DEBUG_TRACE
        "EmuIDirectSoundBuffer8_SetMixBins (setter8 fallback)",
        #endif

        OOVPA_FLAG_PATCH_ALL
    },
};

// ******************************************************************
// * DSound_1_0_4627_SIZE
// ******************************************************************
uint32 DSound_1_0_4627_SIZE = sizeof(DSound_1_0_4627);
