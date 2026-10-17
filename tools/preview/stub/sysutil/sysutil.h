#ifndef PV_SYSUTIL_H
#define PV_SYSUTIL_H
#include "../ppu-types.h"
#define SYSUTIL_EVENT_SLOT0 0
#define SYSUTIL_EXIT_GAME 0x0101
typedef void (*sysutilCallback)(u64 status, u64 param, void *usrdata);
s32 sysUtilRegisterCallback(s32 slot, sysutilCallback cb, void *usrdata);
s32 sysUtilUnregisterCallback(s32 slot);
s32 sysUtilCheckCallback(void);
#define SYSUTIL_SYSTEMPARAM_ID_LANG 0x0111
#define SYSUTIL_SYSTEMPARAM_ID_ENTER_BUTTON_ASSIGN 0x0112
#define SYSUTIL_SYSTEMPARAM_ID_NICKNAME 0x0113
#define SYSUTIL_SYSTEMPARAM_ID_DATE_FORMAT 0x0114
#define SYSUTIL_SYSTEMPARAM_ID_TIME_FORMAT 0x0115
#define SYSUTIL_SYSTEMPARAM_ID_TIMEZONE 0x0116
#define SYSUTIL_SYSTEMPARAM_ID_SUMMERTIME 0x0117
s32 sysUtilGetSystemParamInt(s32 id, s32 *value);
#endif
