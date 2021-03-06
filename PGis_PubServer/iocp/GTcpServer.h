#pragma once

#if(_GSOCKET_FUNC_TCP_SERVER)

extern BOOL bGTcpSvrIsActive;

void GTcpSvr_CloseClients(void);
void GTcpSvr_FreeClients(void);
void GTcpSvr_EndThread(void);
void GTcpSvr_CloseListeners(void);
void GTcpSvr_CloseListenerEvents(void);
void GTcpSvr_FreeListener(void);

void GTcpSvr_Create(void);
void GTcpSvr_Destroy(void);

#endif//#if(_GSOCKET_FUNC_TCP_SERVER)
