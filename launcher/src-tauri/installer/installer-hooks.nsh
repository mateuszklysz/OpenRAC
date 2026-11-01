; The NSIS installer's look: colours only, read by Modern UI when Tauri's
; template adds its first page. Tauri includes this file before any page and
; nothing here defines a hook macro (NSIS_HOOK_*), so the install steps, paths,
; registry keys and shortcuts stay exactly Tauri's.
; Background: the launcher window's colour (tauri.conf.json backgroundColor).

!define MUI_BGCOLOR "181820"
!define MUI_TEXTCOLOR "F2EEE4"
!define MUI_HEADERIMAGE_RIGHT
!define MUI_INSTFILESPAGE_COLORS "F2EEE4 181820"
