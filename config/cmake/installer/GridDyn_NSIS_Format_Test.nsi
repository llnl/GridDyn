; GridDyn Installer Test
; Written by Jennifer Spero

;--------------------------------
;Include Modern UI

  !include "MUI2.nsh"

;--------------------------------
;General

  ;Name and file
  Name "GridDyn Installer Test"
  OutFile "GridDynInstaller.exe"

  ;Default installation folder
  InstallDir "$LOCALAPPDATA\GridDynTest"

  ;Get installation folder from registry if available
  InstallDirRegKey HKCU "Software\GridDyn Test" ""

;--------------------------------
;Interface Config

  !define MUI_WELCOMEFINISHPAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Wizard\welcomefinish.bmp"
  !define MUI_HEADERIMAGE
  !define MUI_HEADERIMAGE_RIGHT
  !define MUI_HEADERIMAGE_BITMAP "${NSISDIR}\Contrib\Graphics\Header\GridDyn_header.bmp"
  !define MUI_ABORTWARNING
  !define MUI_ICON "${NSISDIR}\Contrib\Graphics\Icons\GridDyn_inst_invert.ico"
  !define MUI_UNICON "${NSISDIR}\Contrib\Graphics\Icons\GridDyn_uninst_invert.ico"

;--------------------------------
;Page Settings

  ;!define MUI_WELCOMEPAGE_TEXT "This is a test.$\r$\n Line two."
  !define MUI_FINISHPAGE_LINK "View the GridDyn User Manual (GitHub)"
  !define MUI_FINISHPAGE_LINK_LOCATION "https://github.com/LLNL/GridDyn/blob/master/docs/manuals/GridDynUserManual.pdf"
;--------------------------------
;Pages

  !insertmacro MUI_PAGE_WELCOME
  !insertmacro MUI_PAGE_LICENSE "${NSISDIR}\Docs\GridDyn\License.txt"
  !insertmacro MUI_PAGE_LICENSE "${NSISDIR}\Docs\GridDyn\KLU_License.txt"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES
  !insertmacro MUI_PAGE_FINISH

  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES

;--------------------------------
;Languages

  !insertmacro MUI_LANGUAGE "English"

;--------------------------------
;Initialization

Function .onInit
    # the plugins dir is automatically deleted when the installer exits
    InitPluginsDir
    File /oname=$PLUGINSDIR\splash.bmp "${NSISDIR}\Contrib\Graphics\Wizard\GridDyn_Small.bmp"

    splash::show 2000 $PLUGINSDIR\splash

    Pop $0 ; $0 has '1' if the user closed the splash screen early,
            ; '0' if everything closed normally, and '-1' if some error occurred.
FunctionEnd

;---------------------------------

;--------------------------------
;Installer Sections/Components

Section "Section 1" Section1

  SetOutPath "$INSTDIR"

  ;ADD YOUR OWN FILES HERE...

  ;Store installation folder
  WriteRegStr HKCU "Software\Modern UI Test" "" $INSTDIR

  ;Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

SectionEnd

Section "Section2" Section2
  ;Add code here...
SectionEnd

;--------------------------------
;Descriptions

  ;Language strings
  LangString DESC_Section1 ${LANG_ENGLISH} "A test section."
  LangString DESC_Section2 ${LANG_ENGLISH} "A second test section."

  ;Assign language strings to sections
  !insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${Section1} $(DESC_Section1)
    !insertmacro MUI_DESCRIPTION_TEXT ${Section2} $(DESC_Section2)
  !insertmacro MUI_FUNCTION_DESCRIPTION_END

;--------------------------------
;Uninstaller Section

Section "Uninstall"

  ;ADD YOUR OWN FILES HERE...

  Delete "$INSTDIR\Uninstall.exe"

  RMDir "$INSTDIR"

  DeleteRegKey /ifempty HKCU "Software\Modern UI Test"

SectionEnd
