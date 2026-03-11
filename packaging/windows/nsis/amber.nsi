Target amd64-unicode

!include "MUI.nsh"

!define MUI_ICON "install icon.ico"
!define MUI_UNICON "uninstall icon.ico"

!define APP_NAME "Amber"
!define APP_TARGET "amber-editor"

!define MUI_FINISHPAGE_RUN "$INSTDIR\amber-editor.exe"

SetCompressor lzma

Name ${APP_NAME}
OutFile "amber-setup.exe"

!ifdef X64
InstallDir "$PROGRAMFILES64\${APP_NAME}"
!else
InstallDir "$PROGRAMFILES32\${APP_NAME}"
!endif

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_LICENSE LICENSE
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_NOAUTOCLOSE
!define MUI_FINISHPAGE_RUN_TEXT "Run ${APP_NAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchAmber"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_LANGUAGE "English"

Section "Amber (required)"

	SectionIn RO

	SetOutPath $INSTDIR

	File /r amber\*

	WriteUninstaller "$INSTDIR\uninstall.exe"

SectionEnd

Section "Create Desktop shortcut"
	CreateShortCut "$DESKTOP\${APP_NAME}.lnk" "$INSTDIR\${APP_TARGET}.exe"
SectionEnd

Section "Create Start Menu shortcut"
	CreateDirectory "$SMPROGRAMS\${APP_NAME}"
	CreateShortCut "$SMPROGRAMS\${APP_NAME}\${APP_NAME}.lnk" "$INSTDIR\${APP_TARGET}.exe"
	CreateShortCut "$SMPROGRAMS\${APP_NAME}\Uninstall ${APP_NAME}.lnk" "$INSTDIR\uninstall.exe"
SectionEnd

Section "Associate *.ove files with Amber"
	WriteRegStr HKCR ".ove" "" "AmberEditor.OVEFile"
	WriteRegStr HKCR ".ove" "Content Type" "application/vnd.olive-project"
	WriteRegStr HKCR "AmberEditor.OVEFile" "" "Amber project file"
	WriteRegStr HKCR "AmberEditor.OVEFile\DefaultIcon" "" "$INSTDIR\amber-editor.exe,1"
	WriteRegStr HKCR "AmberEditor.OVEFile\shell\open\command" "" "$\"$INSTDIR\amber-editor.exe$\" $\"%1$\""
	System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd

UninstPage uninstConfirm
UninstPage instfiles

Section "uninstall"

	rmdir /r "$INSTDIR"

	Delete "$DESKTOP\${APP_NAME}.lnk"
	rmdir /r "$SMPROGRAMS\${APP_NAME}"

	DeleteRegKey HKCR ".ove"
	DeleteRegKey HKCR "AmberEditor.OVEFile"
	DeleteRegKey HKCR "AmberEditor.OVEFile\DefaultIcon" ""
	DeleteRegKey HKCR "AmberEditor.OVEFile\shell\open\command" ""
	System::Call 'shell32.dll::SHChangeNotify(i, i, i, i) v (0x08000000, 0, 0, 0)'
SectionEnd

Function LaunchAmber
	ExecShell "" "$INSTDIR\${APP_TARGET}.exe"
FunctionEnd
