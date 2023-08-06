# Microsoft Visual C++ Generated NMAKE File, Format Version 2.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Application" 0x0101

!IF "$(CFG)" == ""
CFG=Win32 Debug
!MESSAGE No configuration specified.  Defaulting to Win32 Debug.
!ENDIF 

!IF "$(CFG)" != "Win32 Release" && "$(CFG)" != "Win32 Debug"
!MESSAGE Invalid configuration "$(CFG)" specified.
!MESSAGE You can specify a configuration when running NMAKE on this makefile
!MESSAGE by defining the macro CFG on the command line.  For example:
!MESSAGE 
!MESSAGE NMAKE /f "VXDMON.MAK" CFG="Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "Win32 Release" (based on "Win32 (x86) Application")
!MESSAGE "Win32 Debug" (based on "Win32 (x86) Application")
!MESSAGE 
!ERROR An invalid configuration is specified.
!ENDIF 

################################################################################
# Begin Project
# PROP Target_Last_Scanned "Win32 Debug"
MTL=MkTypLib.exe
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "Win32 Release"

# PROP BASE Use_MFC 2
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "WinRel"
# PROP BASE Intermediate_Dir "WinRel"
# PROP Use_MFC 2
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "WinRel"
# PROP Intermediate_Dir "WinRel"
OUTDIR=.\WinRel
INTDIR=.\WinRel

ALL : $(OUTDIR)/VXDMON.exe $(OUTDIR)/VXDMON.bsc

$(OUTDIR) : 
    if not exist $(OUTDIR)/nul mkdir $(OUTDIR)

# ADD BASE MTL /nologo /D "NDEBUG" /win32
# ADD MTL /nologo /D "NDEBUG" /win32
MTL_PROJ=/nologo /D "NDEBUG" /win32 
# ADD BASE CPP /nologo /MD /W3 /GX /YX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS" /FR /c
# ADD CPP /nologo /MD /W3 /GX /YX /O2 /I "\mstools\include" /I "\ddk\inc32" /I "\vtd95\include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS" /FR /c
CPP_PROJ=/nologo /MD /W3 /GX /YX /O2 /I "\mstools\include" /I "\ddk\inc32" /I\
 "\vtd95\include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS"\
 /FR$(INTDIR)/ /Fp$(OUTDIR)/"VXDMON.pch" /Fo$(INTDIR)/ /c 
CPP_OBJS=.\WinRel/
# ADD BASE RSC /l 0x0 /d "NDEBUG" /d "_AFXDLL"
# ADD RSC /l 0x0 /d "NDEBUG" /d "_AFXDLL"
RSC_PROJ=/l 0x0 /fo$(INTDIR)/"VXDMON.res" /d "NDEBUG" /d "_AFXDLL" 
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# SUBTRACT BASE BSC32 /Iu
# ADD BSC32 /nologo
# SUBTRACT BSC32 /Iu
BSC32_FLAGS=/nologo /o$(OUTDIR)/"VXDMON.bsc" 
BSC32_SBRS= \
	$(INTDIR)/VXDMON.SBR

$(OUTDIR)/VXDMON.bsc : $(OUTDIR)  $(BSC32_SBRS)
    $(BSC32) @<<
  $(BSC32_FLAGS) $(BSC32_SBRS)
<<

LINK32=link.exe
# ADD BASE LINK32 user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib mfc30.lib mfco30.lib mfcuia32.lib mfcans32.lib ole32.lib oleaut32.lib uuid.lib /NOLOGO /SUBSYSTEM:windows /MACHINE:I386
# ADD LINK32 mfc30.lib mfco30.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib mfcuia32.lib mfcans32.lib ole32.lib oleaut32.lib uuid.lib comctl32.lib /NOLOGO /MACHINE:I386 -L\mstools\lib /SUBSYSTEM:windows,4.0
# SUBTRACT LINK32 /PDB:none
LINK32_FLAGS=mfc30.lib mfco30.lib user32.lib gdi32.lib winspool.lib\
 comdlg32.lib advapi32.lib shell32.lib mfcuia32.lib mfcans32.lib ole32.lib\
 oleaut32.lib uuid.lib comctl32.lib /NOLOGO /INCREMENTAL:no\
 /PDB:$(OUTDIR)/"VXDMON.pdb" /MACHINE:I386 /OUT:$(OUTDIR)/"VXDMON.exe"\
 -L\mstools\lib /SUBSYSTEM:windows,4.0  
DEF_FILE=
LINK32_OBJS= \
	$(INTDIR)/VXDMON.res \
	$(INTDIR)/VXDMON.OBJ

$(OUTDIR)/VXDMON.exe : $(OUTDIR)  $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ELSEIF  "$(CFG)" == "Win32 Debug"

# PROP BASE Use_MFC 2
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "WinDebug"
# PROP BASE Intermediate_Dir "WinDebug"
# PROP Use_MFC 2
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "WinDebug"
# PROP Intermediate_Dir "WinDebug"
OUTDIR=.\WinDebug
INTDIR=.\WinDebug

ALL : $(OUTDIR)/VXDMON.exe $(OUTDIR)/VXDMON.bsc

$(OUTDIR) : 
    if not exist $(OUTDIR)/nul mkdir $(OUTDIR)

# ADD BASE MTL /nologo /D "_DEBUG" /win32
# ADD MTL /nologo /D "_DEBUG" /win32
MTL_PROJ=/nologo /D "_DEBUG" /win32 
# ADD BASE CPP /nologo /MD /W3 /GX /Zi /YX /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS" /FR /c
# ADD CPP /nologo /MD /W3 /GX /Zi /YX /Od /I "\mstools\include" /I "\ddk\inc32" /I "\vtd95\include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /D "_MBCS" /c
# SUBTRACT CPP /Fr
CPP_PROJ=/nologo /MD /W3 /GX /Zi /YX /Od /I "\mstools\include" /I "\ddk\inc32"\
 /I "\vtd95\include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_AFXDLL" /D\
 "_MBCS" /Fp$(OUTDIR)/"VXDMON.pch" /Fo$(INTDIR)/ /Fd$(OUTDIR)/"VXDMON.pdb" /c 
CPP_OBJS=.\WinDebug/
# ADD BASE RSC /l 0x0 /d "_DEBUG" /d "_AFXDLL"
# ADD RSC /l 0x0 /d "_DEBUG" /d "_AFXDLL"
RSC_PROJ=/l 0x0 /fo$(INTDIR)/"VXDMON.res" /d "_DEBUG" /d "_AFXDLL" 
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# SUBTRACT BASE BSC32 /Iu
# ADD BSC32 /nologo
# SUBTRACT BSC32 /Iu
BSC32_FLAGS=/nologo /o$(OUTDIR)/"VXDMON.bsc" 
BSC32_SBRS= \
	

$(OUTDIR)/VXDMON.bsc : $(OUTDIR)  $(BSC32_SBRS)
LINK32=link.exe
# ADD BASE LINK32 user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib mfc30d.lib mfco30d.lib mfcuia32.lib mfcans32.lib ole32.lib oleaut32.lib uuid.lib /NOLOGO /SUBSYSTEM:windows /DEBUG /MACHINE:I386
# ADD LINK32 mfc30d.lib mfco30d.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib mfcuia32.lib mfcans32.lib ole32.lib oleaut32.lib uuid.lib comctl32.lib /NOLOGO /DEBUG /MACHINE:I386 /SUBSYSTEM:windows,4.0
# SUBTRACT LINK32 /PDB:none
LINK32_FLAGS=mfc30d.lib mfco30d.lib user32.lib gdi32.lib winspool.lib\
 comdlg32.lib advapi32.lib shell32.lib mfcuia32.lib mfcans32.lib ole32.lib\
 oleaut32.lib uuid.lib comctl32.lib /NOLOGO /INCREMENTAL:yes\
 /PDB:$(OUTDIR)/"VXDMON.pdb" /DEBUG /MACHINE:I386 /OUT:$(OUTDIR)/"VXDMON.exe"\
 /SUBSYSTEM:windows,4.0 
DEF_FILE=
LINK32_OBJS= \
	$(INTDIR)/VXDMON.res \
	$(INTDIR)/VXDMON.OBJ

$(OUTDIR)/VXDMON.exe : $(OUTDIR)  $(DEF_FILE) $(LINK32_OBJS)
    $(LINK32) @<<
  $(LINK32_FLAGS) $(LINK32_OBJS)
<<

!ENDIF 

.c{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.cpp{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

.cxx{$(CPP_OBJS)}.obj:
   $(CPP) $(CPP_PROJ) $<  

################################################################################
# Begin Group "Source Files"

################################################################################
# Begin Source File

SOURCE=.\VXDMON.RC

$(INTDIR)/VXDMON.res :  $(SOURCE)  $(INTDIR)
   $(RSC) $(RSC_PROJ)  $(SOURCE) 

# End Source File
################################################################################
# Begin Source File

SOURCE=.\VXDMON.C
DEP_VXDMO=\
	..\VXD\VXDMON.H

$(INTDIR)/VXDMON.OBJ :  $(SOURCE)  $(DEP_VXDMO) $(INTDIR)

# End Source File
# End Group
# End Project
################################################################################
