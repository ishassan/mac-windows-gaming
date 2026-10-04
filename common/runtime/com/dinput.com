# DirectInput 5 (IDirectInputA, IDirectInputDevice2A), implemented in
# WinApi-dinput.c on the SDL keyboard and mouse state.

interface IDirectInputA
    QueryInterface *riid *ppvObj
    AddRef
    Release
    CreateDevice *rguid *lplpDirectInputDevice *pUnkOuter
    EnumDevices dwDevType lpCallback pvRef dwFlags
    GetDeviceStatus *rguidInstance
    RunControlPanel *hwndOwner dwFlags = 0
    Initialize *hinst dwVersion = 0

interface IDirectInputDevice2A
    QueryInterface *riid *ppvObj
    AddRef
    Release
    GetCapabilities *lpDIDevCaps
    EnumObjects lpCallback *pvRef dwFlags
    GetProperty rguidProp *pdiph
    SetProperty rguidProp *pdiph
    Acquire
    Unacquire
    GetDeviceState cbData *lpvData
    GetDeviceData cbObjectData *rgdod *pdwInOut dwFlags
    SetDataFormat *lpdf
    SetEventNotification *hEvent
    SetCooperativeLevel *hwnd dwFlags
    GetObjectInfo *pdidoi dwObj dwHow
    GetDeviceInfo *pdidi
    RunControlPanel *hwndOwner dwFlags = 0
    Initialize *hinst dwVersion *rguid = 0
    CreateEffect *rguid *lpeff *ppdeff *punkOuter
    EnumEffects lpCallback *pvRef dwEffType = 0
    GetEffectInfo *pdei *rguid
    GetForceFeedbackState *pdwOut
    SendForceFeedbackCommand dwFlags
    EnumCreatedEffectObjects lpCallback *pvRef fl = 0
    Escape *pesc
    Poll = 0
    SendDeviceData cbObjectData *rgdod *pdwInOut fl
