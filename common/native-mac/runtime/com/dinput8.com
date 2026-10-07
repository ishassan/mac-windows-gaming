# DirectInput 8 (IDirectInput8A, IDirectInputDevice8A): WinApi-dinput8.c
# passes the calls to the DirectInput 5 code (WinApi-dinput.c); the first
# methods of the interfaces are the same.

interface IDirectInput8A
    QueryInterface *riid *ppvObj
    AddRef
    Release
    CreateDevice *rguid *lplpDirectInputDevice *pUnkOuter
    EnumDevices dwDevType lpCallback pvRef dwFlags
    GetDeviceStatus *rguidInstance
    RunControlPanel *hwndOwner dwFlags = 0
    Initialize *hinst dwVersion = 0
    FindDevice *rguidClass *ptszName *pguidInstance
    EnumDevicesBySemantics *ptszUserName *lpdiActionFormat lpCallback *pvRef dwFlags = 0
    ConfigureDevices lpdiCallback *lpdiCDParams dwFlags *pvRefData

interface IDirectInputDevice8A
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
    EnumEffectsInFile *lpszFileName pec *pvRef dwFlags
    WriteEffectToFile *lpszFileName dwEntries *rgDiFileEft dwFlags
    BuildActionMap *lpdiaf *lpszUserName dwFlags
    SetActionMap *lpdiActionFormat *lptszUserName dwFlags
    GetImageInfo *lpdiDevImageInfoHeader
