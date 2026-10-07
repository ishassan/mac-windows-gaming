# Imports of the game exes that the Septerra-based glue files do not cover.
# tools/gen_glue.py turns this file into llasm glue and weak C stubs.
#
# Format:  <return> <name> <params...>
#   return: i = 32-bit value, p = pointer (converted), v = void
#   params: a leading "*" marks a pointer (converted to a host pointer)
#   cdecl functions with variable arguments: return "x" (see wsprintfA)
#   "f": the result is a double on the x87 stack (the C function gets the
#        CPU first and pushes it)
#   a second letter gives the calling convention: "c" cdecl (the caller
#   pops the params), "t" thiscall (the first param comes from ecx)
#   "d": a data import (a variable of the DLL): no glue; a runtime C file
#        defines the variable with the name <name>_asm2c
# The C function is <name>_c. stdcall: the glue pops all params.

# KERNEL32
i CompareStringA Locale dwCmpFlags *lpString1 cchCount1 *lpString2 cchCount2
i CompareStringW Locale dwCmpFlags *lpString1 cchCount1 *lpString2 cchCount2
i CopyFileA *lpExistingFileName *lpNewFileName bFailIfExists
p CreateFileW *lpFileName dwDesiredAccess dwShareMode *lpSecurityAttributes dwCreationDisposition dwFlagsAndAttributes *hTemplateFile
v DebugBreak
i EnumSystemLocalesA lpLocaleEnumProc dwFlags
v FatalAppExitA uAction *lpMessageText
i FlushFileBuffers *hFile
i FreeEnvironmentStringsA *lpszEnvironmentBlock
i FreeEnvironmentStringsW *lpszEnvironmentBlock
i FreeLibrary *hLibModule
i GetACP
i GetCPInfo CodePage *lpCPInfo
p GetCommandLineA
i GetConsoleCP
i GetConsoleMode *hConsoleHandle *lpMode
i GetConsoleOutputCP
i GetCurrentDirectoryA nBufferLength *lpBuffer
i GetCurrentProcessId
i GetCurrentThreadId
i GetDateFormatA Locale dwFlags *lpDate *lpFormat *lpDateStr cchDate
i GetDriveTypeA *lpRootPathName
p GetEnvironmentStrings
p GetEnvironmentStringsW
i GetFileType *hFile
i GetLocaleInfoA Locale LCType *lpLCData cchData
i GetLocaleInfoW Locale LCType *lpLCData cchData
i GetModuleFileNameA *hModule *lpFilename nSize
p GetModuleHandleA *lpModuleName
i GetOEMCP
i GetProcAddress *hModule *lpProcName
p GetProcessHeap
v GetStartupInfoA *lpStartupInfo
p GetStdHandle nStdHandle
i GetStringTypeA Locale dwInfoType *lpSrcStr cchSrc *lpCharType
i GetStringTypeW dwInfoType *lpSrcStr cchSrc *lpCharType
i GetSystemDefaultLangID
v GetSystemTimeAsFileTime *lpSystemTimeAsFileTime
i GetTickCount
i GetTimeFormatA Locale dwFlags *lpTime *lpFormat *lpTimeStr cchTime
i GetTimeZoneInformation *lpTimeZoneInformation
i GetUserDefaultLCID
i GetVersionExA *lpVersionInformation
p HeapAlloc *hHeap dwFlags dwBytes
p HeapCreate flOptions dwInitialSize dwMaximumSize
i HeapDestroy *hHeap
i HeapFree *hHeap dwFlags *lpMem
p HeapReAlloc *hHeap dwFlags *lpMem dwBytes
i HeapSize *hHeap dwFlags *lpMem
i InterlockedDecrement *lpAddend
i InterlockedExchange *Target Value
i InterlockedIncrement *lpAddend
i IsDebuggerPresent
i IsValidCodePage CodePage
i IsValidLocale Locale dwFlags
i LCMapStringA Locale dwMapFlags *lpSrcStr cchSrc *lpDestStr cchDest
i LCMapStringW Locale dwMapFlags *lpSrcStr cchSrc *lpDestStr cchDest
p LoadLibraryA *lpLibFileName
i MultiByteToWideChar CodePage dwFlags *lpMultiByteStr cbMultiByte *lpWideCharStr cchWideChar
v OutputDebugStringA *lpOutputString
v RaiseException dwExceptionCode dwExceptionFlags nNumberOfArguments *lpArguments
v RtlUnwind TargetFrame TargetIp *ExceptionRecord ReturnValue
i SetConsoleCtrlHandler HandlerRoutine Add
i SetCurrentDirectoryA *lpPathName
i SetEndOfFile *hFile
i SetEnvironmentVariableA *lpName *lpValue
i SetFileAttributesA *lpFileName dwFileAttributes
i SetHandleCount uNumber
v SetLastError dwErrCode
i SetStdHandle nStdHandle *hHandle
i SetUnhandledExceptionFilter lpTopLevelExceptionFilter
i TerminateProcess *hProcess uExitCode
i TlsAlloc
i TlsFree dwTlsIndex
i TlsGetValue dwTlsIndex
i TlsSetValue dwTlsIndex lpTlsValue
i UnhandledExceptionFilter *ExceptionInfo
p VirtualAlloc *lpAddress dwSize flAllocationType flProtect
i VirtualFree *lpAddress dwSize dwFreeType
i WideCharToMultiByte CodePage dwFlags *lpWideCharStr cchWideChar *lpMultiByteStr cbMultiByte *lpDefaultChar *lpUsedDefaultChar
i WriteConsoleA *hConsoleOutput *lpBuffer nNumberOfCharsToWrite *lpNumberOfCharsWritten *lpReserved
i WriteConsoleW *hConsoleOutput *lpBuffer nNumberOfCharsToWrite *lpNumberOfCharsWritten *lpReserved
p lstrcatA *lpString1 *lpString2
i lstrcmpA *lpString1 *lpString2
i lstrcmpiA *lpString1 *lpString2
p lstrcpyA *lpString1 *lpString2
p lstrcpynA *lpString1 *lpString2 iMaxLength
i lstrlenA *lpString

# USER32
p BeginPaint *hWnd *lpPaint
i BringWindowToTop *hWnd
i CheckDlgButton *hDlg nIDButton uCheck
i ClientToScreen *hWnd *lpPoint
p CreateDialogParamA *hInstance *lpTemplateName *hWndParent lpDialogFunc dwInitParam
i DestroyMenu *hMenu
i DialogBoxParamA *hInstance *lpTemplateName *hWndParent lpDialogFunc dwInitParam
i EndDialog *hDlg nResult
i EndPaint *hWnd *lpPaint
p GetActiveWindow
i GetClientRect *hWnd *lpRect
p GetDC *hWnd
p GetDesktopWindow
i GetDlgCtrlID *hWnd
p GetDlgItem *hDlg nIDDlgItem
i GetDlgItemInt *hDlg nIDDlgItem *lpTranslated bSigned
i GetDlgItemTextA *hDlg nIDDlgItem *lpString cchMax
p GetFocus
p GetForegroundWindow
p GetMenu *hWnd
i GetMenuItemCount *hMenu
i GetMenuItemInfoA *hmenu item fByPosition *lpmii
p GetSubMenu *hMenu nPos
i GetWindowLongA *hWnd nIndex
i GetWindowRect *hWnd *lpRect
i InvalidateRect *hWnd *lpRect bErase
i IsDlgButtonChecked *hDlg nIDButton
i IsIconic *hWnd
p LoadMenuA *hInstance *lpMenuName
i MapVirtualKeyA uCode uMapType
i MoveWindow *hWnd X Y nWidth nHeight bRepaint
i RegisterClassExA *lpwcx
i ReleaseCapture
i ReleaseDC *hWnd *hDC
i SendDlgItemMessageA *hDlg nIDDlgItem Msg wParam lParam
i SendMessageA *hWnd Msg wParam lParam
p SetActiveWindow *hWnd
p SetCapture *hWnd
i SetDlgItemInt *hDlg nIDDlgItem uValue bSigned
i SetDlgItemTextA *hDlg nIDDlgItem *lpString
i SetRect *lprc xLeft yTop xRight yBottom
i SetWindowLongA *hWnd nIndex dwNewLong
i SetWindowPos *hWnd *hWndInsertAfter X Y cx cy uFlags
i SetWindowTextA *hWnd *lpString
i SystemParametersInfoA uiAction uiParam *pvParam fWinIni
i TrackPopupMenu *hMenu uFlags x y nReserved *hWnd *prcRect
i UnregisterClassA *lpClassName *hInstance
p WindowFromPoint x y
x wsprintfA *lpOut *lpFmt
i wvsprintfA *lpOut *lpFmt *arglist

# GDI32
p CreateFontA cHeight cWidth cEscapement cOrientation cWeight bItalic bUnderline bStrikeOut iCharSet iOutPrecision iClipPrecision iQuality iPitchAndFamily *pszFaceName
i ExtTextOutA *hdc x y options *lprect *lpString c *lpDx
i GetDeviceCaps *hdc index
i GetPixel *hdc x y
i GetTextExtentPointA *hdc *lpString c *lpsz
i SetBkColor *hdc color
i SetPixel *hdc x y color

# ADVAPI32
i RegCloseKey hKey
i RegEnumValueA hKey dwIndex *lpValueName *lpcchValueName *lpReserved *lpType *lpData *lpcbData
i RegOpenKeyExA hKey *lpSubKey ulOptions samDesired *phkResult
i RegQueryInfoKeyA hKey *lpClass *lpcchClass *lpReserved *lpcSubKeys *lpcbMaxSubKeyLen *lpcbMaxClassLen *lpcValues *lpcbMaxValueNameLen *lpcbMaxValueLen *lpcbSecurityDescriptor *lpftLastWriteTime

# SHELL32
v DragAcceptFiles *hWnd fAccept
v DragFinish hDrop
i DragQueryFileA hDrop iFile *lpszFile cch
i SHCreateDirectoryExA *hwnd *pszPath *psa
i SHGetFolderPathA *hwnd csidl *hToken dwFlags *pszPath

# SHLWAPI
i PathAppendA *pszPath *pszMore
i PathFileExistsA *pszPath

# ole32
i CoCreateInstance *rclsid *pUnkOuter dwClsContext *riid *ppv
i CoInitialize *pvReserved
v CoUninitialize

# WSOCK32 (multiplayer is not supported: these fail)
i ws_accept s *addr *addrlen
i ws_bind s *name namelen
i ws_closesocket s
i ws_connect s *name namelen
p ws_gethostbyname *name
i ws_gethostname *name namelen
i ws_htons hostshort
i ws_inet_addr *cp
p ws_inet_ntoa in
i ws_ioctlsocket s cmd *argp
i ws_listen s backlog
i ws_recv s *buf len flags
i ws_select nfds *readfds *writefds *exceptfds *timeout
i ws_send s *buf len flags
i ws_setsockopt s level optname *optval optlen
i ws_socket af type protocol
i ws_WSAStartup wVersionRequested *lpWSAData
i ws_WSACleanup

# mss32 (Miles Sound System subset)
i AIL_allocate_sample_handle dig
i AIL_close_stream stream
i AIL_digital_master_volume dig
v AIL_end_sample S
i AIL_get_preference number
v AIL_init_sample S
v AIL_lock
p AIL_mem_alloc_lock size
v AIL_mem_free_lock *ptr
i AIL_open_stream dig *filename stream_mem
v AIL_pause_stream stream onoff
v AIL_redbook_close hand
i AIL_redbook_open which
i AIL_redbook_play hand startms endms
i AIL_redbook_position hand
i AIL_redbook_set_volume hand volume
i AIL_redbook_stop hand
v AIL_redbook_track_info hand tracknum *startms *endms
i AIL_redbook_tracks hand
i AIL_redbook_volume hand
v AIL_release_sample_handle S
i AIL_sample_status S
i AIL_set_DirectSound_HWND dig *wnd
v AIL_set_digital_master_volume dig master_volume
i AIL_set_preference number value
i AIL_set_sample_file S *file_image block
v AIL_set_sample_loop_count S loop_count
v AIL_set_sample_pan S pan
v AIL_set_sample_volume S volume
v AIL_set_stream_loop_count stream count
v AIL_set_stream_pan stream pan
v AIL_set_stream_volume stream volume
i AIL_shutdown
v AIL_start_sample S
v AIL_start_stream stream
i AIL_startup
i AIL_stream_pan stream
i AIL_stream_status stream
i AIL_stream_volume stream
v AIL_unlock
i AIL_waveOutOpen *drvr *lphWaveOut wDeviceID *lpFormat

# ---- Added for Revenant (MSVC 6, DirectX 5/6, Miles 3D, Smacker) ----
# KERNEL32: threads and synchronization (WinApi-sync.c)
p CreateEventA *lpEventAttributes bManualReset bInitialState *lpName
i SetEvent *hEvent
i ResetEvent *hEvent
i PulseEvent *hEvent
i ReleaseMutex *hMutex
i WaitForSingleObject *hHandle dwMilliseconds
i WaitForMultipleObjects nCount *lpHandles bWaitAll dwMilliseconds
p CreateThread *lpThreadAttributes dwStackSize lpStartAddress lpParameter dwCreationFlags *lpThreadId
v ExitThread dwExitCode
i CreateProcessA *lpApplicationName *lpCommandLine *lpProcessAttributes *lpThreadAttributes bInheritHandles dwCreationFlags *lpEnvironment *lpCurrentDirectory *lpStartupInfo *lpProcessInformation
# KERNEL32: files, time, system (WinApi-kernel32-extra.c)
i FileTimeToDosDateTime *lpFileTime *lpFatDate *lpFatTime
i GetFileAttributesA *lpFileName
i GetFileInformationByHandle *hFile *lpFileInformation
v GetLocalTime *lpSystemTime
v GetSystemTime *lpSystemTime
i SystemTimeToFileTime *lpSystemTime *lpFileTime
i GetLogicalDrives
i GetVolumeInformationA *lpRootPathName *lpVolumeNameBuffer nVolumeNameSize *lpVolumeSerialNumber *lpMaximumComponentLength *lpFileSystemFlags *lpFileSystemNameBuffer nFileSystemNameSize
i GetPrivateProfileIntA *lpAppName *lpKeyName nDefault *lpFileName
i WritePrivateProfileStringA *lpAppName *lpKeyName *lpString *lpFileName
i GetVersion
p GlobalAlloc uFlags dwBytes
p GlobalFree *hMem
v GlobalMemoryStatus *lpBuffer
i IsBadCodePtr lpfn
i IsBadReadPtr *lp ucb
i IsBadWritePtr *lp ucb
i MoveFileA *lpExistingFileName *lpNewFileName
i PeekNamedPipe *hNamedPipe *lpBuffer nBufferSize *lpBytesRead *lpTotalBytesAvail *lpBytesLeftThisMessage
i VirtualLock *lpAddress dwSize
i VirtualUnlock *lpAddress dwSize
# USER32
p CreateDialogIndirectParamA *hInstance *lpTemplate *hWndParent lpDialogFunc dwInitParam
i DrawTextA *hdc *lpchText cchText *lprc format
i DrawTextExA *hdc *lpchText cchText *lprc format *lpdtp
i EnableWindow *hWnd bEnable
i GetKeyboardState *lpKeyState
p GetWindow *hWnd uCmd
p GetWindowDC *hWnd
i IntersectRect *lprcDst *lprcSrc1 *lprcSrc2
i IsWindow *hWnd
i KillTimer *hWnd uIDEvent
i SetTimer *hWnd nIDEvent uElapse lpTimerFunc
i LoadStringA *hInstance uID *lpBuffer cchBufferMax
i MapWindowPoints *hWndFrom *hWndTo *lpPoints cPoints
i TranslateAcceleratorA *hWnd *hAccTable *lpMsg
p WindowFromDC *hDC
# GDI32
i GetClipBox *hdc *lprect
i GetCurrentPositionEx *hdc *lppoint
i GetTextExtentExPointA *hdc *lpszString cchString nMaxExtent *lpnFit *lpnDx *lpSize
i GetTextExtentPoint32A *hdc *lpString c *lpSize
i GetTextMetricsA *hdc *lptm
i MoveToEx *hdc x y *lppt
i SetTextAlign *hdc align
i SetTextCharacterExtra *hdc extra
i SetViewportOrgEx *hdc x y *lppt
# ADVAPI32, SHELL32
i GetUserNameA *lpBuffer *pcbBuffer
i Shell_NotifyIconA dwMessage *lpData
# WINMM (Revenant imports these from _INMM.dll, the GOG copy of winmm)
i timeBeginPeriod uPeriod
i timeEndPeriod uPeriod
i timeGetDevCaps *ptc cbtc
i timeSetEvent uDelay uResolution lpTimeProc dwUser fuEvent
i timeKillEvent uTimerID
# DDRAW, DINPUT
i DirectDrawEnumerateA lpCallback lpContext
i DirectInputCreateA *hinst dwVersion *lplpDirectInput *punkOuter
# MSS32 (Miles 3D and more sample functions; float parameters arrive as raw dwords)
i AIL_3D_position S *x *y *z
i AIL_3D_provider_attribute lib *name *val
i AIL_3D_sample_status S
i AIL_3D_user_data S index
p AIL_allocate_3D_sample_handle lib
v AIL_close_3D_listener listener
v AIL_close_3D_provider lib
v AIL_end_3D_sample S
i AIL_enumerate_3D_providers *next *dest *name
p AIL_open_3D_listener lib
i AIL_open_3D_provider lib
i AIL_redbook_status hand
i AIL_register_EOS_callback S EOS
v AIL_release_3D_sample_handle S
v AIL_resume_3D_sample S
v AIL_resume_sample S
v AIL_sample_ms_position S *total_milliseconds *current_milliseconds
i AIL_sample_user_data S index
v AIL_set_3D_position S x y z
i AIL_set_3D_provider_preference lib *name *val
i AIL_set_3D_sample_file S *file_image
v AIL_set_3D_sample_float_distances S max_dist min_dist max_front_dist min_front_dist
v AIL_set_3D_sample_loop_count S loops
v AIL_set_3D_sample_volume S volume
v AIL_set_3D_user_data S index value
i AIL_set_named_sample_file S *file_type_suffix *file_image file_size block
v AIL_set_sample_user_data S index value
v AIL_start_3D_sample S
v AIL_stop_3D_sample S
v AIL_stop_sample S
v AIL_waveOutClose drvr
# SMACKW32 (WinApi-smack.c); Smack handles are guest pointers to a SMACK struct
p SmackOpen *name flags extrabuf
v SmackClose *smk
i SmackDoFrame *smk
v SmackNextFrame *smk
i SmackWait *smk
v SmackToBuffer *smk left top pitch destheight *buf flags
i SmackSoundOnOff *smk on
i SmackDDSurfaceType *lpDDS
v SmackVolumePan *smk trackflag volume pan
i SmackSoundUseMSS *dd
# ---- Added for Generals Zero Hour (MSVC 6 with MSVCRT.dll, DirectX 8, Miles 6.5, Bink) ----
# KERNEL32 (WinApi-kernel32-gzh.c)
i DosDateTimeToFileTime wFatDate wFatTime *lpFileTime
i FormatMessageA dwFlags *lpSource dwMessageId dwLanguageId *lpBuffer nSize *Arguments
i FormatMessageW dwFlags *lpSource dwMessageId dwLanguageId *lpBuffer nSize *Arguments
i GetComputerNameA *lpBuffer *nSize
i GetDateFormatW Locale dwFlags *lpDate *lpFormat *lpDateStr cchDate
i GetFileSize *hFile *lpFileSizeHigh
i GetPriorityClass *hProcess
i GetTempPathA nBufferLength *lpBuffer
i GetThreadPriority *hThread
i GetTimeFormatW Locale dwFlags *lpTime *lpFormat *lpTimeStr cchTime
p GlobalHandle *pMem
p GlobalLock *hMem
i GlobalUnlock *hMem
i IsProcessorFeaturePresent ProcessorFeature
p LocalAlloc uFlags uBytes
p LocalFree *hMem
p MapViewOfFileEx *hFileMappingObject dwDesiredAccess dwFileOffsetHigh dwFileOffsetLow dwNumberOfBytesToMap *lpBaseAddress
i MulDiv nNumber nNumerator nDenominator
p OpenEventA dwDesiredAccess bInheritHandle *lpName
i SetFileTime *hFile *lpCreationTime *lpLastAccessTime *lpLastWriteTime
i TerminateThread *hThread dwExitCode
i UnmapViewOfFile *lpBaseAddress
# USER32
i AdjustWindowRect *lpRect dwStyle bMenu
p FindWindowA *lpClassName *lpWindowName
i GetDoubleClickTime
i GetKeyboardLayout idThread
p LoadCursorFromFileA *lpFileName
i MessageBoxW *hWnd *lpText *lpCaption uType
i ScreenToClient *hWnd *lpPoint
i SetForegroundWindow *hWnd
i SetWindowTextW *hWnd *lpString
# GDI32
i AddFontResourceA *lpszFilename
p CreateDIBSection *hdc *pbmi usage *ppvBits *hSection offset
i ExtTextOutW *hdc X Y fuOptions *lprc *lpString cbCount *lpDx
i GetTextExtentPoint32W *hdc *lpString c *lpSize
i RemoveFontResourceA *lpFileName
i RestoreDC *hdc nSavedDC
i SaveDC *hdc
i SetDeviceGammaRamp *hDC *lpRamp
# ADVAPI32 (registry keys are plain values, see RegOpenKeyExA)
i RegCreateKeyExA hKey *lpSubKey Reserved *lpClass dwOptions samDesired *lpSecurityAttributes *phkResult *lpdwDisposition
i RegOpenKeyA hKey *lpSubKey *phkResult
i RegQueryValueExA hKey *lpValueName *lpReserved *lpType *lpData *lpcbData
i RegSetValueExA hKey *lpValueName Reserved dwType *lpData cbData
# SHELL32
i SHGetPathFromIDListA *pidl *pszPath
i SHGetSpecialFolderLocation *hwndOwner nFolder *ppidl
i SHGetSpecialFolderPathA *hwndOwner *lpszPath csidl fCreate
# ole32, OLEAUT32 (the web browser of the online lobby: not supported)
i OleInitialize *pvReserved
i OleRun *pUnknown
v OleUninitialize
i GetErrorInfo dwReserved *pperrinfo
i CreateStdDispatch *punkOuter *pvThis *ptinfo *ppunkStdDisp
i LoadTypeLib *szFile *pptlib
p SysAllocString *psz
v SysFreeString *bstr
i VariantClear *pvarg
# IMM32 (input method editor: not supported; input contexts are plain values)
i ImmAssociateContext *hWnd hIMC
i ImmCreateContext
i ImmDestroyContext hIMC
i ImmGetCandidateListA hIMC deIndex *lpCandList dwBufLen
i ImmGetCandidateListW hIMC deIndex *lpCandList dwBufLen
i ImmGetCandidateListCountA hIMC *lpdwListCount
i ImmGetCandidateListCountW hIMC *lpdwListCount
i ImmGetCompositionStringA hIMC dwIndex *lpBuf dwBufLen
i ImmGetCompositionStringW hIMC dwIndex *lpBuf dwBufLen
i ImmGetContext *hWnd
i ImmGetProperty hKL fdwIndex
i ImmReleaseContext *hWnd hIMC
# DBGHELP (crash reports: not supported, all fail)
i StackWalk MachineType *hProcess *hThread *StackFrame *ContextRecord ReadMemoryRoutine FunctionTableAccessRoutine GetModuleBaseRoutine TranslateAddress
i SymCleanup *hProcess
i SymFunctionTableAccess *hProcess AddrBase
i SymGetModuleBase *hProcess dwAddr
i SymGetSymFromAddr *hProcess dwAddr *pdwDisplacement *Symbol
i SymInitialize *hProcess *UserSearchPath fInvadeProcess
i SymLoadModule *hProcess *hFile *ImageName *ModuleName BaseOfDll SizeOfDll
i SymSetOptions SymOptions
# AVIFIL32 (movie capture: not supported, all fail)
v AVIFileInit
v AVIFileExit
i AVIFileOpenA *ppfile *szFile uMode *lpHandler
i AVIFileCreateStreamA *pfile *ppavi *psi
i AVIStreamSetFormat *pavi lPos *lpFormat cbFormat
i AVIStreamWrite *pavi lStart lSamples *lpBuffer cbBuffer dwFlags *plSampWritten *plBytesWritten
i AVIFileRelease *pfile
i AVIStreamRelease *pavi
# DINPUT8
i DirectInput8Create *hinst dwVersion *riidltf *ppvOut *punkOuter
# WSOCK32
i ws_WSAGetLastError
i ws___WSAFDIsSet s *set
i ws_getsockname s *name *namelen
i ws_getsockopt s level optname *optval *optlen
i ws_htonl hostlong
i ws_ntohl netlong
i ws_ntohs netshort
i ws_recvfrom s *buf len flags *from *fromlen
i ws_sendto s *buf len flags *to tolen
i ws_shutdown s how
# MSS32 6.5 (handles are plain values; float parameters arrive as raw dwords)
i AIL_3D_sample_playback_rate S
i AIL_WAV_info *data *info
i AIL_decompress_ADPCM *info *outdata *outsize
i AIL_enumerate_filters *next *dest *name
v AIL_get_DirectSound_info S *lplpDS *lplpDSB
v AIL_quick_handles *pdig *pmdi *pdls
i AIL_quick_load_and_play *filename loop_count wait_request
v AIL_quick_set_volume audio volume extravol
i AIL_quick_startup use_digital use_MIDI output_rate output_bits output_channels
v AIL_quick_unload audio
i AIL_register_3D_EOS_callback S EOS
i AIL_register_stream_callback stream callback
i AIL_sample_playback_rate S
v AIL_sample_volume_pan S *volume *pan
v AIL_set_3D_orientation obj X_face Y_face Z_face X_up Y_up Z_up
v AIL_set_3D_sample_distances S max_dist min_dist
v AIL_set_3D_sample_occlusion S occlusion
v AIL_set_3D_sample_playback_rate S playback_rate
v AIL_set_3D_speaker_type lib speaker_type
v AIL_set_file_callbacks opencb closecb seekcb readcb
i AIL_set_filter_sample_preference S *name *val
p AIL_set_redist_directory *dir
v AIL_set_sample_playback_rate S playback_rate
i AIL_set_sample_processor S pipeline_stage provider
v AIL_set_sample_volume_pan S volume pan
v AIL_set_stream_volume_pan stream volume pan
i AIL_stream_loop_count stream
v AIL_stream_ms_position stream *total_milliseconds *current_milliseconds
v AIL_stream_volume_pan stream *volume *pan
# BINKW32 (WinApi-binkw32.c); Bink handles are guest pointers to a BINK struct
p BinkOpen *name flags
v BinkClose *bnk
i BinkCopyToBuffer *bnk *dest destpitch destheight destx desty flags
i BinkDoFrame *bnk
v BinkGoto *bnk frame flags
v BinkNextFrame *bnk
i BinkOpenDirectSound param
i BinkSetSoundSystem open param
v BinkSetSoundTrack total_tracks *tracks
v BinkSetVolume *bnk trackid volume
i BinkWait *bnk
# MSVCRT, MSVCIRT (WinApi-msvcrt.c): cdecl ("c"); Windows wide characters are 16-bit
ic _XcptFilter xcptnum *pxcptinfoptrs
ic __dllonexit func *pbegin *pend
ic __getmainargs *argc *argv *env doWildCard *startupinfo
pc __p__commode
pc __p__fmode
vc __set_app_type apptype
vc __setusermatherr handler
ic _access *path mode
ic _chmod *path mode
ic _close fd
ic _controlfp new mask
pc _errno
ic _except_handler3 ExceptionRecord EstablisherFrame ContextRecord DispatcherContext
v _CxxThrowException pExceptionObject pThrowInfo
vc _exit status
ic _finite xlo xhi
vc _fpreset
ic _fstat fd *buf
pc _getcwd *buf size
vc _initterm *pfbegin *pfend
ic _isctype c mask
ic _isnan xlo xhi
pc _itoa value *str radix
ic _lseek fd offset origin
ic _mbscmp *s1 *s2
ic _mbslen *s
ic _mbsnccnt *s n
ic _mkdir *path
ic _onexit func
x _open *path oflag
ic _read fd *buf count
x _snprintf *buf count *format
x _spawnl mode *cmdname
vc _splitpath *path *drive *dir *fname *ext
ic _stat *path *buf
ic _statusfp
pc _strdup *s
pc _strlwr *s
ic _strnicmp *s1 *s2 n
pc _strupr *s
ic _vsnprintf *buf count *format *ap
ic _vsnwprintf *buf count *format *ap
ic _wcsicmp *s1 *s2
ic _write fd *buf count
ic _wtoi *s
pc asctime *tm
fc atof *s
ic atoi *s
pc bsearch *key *base num width compare
ic clock
vc exit status
ic fclose *f
ic fflush *f
ic fgetc *f
pc fgets *s n *f
ic fgetwc *f
pc fopen *filename *mode
x fprintf *f *format
ic fputc c *f
ic fputwc c *f
ic fread *ptr size n *f
x fscanf *f *format
ic fseek *f offset origin
ic ftell *f
x fwprintf *f *format
ic fwrite *ptr size n *f
ic isalnum c
ic isalpha c
ic isdigit c
ic isspace c
ic iswalnum c
ic iswalpha c
ic iswascii c
ic iswdigit c
ic iswspace c
pc localtime *timer
pc memmove *dest *src n
vc qsort *base num width compare
pc realloc *ptr size
ic remove *path
ic rename *oldname *newname
vc rewind *f
pc setlocale category *locale
pc strchr *s c
ic strcspn *s1 *s2
ic strftime *s max *format *tm
pc strpbrk *s1 *s2
pc strrchr *s c
ic strspn *s1 *s2
pc strstr *s1 *s2
pc strtok *s *delim
ic strtol *s *endptr base
ic strtoul *s *endptr base
x swscanf *s *format
ic time *timer
ic tolower c
ic vsprintf *buf *format *ap
pc wcscat *dest *src
pc wcschr *s c
ic wcscmp *s1 *s2
pc wcscpy *dest *src
ic wcscspn *s1 *s2
ic wcslen *s
pc wcsncpy *dest *src n
pc wcsrchr *s c
ic wcsspn *s1 *s2
pc wcsstr *s1 *s2
pt exception_ctor *this
pt exception_copy_ctor *this *other
vt exception_dtor *this
vt type_info_dtor *this
# vftable of the MSVCIRT class exception (not imports: WinApi-msvcrt.c takes their addresses)
pt exception_sdtor *this flags
pt exception_what *this
vc cxx_terminate
ic cxx_set_se_translator func
d _iob
d _acmdln
d _pctype
d __mb_cur_max
d _adjust_fdiv
# D3D8 (WinApi-d3d8.c): loaded with LoadLibraryA("D3D8.DLL") and GetProcAddress
p Direct3DCreate8 SDKVersion
