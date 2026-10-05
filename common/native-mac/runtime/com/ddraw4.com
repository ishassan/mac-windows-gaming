# DirectDraw 6 interfaces (IDirectDraw4, IDirectDrawSurface4), implemented in
# WinApi-ddraw.c with the same surface objects as the version 1 interfaces.

interface IDirectDraw4
    QueryInterface *riid *ppvObj
    AddRef
    Release
    Compact
    CreateClipper dwFlags *lplpDDClipper *pUnkOuter
    CreatePalette dwFlags *lpColorTable *lplpDDPalette *pUnkOuter
    CreateSurface *lpDDSurfaceDesc2 *lplpDDSurface *pUnkOuter
    DuplicateSurface *lpDDSurface *lplpDupDDSurface
    EnumDisplayModes dwFlags *lpDDSurfaceDesc2 *lpContext *lpEnumModesCallback
    EnumSurfaces dwFlags *lpDDSD2 *lpContext *lpEnumSurfacesCallback
    FlipToGDISurface
    GetCaps *lpDDDriverCaps *lpDDHELCaps
    GetDisplayMode *lpDDSurfaceDesc2
    GetFourCCCodes *lpNumCodes *lpCodes
    GetGDISurface *lplpGDIDDSSurface
    GetMonitorFrequency *lpdwFrequency
    GetScanLine *lpdwScanLine
    GetVerticalBlankStatus *lpbIsInVB
    Initialize *lpGUID
    RestoreDisplayMode
    SetCooperativeLevel *hWnd dwFlags
    SetDisplayMode dwWidth dwHeight dwBPP dwRefreshRate dwFlags
    WaitForVerticalBlank dwFlags *hEvent
    GetAvailableVidMem *lpDDSCaps2 *lpdwTotal *lpdwFree
    GetSurfaceFromDC *hdc *lpDDS4
    RestoreAllSurfaces = 0
    TestCooperativeLevel = 0
    GetDeviceIdentifier *lpdddi dwFlags

interface IDirectDrawSurface4
    QueryInterface *riid *ppvObj
    AddRef
    Release
    AddAttachedSurface *lpDDSAttachedSurface
    AddOverlayDirtyRect *lpRect
    Blt *lpDestRect *lpDDSrcSurface *lpSrcRect dwFlags *lpDDBltFx
    BltBatch *lpDDBltBatch dwCount dwFlags
    BltFast dwX dwY *lpDDSrcSurface *lpSrcRect dwTrans
    DeleteAttachedSurface dwFlags *lpDDSAttachedSurface
    EnumAttachedSurfaces *lpContext *lpEnumSurfacesCallback
    EnumOverlayZOrders dwFlags *lpContext *lpfnCallback
    Flip *lpDDSurfaceTargetOverride dwFlags
    GetAttachedSurface *lpDDSCaps2 *lplpDDAttachedSurface
    GetBltStatus dwFlags = 0
    GetCaps *lpDDSCaps2
    GetClipper *lplpDDClipper
    GetColorKey dwFlags *lpDDColorKey
    GetDC *lphDC
    GetFlipStatus dwFlags = 0
    GetOverlayPosition *lplX *lplY
    GetPalette *lplpDDPalette
    GetPixelFormat *lpDDPixelFormat
    GetSurfaceDesc *lpDDSurfaceDesc2
    Initialize *lpDD *lpDDSurfaceDesc2
    IsLost = 0
    Lock *lpDestRect *lpDDSurfaceDesc2 dwFlags *hEvent
    ReleaseDC *hDC
    Restore = 0
    SetClipper *lpDDClipper
    SetColorKey dwFlags *lpDDColorKey
    SetOverlayPosition lX lY
    SetPalette *lpDDPalette
    Unlock *lpRect
    UpdateOverlay *lpSrcRect *lpDDDestSurface *lpDestRect dwFlags *lpDDOverlayFx
    UpdateOverlayDisplay dwFlags
    UpdateOverlayZOrder dwFlags *lpDDSReference
    GetDDInterface *lplpDD
    PageLock dwFlags = 0
    PageUnlock dwFlags = 0
    SetSurfaceDesc *lpDDSD2 dwFlags
    SetPrivateData *guidTag *lpData cbSize dwFlags
    GetPrivateData *guidTag *lpBuffer *lpcbBufferSize
    FreePrivateData *guidTag
    GetUniquenessValue *lpValue
    ChangeUniquenessValue = 0
