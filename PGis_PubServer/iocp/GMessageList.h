#pragma once

extern BOOL bGGMessageListIsActive;

void GMessageList_Close(void);
void GMessageList_Free(void);
void GMessageList_EndThread(void);

void GMessageList_Create(void);
void GMessageList_Destroy(void);
