#pragma once

#include "common.h"

u32 f_sha_get(const char* path, u8* sha);
u32 f_copy(const char* dest, const char* orig);
u32 f_delete(const char* path);
u32 f_copy_from_nand(const char* path, u32 size, u32 nand_offset, u32 nand_keyslot, u8* sha);
u32 f_copy_to_nand(const char* path, u32 nand_offset, u32 nand_keyslot);
