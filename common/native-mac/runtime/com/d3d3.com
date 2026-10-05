# Direct3D 6 immediate mode (IDirect3D3 and the objects it makes), implemented
# in WinApi-d3d.c with a software rasterizer that draws into the DirectDraw
# surfaces (16-bit color, 16-bit z-buffer).

interface IDirect3D3
    QueryInterface *riid *ppvObj
    AddRef
    Release
    EnumDevices lpEnumDevicesCallback *lpUserArg
    CreateLight *lplpDirect3DLight *pUnkOuter
    CreateMaterial *lplpDirect3DMaterial3 *pUnkOuter
    CreateViewport *lplpD3DViewport3 *pUnkOuter
    FindDevice *lpD3DFDS *lpD3DFDR
    CreateDevice *rclsid *lpDDS *lplpD3DDevice3 *pUnkOuter
    CreateVertexBuffer *lpVBDesc *lpD3DVertexBuffer dwFlags *pUnkOuter
    EnumZBufferFormats *riidDevice lpEnumCallback *lpContext
    EvictManagedTextures = 0

interface IDirect3DDevice3
    QueryInterface *riid *ppvObj
    AddRef
    Release
    GetCaps *lpD3DHWDevDesc *lpD3DHELDevDesc
    GetStats *lpD3DStats
    AddViewport *lpDirect3DViewport
    DeleteViewport *lpDirect3DViewport
    NextViewport *lpDirect3DViewport *lplpAnotherViewport dwFlags
    EnumTextureFormats lpd3dEnumPixelProc *lpArg
    BeginScene = 0
    EndScene = 0
    GetDirect3D *lplpD3D
    SetCurrentViewport *lpd3dViewport
    GetCurrentViewport *lplpd3dViewport
    SetRenderTarget *lpNewRenderTarget dwFlags
    GetRenderTarget *lplpRenderTarget
    Begin d3dpt dwVertexTypeDesc dwFlags
    BeginIndexed dptPrimitiveType dwVertexTypeDesc *lpvVertices dwNumVertices dwFlags
    Vertex *lpVertexType
    Index wVertexIndex
    End dwFlags
    GetRenderState dwRenderStateType *lpdwRenderState
    SetRenderState dwRenderStateType dwRenderState
    GetLightState dwLightStateType *lpdwLightState
    SetLightState dwLightStateType dwLightState
    SetTransform dtstTransformStateType *lpD3DMatrix
    GetTransform dtstTransformStateType *lpD3DMatrix
    MultiplyTransform dtstTransformStateType *lpD3DMatrix
    DrawPrimitive dptPrimitiveType dwVertexTypeDesc *lpvVertices dwVertexCount dwFlags
    DrawIndexedPrimitive d3dptPrimitiveType dwVertexTypeDesc *lpvVertices dwVertexCount *lpwIndices dwIndexCount dwFlags
    SetClipStatus *lpD3DClipStatus
    GetClipStatus *lpD3DClipStatus
    DrawPrimitiveStrided dptPrimitiveType dwVertexTypeDesc *lpVertexArray dwVertexCount dwFlags
    DrawIndexedPrimitiveStrided d3dptPrimitiveType dwVertexTypeDesc *lpVertexArray dwVertexCount *lpwIndices dwIndexCount dwFlags
    DrawPrimitiveVB d3dptPrimitiveType *lpD3DVertexBuf dwStartVertex dwNumVertices dwFlags
    DrawIndexedPrimitiveVB d3dptPrimitiveType *lpD3DVertexBuf *lpwIndices dwIndexCount dwFlags
    ComputeSphereVisibility *lpCenters *lpRadii dwNumSpheres dwFlags *lpdwReturnValues
    GetTexture dwStage *lplpTexture2
    SetTexture dwStage *lpTexture2
    GetTextureStageState dwStage dwState *lpdwValue
    SetTextureStageState dwStage dwState dwValue
    ValidateDevice *lpdwPasses

interface IDirect3DViewport3
    QueryInterface *riid *ppvObj
    AddRef
    Release
    Initialize *lpDirect3D
    GetViewport *lpData
    SetViewport *lpData
    TransformVertices dwVertexCount *lpData dwFlags *lpOffscreen
    LightElements dwElementCount *lpData
    SetBackground hMat
    GetBackground *lphMat *lpValid
    SetBackgroundDepth *lpDDSurface
    GetBackgroundDepth *lplpDDSurface *lpValid
    Clear dwCount *lpRects dwFlags
    AddLight *lpDirect3DLight
    DeleteLight *lpDirect3DLight
    NextLight *lpDirect3DLight *lplpDirect3DLight dwFlags
    GetViewport2 *lpData
    SetViewport2 *lpData
    SetBackgroundDepth2 *lpDDS
    GetBackgroundDepth2 *lplpDDS *lpValid
    Clear2 dwCount *lpRects dwFlags dwColor dvZ_bits dwStencil

interface IDirect3DLight
    QueryInterface *riid *ppvObj
    AddRef
    Release
    Initialize *lpDirect3D
    SetLight *lpLight
    GetLight *lpLight

interface IDirect3DMaterial3
    QueryInterface *riid *ppvObj
    AddRef
    Release
    SetMaterial *lpMat
    GetMaterial *lpMat
    GetHandle *lpDirect3DDevice3 *lpHandle

interface IDirect3DTexture2
    QueryInterface *riid *ppvObj
    AddRef
    Release
    GetHandle *lpDirect3DDevice2 *lpHandle
    PaletteChanged dwStart dwCount
    Load *lpD3DTexture2
