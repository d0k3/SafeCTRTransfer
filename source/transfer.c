#include "transfer.h"
#include "chainload.h"
#include "nandcmac.h"
#include "nand.h"
#include "firmget.h"
#include "qff.h"
#include "extff.h"
#include "ui.h"
#include "hid.h"
#include "sha.h"
#include "qlzcomp.h"
#include "NCSD_header_o3ds_hdr.h"
#include "NCSD_header_o3ds_dev_hdr.h"
#include "logo_qlz.h"

#define COLOR_STATUS(s) ((s == STATUS_GREEN) ? COLOR_BRIGHTGREEN : (s == STATUS_YELLOW) ? COLOR_BRIGHTYELLOW : (s == STATUS_RED) ? COLOR_RED : COLOR_DARKGREY)
#define REGION_STR          "JPN", "USA", "EUR", "AUS", "CHN", "KOR", "TWN"

#define O3DS_CTRNAND_SIZE   0x2F5D0000
#define NAND_MIN_SIZE       ((IS_O3DS) ? 0x3AF00000 : 0x4D800000)
#define MIN_SD_FREE         NAND_MIN_SIZE // ~1GB
#define CTRNAND_OFFSET      0x0B930000
#define FIRM_NAND_OFFSET    0x0B130000
#define FIRM_NAND_SIZE      0x800000
#define FIRM0_NAND_OFFSET   FIRM_NAND_OFFSET
#define FIRM1_NAND_OFFSET   (FIRM_NAND_OFFSET + (FIRM_NAND_SIZE/2))

#define FIRM21_SHA256       0x87, 0xEF, 0x62, 0x94, 0xB9, 0x95, 0x52, 0x0F, 0xE5, 0x4C, 0x75, 0xCB, 0x6B, 0x17, 0xE0, 0x4A, \
                            0x6C, 0x3D, 0xE3, 0x26, 0xDB, 0x08, 0xFC, 0x93, 0x39, 0x45, 0xC0, 0x06, 0x51, 0x45, 0x5A, 0x89

#define STATUS_GREY    -1
#define STATUS_GREEN    0
#define STATUS_YELLOW   1
#define STATUS_RED      2

static int  statusSdCard       = STATUS_GREY;
static int  statusSystem       = STATUS_GREY;
static int  statusInput        = STATUS_GREY;
static int  statusShaCheck     = STATUS_GREY;
static int  statusPrep         = STATUS_GREY;
static int  statusBackup       = STATUS_GREY;
static int  statusTransfer     = STATUS_GREY;
static char msgSdCard[64]      = "not started";
static char msgSystem[64]      = "not started";
static char msgInput[64]       = "not started";
static char msgShaCheck[64]    = "not started";
static char msgPrep[64]        = "not started";
static char msgBackup[64]      = "not started";
static char msgTransfer[64]    = "not started";
    
u32 ShowTransferStatus(void) {
    const u32 pos_xb = 10;
    const u32 pos_x0 = pos_xb + 4;
    const u32 pos_x1 = pos_x0 + (17*FONT_WIDTH_EXT);
    const u32 pos_yb = 10;
    const u32 pos_yu = 230;
    const u32 pos_y0 = pos_yb + 50;
    const u32 stp = 14;
    
    DrawStringF(BOT_SCREEN, pos_xb, pos_yb, COLOR_STD_FONT, COLOR_STD_BG, "SafeCTRTransfer v" VERSION "\n" "----------------------" "\n" "https://github.com/d0k3/SafeCTRTransfer");
    
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (0*stp), COLOR_STD_FONT, COLOR_STD_BG, "MicroSD Card   -");
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (1*stp), COLOR_STD_FONT, COLOR_STD_BG, "System Status  -");
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (2*stp), COLOR_STD_FONT, COLOR_STD_BG, "Transfer File  -");
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (3*stp), COLOR_STD_FONT, COLOR_STD_BG, "SHA256 Check   -");
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (4*stp), COLOR_STD_FONT, COLOR_STD_BG, "Preparations   -");
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (5*stp), COLOR_STD_FONT, COLOR_STD_BG, "Backup Status  -");
    DrawStringF(BOT_SCREEN, pos_x0, pos_y0 + (6*stp), COLOR_STD_FONT, COLOR_STD_BG, "Install Status -");
    
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (0*stp), COLOR_STATUS(statusSdCard)  , COLOR_STD_BG, "%-21.21s", msgSdCard  );
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (1*stp), COLOR_STATUS(statusSystem)  , COLOR_STD_BG, "%-21.21s", msgSystem  );
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (2*stp), COLOR_STATUS(statusInput)   , COLOR_STD_BG, "%-21.21s", msgInput   );
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (3*stp), COLOR_STATUS(statusShaCheck), COLOR_STD_BG, "%-21.21s", msgShaCheck);
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (4*stp), COLOR_STATUS(statusPrep)    , COLOR_STD_BG, "%-21.21s", msgPrep    );
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (5*stp), COLOR_STATUS(statusBackup)  , COLOR_STD_BG, "%-21.21s", msgBackup  );
    DrawStringF(BOT_SCREEN, pos_x1, pos_y0 + (6*stp), COLOR_STATUS(statusTransfer), COLOR_STD_BG, "%-21.21s", msgTransfer);
    
    DrawStringF(BOT_SCREEN, pos_xb, pos_yu - 10, COLOR_STD_FONT, COLOR_STD_BG, "Usage instructions: https://3ds.guide/");
    return 0;
}

u32 SafeCTRTransfer(void) {
    FILINFO fno;
    UINT bt;
    
    // initialization
    // ShowString("Initializing, please wait...");
    char* waitstr = "Please wait...";
    QlzDecompress(TOP_SCREEN, logo_qlz, 0);
    DrawStringF(TOP_SCREEN, SCREEN_WIDTH_TOP - 10 - GetDrawStringWidth(waitstr), SCREEN_HEIGHT - 10 - GetDrawStringHeight(waitstr),
        COLOR_STD_FONT, COLOR_STD_BG, waitstr);
    
    
    // step #0 - init/check SD card
    snprintf(msgSdCard, 64, "checking...");
    statusSdCard = STATUS_YELLOW;
    ShowTransferStatus();
    
    u64 sdFree = 0;
    u64 sdTotal = 0;
    InitNandCrypto();
    if ((fs_init() != FR_OK) ||
        (f_getfreebyte("0:", &sdFree) != FR_OK) ||
        (f_gettotalbyte("0:", &sdTotal) != FR_OK)) {
        snprintf(msgSdCard, 64, "init failed");
        statusSdCard = STATUS_RED;
        return 1;
    }
    
    snprintf(msgSdCard, 64, "%lluMB/%lluMB free", sdFree / (1024 * 1024), sdTotal / (1024 * 1024));
    statusSdCard = (sdFree < MIN_SD_FREE) ? STATUS_RED : STATUS_GREEN;
    ShowTransferStatus();
    if (sdFree < MIN_SD_FREE) return 1;
    // SD card okay!
    
    
    // step #1 - system status
    snprintf(msgSystem, 64, "checking...");
    statusSystem = STATUS_YELLOW;
    ShowTransferStatus();
    
    const char* regstr[] = { REGION_STR };
    u8 secinfo_sys[0x200];
    u8* region = (secinfo_sys + 0x100);
    char* serial = (char*) (secinfo_sys + 0x102);
    #ifndef ALLOW_A9LH
    if (IS_A9LH) {
        snprintf(msgSystem, 64, "A9LH detected");
        statusSystem = STATUS_RED;
        return 1;
    }
    #endif
    if (((f_qread("1:/rw/sys/SecureInfo_A", secinfo_sys, 0x0, 0x111, &bt) != FR_OK) &&
         (f_qread("1:/rw/sys/SecureInfo_B", secinfo_sys, 0x0, 0x111, &bt) != FR_OK)) ||
        (bt != 0x111) || (*region > 6)) {
        snprintf(msgSystem, 64, "secureinfo error");
        statusSystem = STATUS_RED;
        return 1;
    }
    
    snprintf(msgSystem, 64, "%s %s%s", regstr[*region], IS_O3DS ? "O3DS" : "N3DS", IS_DEVKIT ? " (devkit)" : " (retail)");
    statusSystem = STATUS_GREEN;
    ShowTransferStatus();
    // system status okay!
    
    
    // step #2 - check input files
    snprintf(msgInput, 64, "checking...");
    statusInput = STATUS_YELLOW;
    ShowTransferStatus();
    
    char input_path[128] = { 0 };
    DIR pdir;
    if (f_opendir(&pdir, INPUT_PATH) == FR_OK) {
        while ((f_readdir(&pdir, &fno) == FR_OK) && (*(fno.fname))) {
            if ((fno.fsize == O3DS_CTRNAND_SIZE) && !(*input_path)) {
                snprintf(input_path, 128, INPUT_PATH "/%s", fno.fname);
                TruncateString(msgInput, fno.fname, 20, 8);
            } else if (fno.fsize == O3DS_CTRNAND_SIZE) {
                f_closedir(&pdir);
                snprintf(msgInput, 64, "multiple found");
                statusInput = STATUS_RED;
                return 1;
            }
        }
        f_closedir(&pdir);
    }
    if (!*input_path) {
        snprintf(msgInput, 64, "image not found");
        statusInput = STATUS_RED;
        return 1;
    }
    
    u8 secinfo_img[0x200];
    u8* region_img = (secinfo_img + 0x100);
    if (fs_mount(input_path, 1) != FR_OK) {
        snprintf(msgPrep, 64, "image not mountable");
        statusPrep = STATUS_RED;
        return 1;
    }
    if (((f_qread("4:/rw/sys/SecureInfo_A", secinfo_img, 0x0, 0x111, &bt) != FR_OK) &&
         (f_qread("4:/rw/sys/SecureInfo_B", secinfo_img, 0x0, 0x111, &bt) != FR_OK)) ||
        (bt != 0x111)) {
        snprintf(msgSystem, 64, "image secinfo error");
        statusSystem = STATUS_RED;
        return 1;
    }
    if ((*region_img != *region) && (*region < 3)) {
        snprintf(msgInput, 64, "region mismatch");
        statusInput = STATUS_RED;
        return 1;
    }
    const u8 firm_sha[] = { FIRM21_SHA256 };
    u32 firm_size = 0;
    if (GetFirm(FIRM_BUFFER, &firm_size, "4:", O3DS_NATIVE_FIRM_TIDLOW) != 0) {
        snprintf(msgPrep, 64, "firm load failed");
        statusPrep = STATUS_RED;
        return 1;
    }
    if (sha_cmp(firm_sha, FIRM_BUFFER, firm_size, SHA256_MODE) != 0) {
        snprintf(msgPrep, 64, "not a fw 2.1 image");
        statusPrep = STATUS_RED;
        return 1;
    }
    fs_mount(NULL, 0);
    
    char input_sha_path[128] = { 0 };
    snprintf(input_sha_path, 128, "%s.sha", input_path);
    #ifndef SKIP_SHA
    u8 input_sha[0x20];
    if ((f_qread(input_sha_path, input_sha, 0x0, 0x20, &bt) != FR_OK) || (bt != 0x20)) {
        snprintf(msgInput, 128, ".sha not found");
        statusInput = STATUS_RED;
        return 1;
    }
    #endif
    
    statusInput = STATUS_GREEN;
    ShowTransferStatus();
    // input file basic checks okay!
    
    
    // step #X - point of no return (crashes are still not critical for the system)
    #ifndef NO_TRANSFER // we don't need this is in a test run
    if (!ShowUnlockSequence(1, "All basic checks passed!\n \nTo transfer CTRNAND, enter the sequence\nbelow or press B to cancel.")) {
        snprintf(msgShaCheck, 64, "cancelled by user");
        snprintf(msgPrep, 64, "cancelled by user");
        snprintf(msgBackup, 64, "cancelled by user");
        snprintf(msgTransfer, 64, "cancelled by user");
        statusShaCheck = STATUS_YELLOW;
        statusPrep = STATUS_YELLOW;
        statusBackup = STATUS_YELLOW;
        statusTransfer = STATUS_YELLOW;
        return 1;
    }
    #endif
    
    
    // step #3 - SHA check for transferable image
    #ifndef SKIP_SHA
    snprintf(msgShaCheck, 64, "in progress...");
    statusShaCheck = STATUS_YELLOW;
    ShowTransferStatus();
    
    u8 hash[0x20];
    f_sha_get(input_path, hash);
    if (memcmp(hash, input_sha, 0x20) != 0) {
        snprintf(msgShaCheck, 64, ".sha check failed");
        statusShaCheck = STATUS_RED;
        return 1;
    }
    
    snprintf(msgShaCheck, 64, "matches .sha file");
    statusShaCheck = STATUS_GREEN;
    ShowTransferStatus();
    #else
    snprintf(msgShaCheck, 64, "skipped .sha check");
    statusShaCheck = STATUS_YELLOW;
    ShowTransferStatus();
    #endif
    // file SHA check okay!
    
    
    // step #4 - adapt CTRNAND image
    ShowString("Preparing CTRNAND image,\nplease wait...\n \n(this will take a while)");
    statusPrep = STATUS_YELLOW;
    
    if (fs_mount(input_path, 0) != FR_OK) {
        snprintf(msgPrep, 64, "mount failed");
        statusPrep = STATUS_RED;
        return 1;
    }
    
    snprintf(msgPrep, 64, ".db CMAC fix...");
    ShowTransferStatus();
    if ((FixFileCmac("4:/dbs/ticket.db") != 0) ||
        (FixFileCmac("4:/dbs/certs.db") != 0) ||
        (FixFileCmac("4:/dbs/title.db") != 0) ||
        (FixFileCmac("4:/dbs/import.db") != 0)) {
        snprintf(msgPrep, 64, "CMAC failed");
        statusPrep = STATUS_RED;
        return 1;
    }
    FixFileCmac("4:/dbs/tmp_t.db");
    FixFileCmac("4:/dbs/tmp_i.db");
    
    snprintf(msgPrep, 64, "image cleanup...");
    ShowTransferStatus();
    f_delete("4:/private/movable.sed");
    f_delete("4:/rw/sys/LocalFriendCodeSeed_B");
    f_delete("4:/rw/sys/LocalFriendCodeSeed_A");
    if (*region_img == *region) {
        f_delete("4:/rw/sys/SecureInfo_A");
        f_delete("4:/rw/sys/SecureInfo_B");
    }
    f_delete("4:/data");
    f_delete(input_sha_path);
    
    snprintf(msgPrep, 64, "file transfers...");
    ShowTransferStatus();
    if ((f_copy("4:/private/movable.sed", "1:/private/movable.sed") != FR_OK) ||
        ((f_copy("4:/rw/sys/LocalFriendCodeSeed_B", "1:/rw/sys/LocalFriendCodeSeed_B") != FR_OK) &&
         (f_copy("4:/rw/sys/LocalFriendCodeSeed_A", "1:/rw/sys/LocalFriendCodeSeed_A") != FR_OK)) ||
        ((*region_img == *region) &&
         (f_copy("4:/rw/sys/SecureInfo_A", "1:/rw/sys/SecureInfo_A") != FR_OK) &&
         (f_copy("4:/rw/sys/SecureInfo_B", "1:/rw/sys/SecureInfo_B") != FR_OK)) ||
        (f_copy("4:/data", "1:/data") != FR_OK)) {
        snprintf(msgPrep, 64, "file transfer failed");
        statusPrep = STATUS_RED;
        return 1;
    }
    
    fs_mount(NULL, 0);
    snprintf(msgPrep, 64, "image is prepared");
    statusPrep = STATUS_GREEN;
    ShowTransferStatus();
    // image preparations done!
    
    
    // step #5 - NAND backup
    snprintf(msgBackup, 64, "in progress...");
    statusBackup = STATUS_YELLOW;
    ShowTransferStatus();
    
    char backup_path[64];
    char backup_sha_path[64] = { 0 };
    u8 hash_bak[0x20];
    snprintf(backup_path, 64, INPUT_PATH "/%s_nand.bin", serial);
    snprintf(backup_sha_path, 64, "%s.sha", backup_path);
    if (f_copy_from_nand(backup_path, NAND_MIN_SIZE, 0, 0xFF, hash_bak) != FR_OK) {
        snprintf(msgBackup, 64, "nand backup failed");
        statusBackup = STATUS_RED;
        return 1;
    }
    f_qwrite(backup_sha_path, hash_bak, 0, 0x20, NULL);
    
    TruncateString(msgBackup, backup_path, 20, 8);
    statusBackup = STATUS_GREEN;
    ShowTransferStatus();
    // NAND backup done!
    
    
    // step #6 - actual CTRNAND transfer
    #ifndef NO_TRANSFER
    ShowString("Transfering CTRNAND image,\ncross your fingers...");
    snprintf(msgTransfer, 64, "in progress...");
    statusTransfer = STATUS_YELLOW;
    ShowTransferStatus();
    
    u8 nand_header[0x200];
    if ((ReadNandSectors(nand_header, 0, 1, 0xFF) != 0) ||
        (WriteNandSectors(nand_header, 2, 1, 0xFF) != 0) ||
        (NCSD_header_o3ds_hdr_size != 0x200) ||
        (NCSD_header_o3ds_dev_hdr_size != 0x200)) {
        snprintf(msgTransfer, 64, "transfer test failed");
        statusTransfer = STATUS_RED;
        return 1;
    }
    // point of no return - anything that goes wrong now is bad
    u32 ret = 1;
    statusTransfer = STATUS_RED;
    do {
        if (!IS_O3DS) {
            const u8* inject_header = IS_DEVKIT ? NCSD_header_o3ds_dev_hdr : NCSD_header_o3ds_hdr;
            if (WriteNandSectors(inject_header, 0, 1, 0xFF) != 0) {
                snprintf(msgTransfer, 64, "header write failed!");
                break;
            }
        }
        if ((WriteNandBytes(FIRM_BUFFER, FIRM1_NAND_OFFSET, firm_size, 0x06) != 0) ||
            (WriteNandBytes(FIRM_BUFFER, FIRM0_NAND_OFFSET, firm_size, 0x06) != 0)) {
            snprintf(msgTransfer, 64, "firm write failed!");
            break;
        }
        if (f_copy_to_nand(input_path, CTRNAND_OFFSET, 0x04) != FR_OK) {
            snprintf(msgTransfer, 64, "transfer failed!");
            break;
        }
        ret = 0;
    } while (false);
    if (ret == 0) {
        snprintf(msgTransfer, 64, "transfer success");
        statusTransfer = STATUS_GREEN;
        return 0;
    }
    #elif !defined FAIL_TEST
    snprintf(msgTransfer, 64, "test mode, not done");
    statusTransfer = STATUS_YELLOW;
    return 0;
    #endif
    
    // if we end up here: uhoh
    ShowTransferStatus();
    ShowPrompt(false, "SafeCTRTransfer failed!\nThis really should not have happened :/.");
    ShowPrompt(false, "You may launch an external payload\nto try and fix up your system.\n \nThis may be your LAST CHANCE!\nUse it wisely.");
    const char* optionstr[2] = { "Unmount SD card", "Run " INPUT_PATH "/payload.bin" };
    while (true) {
        u32 user_select = ShowSelectPrompt(2, optionstr, "Make your choice.");
        if (user_select == 1) {
            fs_deinit();
            ShowString("SD card unmounted, you can eject now.\n \n<A> to remount SD card");
            while (true) {
                u32 pad_choice = InputWait();
                if (!(pad_choice & BUTTON_A)) continue;
                if (fs_init() == FR_OK) break;
                ShowString("Reinitialising SD card failed!\n \n<A> to retry");
            }
        } else if (user_select == 2) {
            UINT payload_size;
            if ((f_qread(INPUT_PATH "/payload.bin", WORK_BUFFER, 0, WORK_BUFFER_SIZE, &payload_size) != FR_OK) ||
                !payload_size || (payload_size > PAYLOAD_MAX_SIZE))
                continue;
            if (ShowUnlockSequence(3, "payload.bin (%dkB)\nLaunch as arm9 payload?", payload_size / 1024)) {
                Chainload(WORK_BUFFER, payload_size);
                while(1);
            }
        }
    }
    
    return 0;
}
