#include "stdafx.h"
#include "GLog.h"
#include "GMemory.h"
#include "GPerIoData.h"
#include "GSocket.h"

/*********************************************************************************
                   常量定义
*********************************************************************************/
#define	SIZE_IO_DATA_DEF					/*4096*/  1024
#define	COUNT_IO_DATA_DEF					/*10000*/ 40000

/*********************************************************************************
                   变量定义
*********************************************************************************/
DWORD						dwGIoDataPackHeadSize = 0;
DWORD						dwGIoDataPackTailSize = 0;
DWORD						dwGBufOffset = sizeof(GIO_DATA_INFO);
DWORD						dwGBufSize = SIZE_IO_DATA_DEF - sizeof(GIO_DATA_INFO);
DWORD						dwGIoDataSize = SIZE_IO_DATA_DEF;
DWORD						dwGIoDataMemBytes = 0;
DWORD						dwGIoDataTotal = COUNT_IO_DATA_DEF;

PGIO_DATA					pGIoDataPoolAddr = NULL;
#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
SLIST_HEADER				GIoDataPoolHead;
#else
DWORD						dwGIoDataFree = 0;
DWORD						dwGIoDataAlloc = 0;
PGIO_DATA					pGIoDataPoolHead = NULL;
PGIO_DATA					pGIoDataPoolTail = NULL;
CRITICAL_SECTION			GIoDataPoolHeadCS;
CRITICAL_SECTION			GIoDataPoolTailCS;
#endif
BOOL						bGIoDataIsActive = FALSE;
/*********************************************************************************
                   变量获取和设置
*********************************************************************************/
DWORD GIoDat_GetPackHeadSize(void)
{
	return(dwGIoDataPackHeadSize);
}

void GIoDat_SetPackHeadSize(DWORD dwSize)
{
	if(bGIoDataIsActive || (dwGIoDataSize <= sizeof(GIO_DATA_INFO) + dwSize + dwGIoDataPackTailSize))
		return;
	dwGIoDataPackHeadSize = dwSize;
	dwGBufOffset = sizeof(GIO_DATA_INFO) + dwGIoDataPackHeadSize;
	dwGBufSize = dwGIoDataSize - dwGBufOffset - dwGIoDataPackTailSize;
}

DWORD GIoDat_GetPackTailSize(void)
{
	return(dwGIoDataPackTailSize);
}

void GIoDat_SetPackTailSize(DWORD dwSize)
{
	if(bGIoDataIsActive || (dwGIoDataSize <= sizeof(GIO_DATA_INFO) + dwSize + dwGIoDataPackHeadSize))
		return;
	dwGIoDataPackTailSize = dwSize;
	dwGBufSize = dwGIoDataSize - dwGBufOffset - dwGIoDataPackTailSize;
}

DWORD GIoDat_GetSize(void)
{
	return(dwGIoDataSize);
}

DWORD GIoDat_GetGBufSize(void)
{
	return(dwGBufSize);
}

void GIoDat_SetGBufSize(DWORD dwSize)
{
	if(bGIoDataIsActive || (!dwSize))
		return;	
	dwGBufSize = dwSize;
	dwGIoDataSize = dwGBufSize + sizeof(GIO_DATA_INFO);
}

DWORD GIoDat_GetMemBytes(void)
{
	return(dwGIoDataMemBytes);
}

DWORD GIoDat_GetTotal(void)
{
	return(dwGIoDataTotal);
}

void GIoDat_SetTotal(DWORD dwTotal)
{
	if(bGIoDataIsActive || (!dwTotal))
		return;
	dwGIoDataTotal = dwTotal;
}

DWORD GIoDat_GetUsed(void)
{
	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	return(dwGIoDataTotal - QueryDepthSList(&GIoDataPoolHead));
	#else
	return(dwGIoDataAlloc - dwGIoDataFree);
	#endif
}

/*********************************************************************************
                   Io内存池管理
*********************************************************************************/
PGIO_BUF GIoDat_AllocGBuf(void)
{
	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	PGIO_BUF Result;
	Result = (PGIO_BUF)InterlockedPopEntrySList(&GIoDataPoolHead);	
	if(Result)
	{		
		GIoDat_ResetIoDataOnAlloc(Result);
		return((char *)Result + dwGBufOffset);
	}else
	{
		GLog_Write("GIoDat_AllocGBuf：分配GBuf失败");
		return(NULL);
	}
	#else
	EnterCriticalSection(&GIoDataPoolHeadCS);
	if(pGIoDataPoolHead->pNext)
	{
		PGIO_BUF Result;

		Result = (PGIO_BUF)pGIoDataPoolHead;
		pGIoDataPoolHead = pGIoDataPoolHead->pNext;
		dwGIoDataAlloc++;

		LeaveCriticalSection(&GIoDataPoolHeadCS);
		GIoDat_ResetIoDataOnAlloc(Result);
		return((char *)Result + dwGBufOffset);
	}else
	{
		LeaveCriticalSection(&GIoDataPoolHeadCS);
		GLog_Write("GIoDat_AllocGBuf：分配GBuf失败");
		return(NULL);
	}
	#endif
}

void GIoDat_FreeGBuf(PGIO_BUF pGIoBuf)
{
	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	InterlockedPushEntrySList(&GIoDataPoolHead, (PSLIST_ENTRY)((char *)pGIoBuf - dwGBufOffset));
	#else
	EnterCriticalSection(&GIoDataPoolTailCS);
	pGIoBuf = (char *)pGIoBuf - dwGBufOffset;
	((PGIO_DATA)pGIoBuf)->pNext = NULL;
	pGIoDataPoolTail->pNext = (PGIO_DATA)pGIoBuf;
	pGIoDataPoolTail = (PGIO_DATA)pGIoBuf;
	dwGIoDataFree++;
	LeaveCriticalSection(&GIoDataPoolTailCS);
	#endif
}

PGIO_DATA GIoDat_Alloc(void)
{
	PGIO_DATA Result;
	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	Result = (PGIO_DATA)InterlockedPopEntrySList(&GIoDataPoolHead);	
	if(Result)
	{
		GIoDat_ResetIoDataOnAlloc(Result);
		return(Result);
	}else
	{
		GLog_Write("GIoDat_Alloc：分配IoData失败");
		return(NULL);
	}
	#else
	EnterCriticalSection(&GIoDataPoolHeadCS);
	if(pGIoDataPoolHead->pNext)
	{
		Result = pGIoDataPoolHead;
		pGIoDataPoolHead = pGIoDataPoolHead->pNext;
		dwGIoDataAlloc++;

		LeaveCriticalSection(&GIoDataPoolHeadCS);
		GIoDat_ResetIoDataOnAlloc(Result);
		return(Result);
	}else
	{
		LeaveCriticalSection(&GIoDataPoolHeadCS);
		GLog_Write("GIoDat_Alloc：分配IoData失败");
		return(NULL);
	}
	#endif
}

void GIoDat_Free(PGIO_DATA pIoData)
{
	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	InterlockedPushEntrySList(&GIoDataPoolHead, (PSLIST_ENTRY)pIoData);
	#else
	EnterCriticalSection(&GIoDataPoolTailCS);
	pIoData->pNext = NULL;
	pGIoDataPoolTail->pNext = pIoData;
	pGIoDataPoolTail = pIoData;
	dwGIoDataFree++;
	LeaveCriticalSection(&GIoDataPoolTailCS);
	#endif
}

void GIoDat_Init(PGIO_DATA pIoData)
{
	ZeroMemory(pIoData, sizeof(WSAOVERLAPPED));
	pIoData->WSABuf.buf = pIoData->cData;
}

void GIoDat_Create(void)
{
	if(bGIoDataIsActive)
		return;
	
	dwGIoDataMemBytes = dwGIoDataSize * dwGIoDataTotal;
	pGIoDataPoolAddr = (PGIO_DATA)GMem_Alloc(dwGIoDataMemBytes);
	if(!pGIoDataPoolAddr)
	{
		dwGIoDataMemBytes = 0;
		GLog_Write("GIoDat_Create：从GMem分配IO上下文所需内存失败");
		return;
	}

	DWORD i;

	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	PGIO_DATA pIoData;

	InitializeSListHead(&GIoDataPoolHead);
	pIoData = pGIoDataPoolAddr;
	for(i = 0; i < dwGIoDataTotal; i++)
	{
		GIoDat_Init(pIoData);
		InterlockedPushEntrySList(&GIoDataPoolHead, (PSLIST_ENTRY)pIoData);
		pIoData = PGIO_DATA((char *)pIoData + dwGIoDataSize);
	}
	#else
	dwGIoDataAlloc = 0;
	dwGIoDataFree = 0;
	pGIoDataPoolHead = pGIoDataPoolAddr;
	pGIoDataPoolTail = pGIoDataPoolAddr;
	for(i = 0; i < dwGIoDataTotal - 1; i++)
	{
		GIoDat_Init(pGIoDataPoolTail);
		pGIoDataPoolTail->pNext = PGIO_DATA((char *)pGIoDataPoolTail + dwGIoDataSize);
		pGIoDataPoolTail = pGIoDataPoolTail->pNext;
	}
	GIoDat_Init(pGIoDataPoolTail);
	pGIoDataPoolTail->pNext = NULL;
	InitializeCriticalSection(&GIoDataPoolHeadCS);
	InitializeCriticalSection(&GIoDataPoolTailCS);
	#endif

	bGIoDataIsActive = TRUE;
}

void GIoDat_Destroy(void)
{
	bGIoDataIsActive = FALSE;
	if(!pGIoDataPoolAddr)
		return;

	GMem_Free(pGIoDataPoolAddr);
	pGIoDataPoolAddr = NULL;
	dwGIoDataMemBytes = 0;
	#if(_USE_SINGLY_LINKED_LIST_IN_IODATA_POOL)
	InterlockedFlushSList(&GIoDataPoolHead);
	#else
	dwGIoDataAlloc = 0;
	dwGIoDataFree = 0;
	pGIoDataPoolHead = NULL;
	pGIoDataPoolTail = NULL;
	DeleteCriticalSection(&GIoDataPoolHeadCS);
	DeleteCriticalSection(&GIoDataPoolTailCS);
	#endif
}