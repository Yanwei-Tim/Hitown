#include "stdafx.h"
#include <winsock2.h>
#include <mswsock.h>

#include "GLog.h"
#include "GWorkerThread.h"
#include "GPerIoData.h"
#include "GPerHandleData.h"
#include "GThread.h"
#include "GSocketInside.h"
#include "GSocket.h"

#if(_GSOCKET_FUNC_TCP_CLIENT)

#define GTcpClt_ConnectEx(pClient, Addr, pIoData) (pfnGSockConnectEx(pClient->Socket, (sockaddr *)&Addr, sizeof(SOCKADDR_IN), NULL, 0,NULL,(LPOVERLAPPED)pIoData))
#define GTcpClt_DisconnectEx(pClient) (pfnGSockDisconnectEx(pClient->Socket, NULL, TF_REUSE_SOCKET, 0))

/*********************************************************************************
                   变量定义和初始化
*********************************************************************************/
#if(_USE_INTERLOCKED_IN_LIST)
BOOL						bGTcpCltClientLock = FALSE;
#else
CRITICAL_SECTION			GTcpCltClientCS;
#endif
PGHND_DATA					pGTcpCltClientHead = NULL;
DWORD						dwGTcpCltClientCount = NULL;

GTHREAD						GTcpCltServiceThreadData;
BOOL						bGTcpCltIsActive = FALSE;

/*********************************************************************************
                变量设置
*********************************************************************************/
DWORD GTcpClt_GetClientCount(void)
{
	return(dwGTcpCltClientCount);
}

/*********************************************************************************
                客户端操作
*********************************************************************************/
#if(_USE_INTERLOCKED_IN_LIST)
void GTcpClt_LockClientList(void)
{
	for(;;)
	{
		if(FALSE == GSock_InterlockedSet(bGTcpCltClientLock, TRUE, FALSE))
			return;
	}
}

void GTcpClt_UnlockClientList(void)
{
	GSock_InterlockedSet(bGTcpCltClientLock, FALSE, TRUE);
}
#endif

void GTcpClt_InsertClientList(PGHND_DATA pClient)
{
	#if(_USE_INTERLOCKED_IN_LIST)
	GTcpClt_LockClientList();
	#else
	EnterCriticalSection(&GTcpCltClientCS);
	#endif

	pClient->pPrior = NULL;
	pClient->pNext = pGTcpCltClientHead;
	if(pGTcpCltClientHead)
		pGTcpCltClientHead->pPrior = pClient;
	pGTcpCltClientHead = pClient;
	dwGTcpCltClientCount++;

	#if(_USE_INTERLOCKED_IN_LIST)
	GTcpClt_UnlockClientList();
	#else
	LeaveCriticalSection(&GTcpCltClientCS);
	#endif
}

void GTcpClt_DeleteClientList(PGHND_DATA pClient)
{
	#if(_USE_INTERLOCKED_IN_LIST)
	GTcpClt_LockClientList();
	#else
	EnterCriticalSection(&GTcpCltClientCS);
	#endif

	if(pClient == pGTcpCltClientHead)
	{
		pGTcpCltClientHead = pGTcpCltClientHead->pNext;
		if(pGTcpCltClientHead)
			pGTcpCltClientHead->pPrior = NULL;
	}else
	{
		pClient->pPrior->pNext = pClient->pNext;
		if(pClient->pNext)
			pClient->pNext->pPrior = pClient->pPrior;
	}
	dwGTcpCltClientCount--;

	#if(_USE_INTERLOCKED_IN_LIST)
	GTcpClt_UnlockClientList();
	#else
	LeaveCriticalSection(&GTcpCltClientCS);
	#endif
}

void GTcpClt_TraversalClient(DWORD dwParam, DWORD dwFromIndex, DWORD dwCount, PFN_ON_GSOCK_TRAVERSAL pfnOnProc)
{
	if(!bGTcpCltIsActive)
	{
		return;
	}

	#if(_USE_INTERLOCKED_IN_LIST)
	GTcpClt_LockClientList();
	#else
	EnterCriticalSection(&GTcpCltClientCS);
	#endif

	PGHND_DATA pClient;
	DWORD dwIndex;

	dwIndex = 0;
	dwCount = dwFromIndex + dwCount;
	pClient = pGTcpCltClientHead;
	while(pClient)
	{		
		if(dwIndex >= dwFromIndex)
		{
			if((dwIndex >= dwCount) || (!pfnOnProc(dwParam, dwIndex, (DWORD)pClient)))
				break;
		}
		dwIndex++;
		pClient = pClient->pNext;
	}

	#if(_USE_INTERLOCKED_IN_LIST)
	GTcpClt_UnlockClientList();
	#else
	LeaveCriticalSection(&GTcpCltClientCS);
	#endif
}

void GTcpClt_OnReadWriteError(PGHND_DATA pClient, PGIO_DATA pIoData)
{
	if(GIO_WRITE_COMPLETED == pIoData->OperType)
		pfnOnGSockSendErrorClt((DWORD)pClient, pIoData->cData, pIoData->WSABuf.len);
	GIoDat_Free(pIoData);

	if(GHND_STATE_CONNECTED != GSock_InterlockedSet(pClient->hsState, GHND_STATE_DISCONNECT, GHND_STATE_CONNECTED))
		return;

	GTcpClt_DisconnectEx(pClient);
	pClient->dwTickCountAcitve = GetTickCount();
	pfnOnGSockDisconnectClt((DWORD)pClient);
}

void GTcpClt_OnReadWrite(DWORD dwBytes, PGHND_DATA pClient, PGIO_DATA pIoData)
{
	if(!dwBytes)
	{
		if((GIO_READ_COMPLETED == pIoData->OperType) && bGSockIsZeroByteRecv)
		{
			GIoDat_ResetIoDataOnRead(pIoData);
			pIoData->OperType = GIO_ZERO_READ_COMPLETED;
			pIoData->WSABuf.len = dwGBufSize;
			dwBytes = 0;
			DWORD dwFlag = 0;
			if((SOCKET_ERROR == WSARecv(pClient->Socket, &(pIoData->WSABuf), 1, &dwBytes, &dwFlag, LPWSAOVERLAPPED(pIoData), NULL)) ||
				(ERROR_IO_PENDING == WSAGetLastError()))
			{
				return;
			}
		}
		GTcpClt_OnReadWriteError(pClient, pIoData);
		return;
	}

	pClient->dwTickCountAcitve = GetTickCount();
	if(GIO_WRITE_COMPLETED == pIoData->OperType)
	{
		pfnOnGSockSendedClt((DWORD)pClient, pIoData->cData, dwBytes);
		GIoDat_Free(pIoData);
	}else
	{
		#if(_USE_GPROTOCOL)
		if(GCommProt_ProcessReceive(pClient, pIoData->cData, dwBytes, pfnOnGSockReceiveClt))
		{
			pIoData = GIoDat_Alloc();
			if(!pIoData)
			{
				GLog_Write("GTcpClt_OnReadWrite：IoData分配失败，无法再投递接收");
				return;
			}
		}
		#else
		pfnOnGSockReceiveClt((DWORD)pClient, pIoData->cData, dwBytes);
		#endif

		GIoDat_ResetIoDataOnRead(pIoData);
		pIoData->OperType = GIO_READ_COMPLETED;
		pIoData->WSABuf.len = dwGSockRecvBytes;
		dwBytes = 0;
		DWORD dwFlag = 0;
		if((SOCKET_ERROR == WSARecv(pClient->Socket, &(pIoData->WSABuf), 1, &dwBytes, &dwFlag, LPWSAOVERLAPPED(pIoData), NULL)) &&	
			(ERROR_IO_PENDING != WSAGetLastError()))
		{
			GTcpClt_OnReadWriteError(pClient, pIoData);
		}
	}
}

void GTcpClt_OnConnectError(PGHND_DATA pClient, PGIO_DATA pIoData)
{
	GIoDat_Free(pIoData);
	GTcpClt_DisconnectEx(pClient);
	pClient->dwTickCountAcitve = GetTickCount();
	pClient->hsState = GHND_STATE_DISCONNECT;
	pfnOnGSockConnectError(DWORD(pClient));
}

void GTcpClt_OnConnected(DWORD dwBytes, PGHND_DATA pClient, PGIO_DATA pIoData)
{
	setsockopt(pClient->Socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
	pClient->hsState = GHND_STATE_CONNECTED;
	pClient->dwTickCountAcitve = GetTickCount();
	pClient->pfnOnIocpOper = &GTcpClt_OnReadWrite;
	pClient->pfnOnIocpError = &GTcpClt_OnReadWriteError;

	pfnOnGSockConnectClt((DWORD)pClient, pIoData->cData, dwBytes);

	ZeroMemory(pIoData, sizeof(WSAOVERLAPPED));
	DWORD dwCount = dwGSockNumberPostRecv;
	for(;;)
	{
		GIoDat_ResetIoDataOnRead(pIoData);
		pIoData->WSABuf.len = dwGSockRecvBytes;
		dwBytes = 0;
		DWORD dwFlag = 0;
		if((SOCKET_ERROR == WSARecv(pClient->Socket, &(pIoData->WSABuf), 1, &dwBytes, &dwFlag, LPWSAOVERLAPPED(pIoData), NULL)) &&
			(ERROR_IO_PENDING != WSAGetLastError()))
		{
			GTcpClt_OnReadWriteError(pClient, pIoData);
			return;
		}		
		dwCount--;
		if(!dwCount)
			return;
		pIoData = GIoDat_Alloc();
		if(!pIoData)
		{
			GLog_Write("GTcpClt_OnConnected：连接后，申请IoData失败");
			return;
		}
		pIoData->OperType = GIO_READ_COMPLETED;
		pIoData->pOwner = pClient;
	}
}

BOOL GTcpClt_Connect(PGHND_DATA pClient)
{
	PGIO_DATA pIoData;

	pIoData = GIoDat_Alloc();
	if(!pIoData)
	{
		GLog_Write("GTcpClt_Connect：申请IoData失败，无法连接");
		return(FALSE);
	}

	SOCKADDR_IN saRemoteAddr;	
	saRemoteAddr.sin_family = AF_INET;
	saRemoteAddr.sin_addr.S_un.S_addr = pClient->dwAddr;
	saRemoteAddr.sin_port = htons((WORD)pClient->dwPort);

	pClient->dwTickCountAcitve = GetTickCount();
	pClient->pfnOnIocpOper = &GTcpClt_OnConnected;
	pClient->pfnOnIocpError = &GTcpClt_OnConnectError;
	pClient->hsState = GHND_STATE_CONNECTING;

	pIoData->OperType = GIO_READ_COMPLETED;
	pIoData->pOwner = pClient;
	pIoData->WSABuf.len = dwGSockRecvBytes;
	if((!GTcpClt_ConnectEx(pClient, saRemoteAddr, pIoData)) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		pClient->hsState = GHND_STATE_DISCONNECT;
		GIoDat_Free(pIoData);
		GLog_Write("GTcpClt_Connect：ConnectEx请求失败");
		return(FALSE);
	}
	return(TRUE);
}

DWORD GTcpClt_CreateClient(char *pszRemoteIp, DWORD dwRemotePort, char *pszLocalIp, void* pOwner)
{
	PGHND_DATA pClient;

	pClient = GHndDat_Alloc();
	if(!pClient)
	{
		GLog_Write("GTcpClt_CreateClient：申请HndData失败，无法创建连接");
		return(0);
	}
	#if(!_REUSED_SOCKET)
	GSock_InitTcpHndData(pClient);
	#endif

	SOCKADDR_IN saLocalAddr;

	saLocalAddr.sin_family = AF_INET;
	saLocalAddr.sin_addr.S_un.S_addr = inet_addr(pszLocalIp);
	saLocalAddr.sin_port = htons(0);
	do
	{
		if(SOCKET_ERROR == bind(pClient->Socket, (sockaddr *)&(saLocalAddr), sizeof(SOCKADDR_IN)))
		{
			#if(_REUSED_SOCKET)
			GTcpClt_DisconnectEx(pClient);
			#else
			GSock_UninitTcpHndData(pClient);
			#endif
			GHndDat_Free(pClient);
			GLog_Write("GTcpClt_CreateClient：bind失败，无法创建连接");
			return(0);
		}else
			break;
	}while(1);

	pClient->htType = GHND_TYPE_TCP_CLT_CLIENT;
	pClient->pData = NULL;
	pClient->pOwner = pOwner;
	pClient->dwAddr = inet_addr(pszRemoteIp);
	pClient->dwPort = dwRemotePort;
	GTcpClt_InsertClientList(pClient);
	pfnOnGSockCreateClientClt(DWORD(pClient));
	if(!GTcpClt_Connect(pClient))
	{
		GTcpClt_DeleteClientList(pClient);
		pfnOnGSockDestroyClientClt(DWORD(pClient));
		#if(_REUSED_SOCKET)
		GTcpClt_DisconnectEx(pClient);
		#else
		GSock_UninitTcpHndData(pClient);
		#endif
		GHndDat_Free(pClient);
		GLog_Write("GTcpClt_CreateClient：GTcpClt_Connect失败，无法创建连接");
		return(0);
	}else	
		return((DWORD)pClient);
}

void GTcpClt_DestroyClient(DWORD dwClientContext)
{
	GTcpClt_DeleteClientList((PGHND_DATA)dwClientContext);
	#if(_REUSED_SOCKET)	
	GTcpClt_DisconnectEx(PGHND_DATA(dwClientContext));
	#else
	GSock_UninitTcpHndData((PGHND_DATA)dwClientContext);	
	#endif
	GHndDat_Free((PGHND_DATA)dwClientContext);
}

void GTcpClt_DisconnectClient(DWORD dwClientContext)
{
	PGIO_DATA pIoData = GIoDat_Alloc();
	if(pIoData)
	{
		pIoData->OperType = GIO_CLOSE;
		pIoData->WSABuf.len = 0;
		pIoData->pOwner = (void*)dwClientContext;
		PostQueuedCompletionStatus(hGWkrThrdCompletionPort, 0, dwClientContext, (LPOVERLAPPED)pIoData);
	}else
		GLog_Write("GTcpSvr_DoCloseClient：申请IoData失败，无法关闭连接");
}

void GTcpClt_PostBroadcastBuf(char* pBuf, DWORD dwBytes)
{
	PGHND_DATA pClient;

	pClient = pGTcpCltClientHead;
	while(pClient)
	{
		GCommProt_PostSendBuf((DWORD)pClient, pBuf, dwBytes);
		pClient = pClient->pNext;
	}
}

/*********************************************************************************
                   TcpClient伺服线程
*********************************************************************************/
void GTcpCltServiceThread(PGTHREAD pThread)
{
	PGHND_DATA pClient;
	INT nLong;
	INT nLongBytes = sizeof(INT);
	DWORD dwTickCount;

	GLog_Write("GTcpCltServiceThread：服务线程开始");
	for(;;)
	{
		#if(_RUN_INFO)
		pThread->dwState = GTHREAD_STATE_SLEEP;
		#endif

		Sleep(1000);

		#if(_RUN_INFO)
		pThread->dwState = GTHREAD_STATE_WORKING1;
		pThread->dwRunCount++;
		#endif

		if(pThread->bIsShutdown)
			break;

		#if(_USE_INTERLOCKED_IN_LIST)
		GTcpClt_LockClientList();
		#else
		EnterCriticalSection(&GTcpCltClientCS);
		#endif

		pClient = pGTcpCltClientHead;
		dwTickCount = GetTickCount();

		#if(_RUN_INFO)
		pThread->dwState = GTHREAD_STATE_WORKING1;
		#endif

		while(pClient)
		{
			if(GHND_STATE_DISCONNECT == pClient->hsState)
			{
				if(dwTickCount > pClient->dwTickCountAcitve)
					nLong = dwTickCount - pClient->dwTickCountAcitve;
				else
					nLong = pClient->dwTickCountAcitve - dwTickCount;			
				if((DWORD)nLong > dwGSockTimeAutoConnect)
				{
					#if(_RUN_INFO)
					pThread->dwState = GTHREAD_STATE_WORKING2;
					#endif

					GTcpClt_Connect(pClient);
				}
			}else
			if(GHND_STATE_CONNECTED == pClient->hsState)
			{
				if(dwTickCount > pClient->dwTickCountAcitve)
					nLong = dwTickCount - pClient->dwTickCountAcitve;
				else
					nLong = pClient->dwTickCountAcitve - dwTickCount;
				if((DWORD)nLong > dwGSockTimeIdleOvertime)
				{
					#if(_RUN_INFO)
					pThread->dwState = GTHREAD_STATE_WORKING3;
					#endif

					GTcpClt_DisconnectClient(DWORD(pClient));
				}else
				if((DWORD)nLong > dwGSockTimeHeartbeat)
				{
					#if(_RUN_INFO)
					pThread->dwState = GTHREAD_STATE_WORKING4;
					#endif

					pfnOnGSockHeartbeat(DWORD(pClient));
				}
			}
			pClient = pClient->pNext;
		}
		#if(_USE_INTERLOCKED_IN_LIST)
		GTcpClt_UnlockClientList();
		#else
		LeaveCriticalSection(&GTcpCltClientCS);
		#endif
	}
	GLog_Write("GTcpCltServiceThread：服务线程结束");
}

void GTcpClt_Create(void)
{
	if(bGTcpCltIsActive)
		return;	

	pGTcpCltClientHead = NULL;
	dwGTcpCltClientCount = 0;
	#if(!_USE_INTERLOCKED_IN_LIST)
	InitializeCriticalSection(&GTcpCltClientCS);
	#endif	

	GThrd_CreateThread(&GTcpCltServiceThreadData, GTHREAD_TYPE_TCP_CLIENT_SERVICE, "TcpClt伺服", &GTcpCltServiceThread);
	if(!GTcpCltServiceThreadData.dwThreadId)
	{
		#if(!_USE_INTERLOCKED_IN_LIST)
		DeleteCriticalSection(&GTcpCltClientCS);
		#endif
		GLog_Write("GTcpClt_Create：创建服务线程“GTcpCltServiceThread”失败");
		return;
	}

	bGTcpCltIsActive = TRUE;
}

void GTcpClt_CloseClients(void)
{
	PGHND_DATA pClient;

	GLog_Write("GTcpClt_CloseClients：正在关闭客户端的连接");

	pClient = pGTcpCltClientHead;
	while(pClient)
	{
		closesocket(pClient->Socket);
		pClient = pClient->pNext;
	}

	GLog_Write("GTcpClt_CloseClients：成功关闭客户端的连接");
}

void GTcpClt_FreeClients(void)
{
	PGHND_DATA pClient, tmp;

	GLog_Write("GTcpClt_FreeClients：正在释放客户端");

	pClient = pGTcpCltClientHead;
	while(pClient)
	{
		tmp = pClient->pNext;
		pfnOnGSockDestroyClientClt(DWORD(pClient));
		GTcpClt_DeleteClientList(pClient);
		GHndDat_Free(pClient);
		pClient = tmp;
	}

	GLog_Write("GTcpClt_FreeClients：成功释放客户端");
}

void GTcpClt_EndThread(void)
{
	if(GTcpCltServiceThreadData.bIsShutdown)
	{
		GTcpCltServiceThreadData.bIsShutdown = TRUE;
		GLog_Write("GTcpClt_EndThread：正在关闭服务线程“GTcpCltServiceThread”");
		GThrd_DestroyThread(&GTcpCltServiceThreadData);
		GLog_Write("GTcpClt_EndThread：成功关闭服务线程“GTcpCltServiceThread”");
	}
}

void GTcpClt_Destroy(void)
{
	if(!bGTcpCltIsActive)
		return;
	bGTcpCltIsActive = FALSE;

	pGTcpCltClientHead = NULL;
	dwGTcpCltClientCount = 0;
	#if(!_USE_INTERLOCKED_IN_LIST)
	DeleteCriticalSection(&GTcpCltClientCS);
	#endif
}

#endif//#if(_GSOCKET_FUNC_TCP_CLIENT)