#include "firmget.h"
#include "ncch.h"
#include "exefs.h"
#include "ff.h"
#include "sha.h"

#define FIRM_MAGIC  'F', 'I', 'R', 'M'

// see: https://www.3dbrew.org/wiki/FIRM#Firmware_Section_Headers
typedef struct {
    u32 offset;
    u32 address;
    u32 size;
    u32 type;
    u8  hash[0x20];
} __attribute__((packed)) FirmSectionHeader;

// see: https://www.3dbrew.org/wiki/FIRM#FIRM_Header
typedef struct {
    u8  magic[4];
    u8  dec_magic[4];
    u32 entry_arm11;
    u32 entry_arm9;
    u8  reserved1[0x30];
    FirmSectionHeader sections[4];
    u8  signature[0x100];
} __attribute__((packed, aligned(16))) FirmHeader;


u32 VerifyFirm(void* data, u32 data_size) {
    u8 magic[] = { FIRM_MAGIC };
    FirmHeader* header = (FirmHeader*) data;
    if (memcmp(header->magic, magic, sizeof(magic)) != 0)
        return 1;
    
    u32 firm_size = sizeof(FirmHeader);
    for (u32 i = 0; i < 4; i++) {
        FirmSectionHeader* section = header->sections + i;
        if (!section->size) continue;
        if (section->offset < firm_size) return 1;
        firm_size = section->offset + section->size;
    }
    
    if ((firm_size > FIRM_MAX_SIZE) || (firm_size > data_size))
        return 1;
    
    // hash verify all available sections
    for (u32 i = 0; i < 4; i++) {
        FirmSectionHeader* section = header->sections + i;
        if (!section->size) continue;
        if (sha_cmp(section->hash, ((u8*) data) + section->offset, section->size, SHA256_MODE) != 0)
            return 1;
    }
    
    // check arm11 / arm9 entrypoints
    int section_arm11 = -1;
    int section_arm9 = -1;
    for (u32 i = 0; i < 4; i++) {
        FirmSectionHeader* section = header->sections + i;
        if ((header->entry_arm11 >= section->address) &&
            (header->entry_arm11 < section->address + section->size))
            section_arm11 = i;
        if ((header->entry_arm9 >= section->address) &&
            (header->entry_arm9 < section->address + section->size))
            section_arm9 = i;
    }
    
    // sections for arm11 / arm9 entrypoints not found?
    if ((section_arm11 < 0) || (section_arm9 < 0))
        return 1;
    
    return 0;
}

u32 GetNcchHeaders(NcchHeader* ncch, NcchExtHeader* exthdr, ExeFsHeader* exefs, FIL* file) {
    u32 offset_ncch = f_tell(file);
    UINT btr;
    
    if ((f_read(file, ncch, sizeof(NcchHeader), &btr) != FR_OK) ||
        (ValidateNcchHeader(ncch) != 0))
        return 1;
    
    if (exthdr) {
        if (!ncch->size_exthdr) return 1;
        f_lseek(file, offset_ncch + NCCH_EXTHDR_OFFSET);
        if ((f_read(file, exthdr, NCCH_EXTHDR_SIZE, &btr) != FR_OK) ||
            (DecryptNcch((u8*) exthdr, NCCH_EXTHDR_OFFSET, NCCH_EXTHDR_SIZE, ncch, NULL) != 0))
            return 1;
    }
    
    if (exefs) {
        if (!ncch->size_exefs) return 1;
        u32 offset_exefs = offset_ncch + (ncch->offset_exefs * NCCH_MEDIA_UNIT);
        f_lseek(file, offset_exefs);
        if ((f_read(file, exefs, sizeof(ExeFsHeader), &btr) != FR_OK) ||
            (DecryptNcch((u8*) exefs, ncch->offset_exefs * NCCH_MEDIA_UNIT, sizeof(ExeFsHeader), ncch, NULL) != 0) ||
            (ValidateExeFsHeader(exefs, ncch->size_exefs * NCCH_MEDIA_UNIT) != 0))
            return 1;
    }
    
    return 0;
}

u32 LoadExeFsFile(void* data, u32* size, const char* path, const char* name, u32 size_max) {
    NcchHeader ncch;
    ExeFsHeader exefs;
    FIL file;
    UINT btr;
    u32 ret = 0;
    
    // open file, get NCCH, ExeFS header
    if (f_open(&file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return 1;
    f_lseek(&file, 0);
    if ((GetNcchHeaders(&ncch, NULL, &exefs, &file) != 0) ||
        (!ncch.size_exefs)) {
        f_close(&file);
        return 1;
    }
    
    // load file from exefs
    ExeFsFileHeader* exefile = NULL;
    for (u32 i = 0; i < 10; i++) {
        u32 size = exefs.files[i].size;
        if (!size || (size > size_max)) continue;
        char* exename = exefs.files[i].name;
        if (strncmp(name, exename, 8) == 0) {
            exefile = exefs.files + i;
            break;
        }
    }
    if (exefile) {
        *size = exefile->size;
        u32 offset_exefile = (ncch.offset_exefs * NCCH_MEDIA_UNIT) + sizeof(ExeFsHeader) + exefile->offset;
        f_lseek(&file, offset_exefile); // offset to file
        if ((f_read(&file, data, *size, &btr) != FR_OK) ||
            (DecryptNcch(data, offset_exefile, *size, &ncch, &exefs) != 0) ||
            (btr != *size)) {
            ret = 1;
        }
    } else ret = 1;
    
    f_close(&file);
    return ret;
}

u32 GetFirm(void* firm, u32* size, const char* drv, u32 tidlow) {
    char path[128] = { 0 };
    
    snprintf(path, 128, "%s/title/%08lx/%08lx/content", drv, (u32) FIRM_TIDHIGH, tidlow);
    char* name = path + strnlen(path, 127);
    
    DIR pdir;
    FILINFO fno;
    if (f_opendir(&pdir, path) == FR_OK) {
        while ((f_readdir(&pdir, &fno) == FR_OK) && (*(fno.fname))) {
            char* ext = strrchr(fno.fname, '.');
            if (!ext || (strncasecmp(ext, ".app", 5) != 0)) continue;
            if (*name) return 1; // two or more app files found
            snprintf(name, 128 - 1 - (name - path), "/%s", fno.fname);
        }
        f_closedir(&pdir);
    }
    if (!(*name)) return 1;
    
    if ((LoadExeFsFile(firm, size, path, ".firm", FIRM_MAX_SIZE) != 0) || (VerifyFirm(firm, *size) != 0))
        return 1;
    
    return 0;
}
