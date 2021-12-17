#ifndef _INCLUDE_SIGSEGV_STUB_HLTVSERVER_H_
#define _INCLUDE_SIGSEGV_STUB_HLTVSERVER_H_


#include "link/link.h"

class CBaseServer : public IServer
{
public:
    IClient *CreateFakeClient(const char *name)                      { return ft_CreateFakeClient(this, name); }
    
private:
    static MemberFuncThunk<CBaseServer *, IClient *, const char *>              ft_CreateFakeClient;
};

class CHLTVServer : public IGameEventListener2, public CBaseServer
{
public:
    void StartMaster(IClient *client)      {        ft_StartMaster(this, client); }
    int CountClientFrames()                { return ft_CountClientFrames(this); }
    void RemoveOldestFrame()               {        ft_RemoveOldestFrame(this); }
    
private:
    static MemberFuncThunk<CHLTVServer *, void, IClient *>   ft_StartMaster;
    static MemberFuncThunk<CHLTVServer *, int>               ft_CountClientFrames;
    static MemberFuncThunk<CHLTVServer *, void>              ft_RemoveOldestFrame;
};

class CGameClient 
{
public:
    bool ShouldSendMessages()                      { return ft_ShouldSendMessages(this); }
    
private:
    static MemberFuncThunk<CGameClient *, bool>              ft_ShouldSendMessages;
};

typedef struct CustomFile_s
{
	CRC32_t			crc;	//file CRC
	unsigned int	reqID;	// download request ID
} CustomFile_t;

class CBaseClient : public IGameEventListener2, public IClient, public IClientMessageHandler {
public:
	virtual void UpdateUserSettings();
	int				m_nClientSlot;	
	// entity index of this client (different from clientSlot+1 in HLTV and Replay mode):
	int				m_nEntityIndex;	
	
	int				m_UserID;			// identifying number on server
	char			m_Name[MAX_PLAYER_NAME_LENGTH];			// for printing to other people
	char			m_GUID[SIGNED_GUID_LEN + 1]; // the clients CD key

	CSteamID		m_SteamID;			// This is valid when the client is authenticated
	
	uint32			m_nFriendsID;		// client's friends' ID
	char			m_FriendsName[MAX_PLAYER_NAME_LENGTH];

	KeyValues		*m_ConVars;			// stores all client side convars
	bool			m_bConVarsChanged;	// true if convars updated and not changes process yet
	bool			m_bInitialConVarsSet; // Has the client sent their initial set of convars
	bool			m_bSendServerInfo;	// true if we need to send server info packet to start connect
	IServer		*m_Server;			// pointer to server object
	bool			m_bIsHLTV;			// if this a HLTV proxy ?
	bool			m_bIsReplay;		// if this is a Replay proxy ?
	int				m_clientChallenge;	// client's challenge he sent us, we use to auth replies
	
	// Client sends this during connection, so we can see if
	//  we need to send sendtable info or if the .dll matches
	CRC32_t			m_nSendtableCRC;

	// a client can have couple of cutomized files distributed to all other players
	CustomFile_t	m_nCustomFiles[MAX_CUSTOM_FILES];
};

class CHLTVClient : public CBaseClient {

};

class CNetworkStringTable  : public INetworkStringTable
{
public:
	virtual			~CNetworkStringTable( void );
	virtual void	Dump( void );
	virtual void	Lock( bool bLock );

	TABLEID					m_id;
	char					*m_pszTableName;
	// Must be a power of 2, so encoding can determine # of bits to use based on log2
	int						m_nMaxEntries;
	int						m_nEntryBits;
	int						m_nTickCount;
	int						m_nLastChangedTick;
	bool					m_bChangeHistoryEnabled : 1;
	bool					m_bLocked : 1;
	bool					m_bAllowClientSideAddString : 1;
	bool					m_bUserDataFixedSize : 1;
	bool					m_bIsFilenames : 1;
	int						m_nUserDataSize;
	int						m_nUserDataSizeBits;
	pfnStringChanged		m_changeFunc;
	void					*m_pObject;
	CNetworkStringTable		*m_pMirrorTable;
};

abstract_class IEntityFactory
{
public:
	virtual IServerNetworkable *Create( const char *pClassName ) = 0;
	virtual void Destroy( IServerNetworkable *pNetworkable ) = 0;
	virtual size_t GetEntitySize() = 0;
};

abstract_class IEntityFactoryDictionary
{
public:
	virtual void InstallFactory( IEntityFactory *pFactory, const char *pClassName ) = 0;
	virtual IServerNetworkable *Create( const char *pClassName ) = 0;
	virtual void Destroy( const char *pClassName, IServerNetworkable *pNetworkable ) = 0;
	virtual IEntityFactory *FindFactory( const char *pClassName ) = 0;
	virtual const char *GetCannonicalName( const char *pClassName ) = 0;
};

extern GlobalThunk<CHLTVServer *> hltv;
#endif
