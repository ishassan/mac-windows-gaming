# DirectPlay (dplayx.dll) interfaces, created with CoCreateInstance.
# Single-player only: every method fails with DPERR_UNSUPPORTED (E_NOTIMPL)
# unless WinApi-dplay.c has it. DPERR_NOTLOBBIED = 0x887703f2.

interface IDirectPlay4A
    QueryInterface *riid *ppvObj
    AddRef
    Release
    AddPlayerToGroup idGroup idPlayer
    Close
    CreateGroup *lpidGroup *lpGroupName *lpData dwDataSize dwFlags
    CreatePlayer *lpidPlayer *lpPlayerName hEvent *lpData dwDataSize dwFlags
    DeletePlayerFromGroup idGroup idPlayer
    DestroyGroup idGroup
    DestroyPlayer idPlayer
    EnumGroupPlayers idGroup *lpguidInstance cb ctx dwFlags
    EnumGroups *lpguidInstance cb ctx dwFlags
    EnumPlayers *lpguidInstance cb ctx dwFlags
    EnumSessions *lpsd dwTimeout cb ctx dwFlags
    GetCaps *lpDPCaps dwFlags
    GetGroupData idGroup *lpData *lpdwDataSize dwFlags
    GetGroupName idGroup *lpData *lpdwDataSize
    GetMessageCount idPlayer *lpdwCount
    GetPlayerAddress idPlayer *lpData *lpdwDataSize
    GetPlayerCaps idPlayer *lpPlayerCaps dwFlags
    GetPlayerData idPlayer *lpData *lpdwDataSize dwFlags
    GetPlayerName idPlayer *lpData *lpdwDataSize
    GetSessionDesc *lpData *lpdwDataSize
    Initialize *lpGUID
    Open *lpsd dwFlags
    Receive *lpidFrom *lpidTo dwFlags *lpData *lpdwDataSize
    Send idFrom idTo dwFlags *lpData dwDataSize
    SetGroupData idGroup *lpData dwDataSize dwFlags
    SetGroupName idGroup *lpGroupName dwFlags
    SetPlayerData idPlayer *lpData dwDataSize dwFlags
    SetPlayerName idPlayer *lpPlayerName dwFlags
    SetSessionDesc *lpSessDesc dwFlags
    AddGroupToGroup idParentGroup idGroup
    CreateGroupInGroup idParentGroup *lpidGroup *lpGroupName *lpData dwDataSize dwFlags
    DeleteGroupFromGroup idParentGroup idGroup
    EnumConnections *lpguidApplication cb ctx dwFlags = 0
    EnumGroupsInGroup idGroup *lpguidInstance cb ctx dwFlags
    GetGroupConnectionSettings dwFlags idGroup *lpData *lpdwDataSize
    InitializeConnection *lpConnection dwFlags
    SecureOpen *lpsd dwFlags *lpSecurity *lpCredentials
    SendChatMessage idFrom idTo dwFlags *lpChatMessage
    SetGroupConnectionSettings dwFlags idGroup *lpConnection
    StartSession dwFlags idGroup
    GetGroupFlags idGroup *lpdwFlags
    GetGroupParent idGroup *lpidParent
    GetPlayerAccount idPlayer dwFlags *lpData *lpdwDataSize
    GetPlayerFlags idPlayer *lpdwFlags
    GetGroupOwner idGroup *lpidOwner
    SetGroupOwner idGroup idOwner
    SendEx idFrom idTo dwFlags *lpData dwDataSize dwPriority dwTimeout lpContext *lpdwMsgID
    GetMessageQueue idFrom idTo dwFlags *lpdwNumMsgs *lpdwNumBytes
    CancelMessage dwMsgID dwFlags
    CancelPriority dwMinPriority dwMaxPriority dwFlags

interface IDirectPlayLobby3A
    QueryInterface *riid *ppvObj
    AddRef
    Release
    Connect dwFlags *lplpDP *pUnk
    CreateAddress *guidSP *guidDataType *lpData dwDataSize *lpAddress *lpdwAddressSize
    EnumAddress cb *lpAddress dwAddressSize ctx
    EnumAddressTypes cb *guidSP ctx dwFlags = 0
    EnumLocalApplications cb ctx dwFlags = 0
    GetConnectionSettings dwAppID *lpData *lpdwDataSize = 0x887703f2
    ReceiveLobbyMessage dwFlags dwAppID *lpdwMessageFlags *lpData *lpdwDataSize
    RunApplication dwFlags *lpdwAppID *lpConn hReceiveEvent
    SendLobbyMessage dwFlags dwAppID *lpData dwDataSize
    SetConnectionSettings dwFlags dwAppID *lpConn
    SetLobbyMessageEvent dwFlags dwAppID hReceiveEvent
    CreateCompoundAddress *lpElements dwElementCount *lpAddress *lpdwAddressSize
    ConnectEx dwFlags *riid *lplpDP *pUnk
    RegisterApplication dwFlags *lpAppDesc
    UnregisterApplication dwFlags *guidApplication
    WaitForConnectionSettings dwFlags
