#include "stub/server.h"


GlobalThunk<CHLTVServer *> hltv("hltv");

MemberFuncThunk<CHLTVServer *, void, IClient *> CHLTVServer::ft_StartMaster("CHLTVServer::StartMaster");
MemberFuncThunk<CHLTVServer *, int> CHLTVServer::ft_CountClientFrames("CClientFrameManager::CountClientFrames");
MemberFuncThunk<CHLTVServer *, void> CHLTVServer::ft_RemoveOldestFrame("CClientFrameManager::RemoveOldestFrame");
MemberFuncThunk<CBaseServer *, IClient *, const char *> CBaseServer::ft_CreateFakeClient("CBaseServer::CreateFakeClient");

MemberFuncThunk<CGameClient *, bool>              CGameClient::ft_ShouldSendMessages("CGameClient::ShouldSendMessages");