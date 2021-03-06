#include "stdafx.h"
#include "GLog.h"
#include "GMemory.h"
#include "GMessageListData.h"
#include "GSocket.h"

/*********************************************************************************
                   常量定义
*********************************************************************************/
#define	COUNT_GHND_MESSAGELIST_DATA_DEF					1100

/*********************************************************************************
                   默认函数定义
*********************************************************************************/
void GHndMesListDat_OnCreateDef(DWORD dwMessageContext)
{
}

void GHndMesListDat_OnDestroyDef(DWORD dwMessageContext)
{
}

/*********************************************************************************
                   变量定义
*********************************************************************************/
PFN_ON_GHND_EVENT			pfnGHndMesListDatOnCreate = GHndMesListDat_OnCreateDef;
PFN_ON_GHND_EVENT			pfnGHndMesListDatOnDestroy = GHndMesListDat_OnDestroyDef;

DWORD						dwGHndMesListDataMemBytes = 0;
DWORD						dwGHndMesListDataTotal = COUNT_GHND_MESSAGELIST_DATA_DEF;
PGHND_MESSAGELIST_DATA		pGHndMesListDataPoolAddr = NULL;
DWORD						dwGHndMesListDataAlloc = 0;
DWORD						dwGHndMesListDataFree = 0;
PGHND_MESSAGELIST_DATA		pGHndMesListDataPoolHead = NULL;
PGHND_MESSAGELIST_DATA		pGHndMesListDataPoolTail = NULL;
CRITICAL_SECTION			GHndMesListDataPoolHeadCS;
CRITICAL_SECTION			GHndMesListDataPoolTailCS;
BOOL						bGHndMesListDataIsActive = FALSE;

/*********************************************************************************
                   Handle参数获取和设置
*********************************************************************************/
/***
 *名称:GHndDat_GetMemBytes
 *说明:获取GHndData占内存数
 *输入:
 *输出:
 *条件:
 *******************************************************************************/
DWORD GHndMesListDat_GetMemBytes(void)
{
	return(dwGHndMesListDataMemBytes);
}

DWORD GHndMesListDat_GetTotal(void)
{
	return(dwGHndMesListDataTotal);
}

DWORD GHndMesListDat_GetSize(void)
{
	return(sizeof(GHND_MESSAGELIST_DATA));
}

void GHndMesListDat_SetTotal(DWORD dwTotal)
{
	if(bGHndMesListDataIsActive || (!dwTotal))
		return;
	dwGHndMesListDataTotal = dwTotal;
}

DWORD GHndMesListDat_GetUsed(void)
{
	return(dwGHndMesListDataAlloc - dwGHndMesListDataFree);
}

// GHND_TYPE GHndMesListDat_GetType(DWORD dwMessageContext)
// {
// 	return(((GHND_MESSAGELIST_DATA)dwMessageContext)->htType);
// }
// 
// GHND_STATE GHndMesListDat_GetState(DWORD dwMessageContext)
// {
// 	return(((GHND_MESSAGELIST_DATA)dwMessageContext)->hsState);
// }

DWORD GHndMesListDat_GetTickCountAcitve(DWORD dwMessageContext)
{
	return(((PGHND_MESSAGELIST_DATA)dwMessageContext)->dwTickCountAcitve);
}

DWORD GHndMesListDat_GetOwner(DWORD dwMessageContext)
{
	return((DWORD)((PGHND_MESSAGELIST_DATA)dwMessageContext)->pOwner);
}

void* GHndMesListDat_GetData(DWORD dwMessageContext)
{
	return(((PGHND_MESSAGELIST_DATA)dwMessageContext)->szData);
}

void GHndMesListDat_SetData(DWORD dwMessageContext, const char* pData)
{
	strncpy(((PGHND_MESSAGELIST_DATA)dwMessageContext)->szData,pData,strlen(pData));
}

void GHndMesListDat_SetProcOnHndCreate(PFN_ON_GHND_EVENT pfnOnProc)
{
	if(pfnOnProc)
		pfnGHndMesListDatOnCreate = pfnOnProc;
}

void GHndMesListDat_SetProcOnHndDestroy(PFN_ON_GHND_EVENT pfnOnProc)
{
	if(pfnOnProc)
		pfnGHndMesListDatOnDestroy = pfnOnProc;
}

/*********************************************************************************
                   句柄池管理
*********************************************************************************/
PGHND_MESSAGELIST_DATA GHndMesListDat_Alloc(void)
{
	EnterCriticalSection(&GHndMesListDataPoolHeadCS);
	if(pGHndMesListDataPoolHead->pNext)
	{
		PGHND_MESSAGELIST_DATA Result;

		Result = pGHndMesListDataPoolHead;
		pGHndMesListDataPoolHead = pGHndMesListDataPoolHead->pNext;
		dwGHndMesListDataAlloc++;
		LeaveCriticalSection(&GHndMesListDataPoolHeadCS);
		return(Result);
	}else
	{
		LeaveCriticalSection(&GHndMesListDataPoolHeadCS);
		GLog_Write("GHndMesListDat_Alloc：分配句柄上下文失败");
		return(NULL);
	}
}

void GHndMesListDat_Free(PGHND_MESSAGELIST_DATA pHndData)
{
	EnterCriticalSection(&GHndMesListDataPoolTailCS);
	pHndData->pNext = NULL;
	pGHndMesListDataPoolTail->pNext = pHndData;
	pGHndMesListDataPoolTail = pHndData;
	dwGHndMesListDataFree++;
	LeaveCriticalSection(&GHndMesListDataPoolTailCS);
}

void GHndMesListDat_Create(PFN_ON_GHND_MESSAGELIST_CREATE pfnOnCreate)
{
	if(bGHndMesListDataIsActive)
		return;	

	dwGHndMesListDataMemBytes = dwGHndMesListDataTotal * sizeof(GHND_MESSAGELIST_DATA);
	pGHndMesListDataPoolAddr = (PGHND_MESSAGELIST_DATA)GMem_Alloc(dwGHndMesListDataMemBytes);
	if(!pGHndMesListDataPoolAddr)
	{
		dwGHndMesListDataMemBytes = 0;
		GLog_Write("GHndMesListDat_Create：从GMem分配句柄上下文所需内存失败");
		return;
	}

	DWORD i;

	dwGHndMesListDataAlloc = 0;
	dwGHndMesListDataFree = 0;
	pGHndMesListDataPoolHead = pGHndMesListDataPoolAddr;
	pGHndMesListDataPoolTail = pGHndMesListDataPoolAddr;

	for(i = 0; i < dwGHndMesListDataTotal - 1; i++)
	{
		if(pfnOnCreate)
			pfnOnCreate(pGHndMesListDataPoolTail);
		pfnGHndMesListDatOnCreate((DWORD)pGHndMesListDataPoolTail);
		pGHndMesListDataPoolTail->pNext = pGHndMesListDataPoolTail + 1;
		pGHndMesListDataPoolTail = pGHndMesListDataPoolTail->pNext;
	}
	if(pfnOnCreate)
		pfnOnCreate(pGHndMesListDataPoolTail);
	pfnGHndMesListDatOnCreate((DWORD)pGHndMesListDataPoolTail);		
	pGHndMesListDataPoolTail->pNext = NULL;
	InitializeCriticalSection(&GHndMesListDataPoolHeadCS);
	InitializeCriticalSection(&GHndMesListDataPoolTailCS);
	
	bGHndMesListDataIsActive = TRUE;
}

void GHndMesListDat_Destroy(PFN_ON_GHND_MESSAGELIST_DESTROY pfnOnDestroy)
{
	bGHndMesListDataIsActive = FALSE;
	if(!pGHndMesListDataPoolAddr)
		return;

	PGHND_MESSAGELIST_DATA pHndData;

	pHndData = pGHndMesListDataPoolHead;
	while(pHndData)
	{
		pfnGHndMesListDatOnDestroy((DWORD)pHndData);
		if(pfnOnDestroy)
			pfnOnDestroy(pHndData);
		pHndData = pHndData->pNext;
	}

	GMem_Free(pGHndMesListDataPoolAddr);
	pGHndMesListDataPoolAddr = NULL;
	pGHndMesListDataPoolHead = NULL;
	pGHndMesListDataPoolTail = NULL;
	dwGHndMesListDataAlloc = 0;
	dwGHndMesListDataFree = 0;
	DeleteCriticalSection(&GHndMesListDataPoolHeadCS);
	DeleteCriticalSection(&GHndMesListDataPoolTailCS);
	dwGHndMesListDataMemBytes = 0;
}
