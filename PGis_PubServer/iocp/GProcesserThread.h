#pragma once

extern HANDLE hGProcThrdThreadCompletionPort;
extern BOOL bGProcThrdThreadIsActive;

void GProcThrd_Create(void);
void GProcThrd_Destroy(void);

