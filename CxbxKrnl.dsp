# Microsoft Developer Studio Project File - Name="CxbxKrnl" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 60000
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=CxbxKrnl - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "CxbxKrnl.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "CxbxKrnl.mak" CFG="CxbxKrnl - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "CxbxKrnl - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "CxbxKrnl - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "CxbxKrnl - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "CxbxKrnl___Win32_Release"
# PROP BASE Intermediate_Dir "CxbxKrnl___Win32_Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Bin"
# PROP Intermediate_Dir "Bin"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "CXBXKRNL_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /O2 /I "Include" /I "Include/Core/" /I "Include/Win32/" /I "Include/Win32/Cxbxkrnl" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "CXBXKRNL_EXPORTS" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /i "Include\Win32\CxbxKrnl" /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 ws2_32.lib dsound.lib winmm.lib ddraw.lib d3d8.lib dinput8.lib dxguid.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /map /machine:I386 /out:"Bin/Cxbx.dll" /libpath:"Lib"
# SUBTRACT LINK32 /profile /pdb:none /debug

!ELSEIF  "$(CFG)" == "CxbxKrnl - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Bin\Debug"
# PROP Intermediate_Dir "Bin\Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "CXBXKRNL_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MDd /W3 /Gm /Zi /Od /I "Include" /I "Include/Core/" /I "Include/Win32/" /I "Include/Win32/Cxbx" /I "Include/Win32/Cxbxkrnl" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "CXBXKRNL_EXPORTS" /FR /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /i "Include\Win32\CxbxKrnl" /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 ws2_32.lib dsound.lib winmm.lib ddraw.lib d3dx8.lib d3d8.lib dinput8.lib dxguid.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /map /debug /machine:I386 /pdbtype:sept /libpath:"Lib"
# SUBTRACT LINK32 /pdb:none

!ENDIF 

# Begin Target

# Name "CxbxKrnl - Win32 Release"
# Name "CxbxKrnl - Win32 Debug"
# Begin Group "Bin"

# PROP Default_Filter ""
# Begin Group "Debug"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Bin\Debug\Cxbx.dll
# End Source File
# End Group
# Begin Source File

SOURCE=.\Bin\Cxbx.dll
# End Source File
# End Group
# Begin Group "Doc"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Doc\Changelog.txt
# End Source File
# Begin Source File

SOURCE=.\Doc\Input.txt
# End Source File
# Begin Source File

SOURCE=.\Doc\RemovedCode.txt
# End Source File
# Begin Source File

SOURCE=.\Doc\Thanks.txt
# End Source File
# Begin Source File

SOURCE=.\Doc\Todo.txt
# End Source File
# End Group
# Begin Group "Include"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Include\Win32\AlignPosfix1.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\AlignPrefix1.h
# End Source File
# Begin Source File

SOURCE=.\Include\Cxbx.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\D3D8.1.0.3925.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\D3D8.1.0.4034.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\D3D8.1.0.4134.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\D3D8.1.0.4361.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\D3D8.1.0.4627.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\DSound.1.0.3936.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\DSound.1.0.4361.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\DSound.1.0.4627.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\Emu.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuD3D8.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuDInput.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuDSound.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuFile.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuFS.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuLDT.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuNtDll.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuShared.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuXapi.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuXG.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuXOnline.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\EmuXTL.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\HLEDataBase.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\Mutex.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\OOVPA.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\Xapi.1.0.3911.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\Xapi.1.0.4034.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\Xapi.1.0.4134.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\Xapi.1.0.4361.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\Xapi.1.0.4627.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\XBController.h
# End Source File
# Begin Source File

SOURCE=.\Include\Core\Xbe.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\XBVideo.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\XG.1.0.4361.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\XG.1.0.4627.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\XNet.1.0.3911.h
# End Source File
# Begin Source File

SOURCE=.\Include\Win32\CxbxKrnl\XOnline.1.0.4361.h
# End Source File
# End Group
# Begin Group "Resource"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Resource\Cxbx.ico
# End Source File
# Begin Source File

SOURCE=.\Resource\CxbxDll.rc
# End Source File
# End Group
# Begin Group "Source"

# PROP Default_Filter ""
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\D3D8.1.0.3925.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\D3D8.1.0.4034.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\D3D8.1.0.4134.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\D3D8.1.0.4361.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\D3D8.1.0.4627.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\DSound.1.0.3936.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\DSound.1.0.4361.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\DSound.1.0.4627.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\Emu.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuD3D8.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuD3D8Conv.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuDInput.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuDSound.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuFile.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuFS.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuKrnl.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuLDT.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuNtDll.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\EmuShared.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuXapi.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuXG.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\EmuXOnline.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Core\Error.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\HLEDataBase.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\KernelThunk.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\Mutex.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\Xapi.1.0.3911.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\Xapi.1.0.4034.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\Xapi.1.0.4134.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\Xapi.1.0.4361.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\Xapi.1.0.4627.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\XBController.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\XBVideo.cpp
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\XG.1.0.4361.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\XG.1.0.4627.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\XNet.1.0.3911.inl
# End Source File
# Begin Source File

SOURCE=.\Source\Win32\CxbxKrnl\XOnline.1.0.4361.inl
# End Source File
# End Group
# End Target
# End Project
