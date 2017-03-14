#pragma once

#include "common.h"

#define FIRM_TIDHIGH 0x00040138
#define O3DS_NATIVE_FIRM_TIDLOW 0x00000002
#define N3DS_NATIVE_FIRM_TIDLOW 0x20000002
#define FIRM_MAX_SIZE 0x400000 // 4MB, due to FIRM partition size

u32 GetFirm(void* firm, u32* size, const char* drv, u32 tidlow);
