#pragma once

extern BOOL bGUdpCltIsActive;

void GUdpClt_CloseClients(void);
void GUdpClt_FreeClients(void);
void GUdpClt_EndThread(void);

void GUdpClt_Create(void);
void GUdpClt_Destroy(void);