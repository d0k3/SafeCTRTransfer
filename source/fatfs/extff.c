#include "extff.h"
#include "nand.h"
#include "sha.h"
#include "ff.h"
#include "ui.h"

u32 f_sha_get(const char* path, u8* sha) {
    FIL file;
    u64 fsize;
    u32 ret = FR_OK;
    
    if (f_open(&file, path, FA_READ | FA_OPEN_EXISTING) != FR_OK)
        return 1;
    fsize = f_size(&file);
    f_lseek(&file, 0);
    ShowProgress(0, 0, path);
    sha_init(SHA256_MODE);
    for (u64 pos = 0; (pos < fsize) && (ret == FR_OK); pos += WORK_BUFFER_SIZE) {
        UINT bytes_read = 0;
        ret = f_read(&file, WORK_BUFFER, WORK_BUFFER_SIZE, &bytes_read);
        if (!ShowProgress(pos + bytes_read, fsize, path)) ret = FR_DENIED;
        sha_update(WORK_BUFFER, bytes_read);
    }
    sha_get(sha);
    f_close(&file);
    ShowProgress(1, 1, path);
    
    return ret;
}

u32 f_copy_worker(char* fdest, char* forig) {
    FILINFO fno;
    u32 ret = f_stat(forig, &fno);
    if (ret != FR_OK) return ret;
    
    // ShowProgress(0, 0, forig);
    if (fno.fattrib & AM_DIR) { // processing folders
        DIR pdir;
        char* oname = forig + strnlen(forig, 256);
        char* dname = fdest + strnlen(fdest, 256);
        
        if ((ret = f_mkdir(fdest)) != FR_OK) return ret;
        if ((ret = f_opendir(&pdir, forig)) != FR_OK) return ret;
        *(oname++) = *(dname++) = '/';
        
        while (((ret = f_readdir(&pdir, &fno)) == FR_OK) && *(fno.fname)) {
            if ((strncmp(fno.fname, ".", 2) == 0) || (strncmp(fno.fname, "..", 3) == 0))
                continue; // filter out virtual entries
            strncpy(oname, fno.fname, 256 - (oname - forig));
            strncpy(dname, fno.fname, 256 - (dname - fdest));
            if ((ret = f_copy_worker(fdest, forig)) != FR_OK) break;
        }
        f_closedir(&pdir);
    } else { // copying files
        FIL ofile;
        FIL dfile;
        u64 fsize = fno.fsize;
        
        if ((ret = f_open(&ofile, forig, FA_READ | FA_OPEN_EXISTING)) != FR_OK) 
            return ret;
        if ((ret = f_open(&dfile, fdest, FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK) {
            f_close(&ofile);
            return ret;
        }
        
        f_lseek(&dfile, 0);
        f_sync(&dfile);
        f_lseek(&ofile, 0);
        f_sync(&ofile);
        
        for (u64 pos = 0; (pos < fsize) && (ret == FR_OK); pos += WORK_BUFFER_SIZE) {
            UINT bytes_read = 0;
            UINT bytes_written = 0;            
            if ((ret = f_read(&ofile, WORK_BUFFER, WORK_BUFFER_SIZE, &bytes_read)) != FR_OK) break;
            /*if (!ShowProgress(pos + (bytes_read / 2), fsize, forig)) {
                ret = FR_DENIED;
                break;
            }*/
            if ((ret = f_write(&dfile, WORK_BUFFER, bytes_read, &bytes_written)) != FR_OK) break;
            if (bytes_read != bytes_written) ret = FR_NO_FILE;
        }
        // ShowProgress(1, 1, forig);
        
        f_close(&ofile);
        f_close(&dfile);
        if (ret != FR_OK) f_unlink(fdest);
    }
    
    return ret;
}

u32 f_copy(const char* dest, const char* orig) {
    char dpath[256] = { 0 }; // 256 is the maximum length of a full path
    char opath[256] = { 0 };
    strncpy(dpath, dest, 255);
    strncpy(opath, orig, 255);
    return f_copy_worker(dpath, opath);
}

u32 f_delete_worker(char* fpath) {
    FILINFO fno;
    u32 ret = FR_OK;
    
    // this code handles directory content deletion
    ret = f_stat(fpath, &fno);
    if (ret != FR_OK) return ret; // fpath does not exist
    if (fno.fattrib & AM_DIR) { // process folder contents
        DIR pdir;
        char* fname = fpath + strnlen(fpath, 255);
        
        ret = f_opendir(&pdir, fpath);
        if (ret != FR_OK) return ret;
        *(fname++) = '/';
        
        while (f_readdir(&pdir, &fno) == FR_OK) {
            if ((strncmp(fno.fname, ".", 2) == 0) || (strncmp(fno.fname, "..", 3) == 0))
                continue; // filter out virtual entries
            strncpy(fname, fno.fname, fpath + 255 - fname);
            if (fno.fname[0] == 0) break;
            else f_delete_worker(fpath);
        }
        f_closedir(&pdir);
        *(--fname) = '\0';
    }
    
    return f_unlink(fpath);
}

u32 f_delete(const char* path) {
    char fpath[256] = { 0 }; // 256 is the maximum length of a full path
    strncpy(fpath, path, 255);
    return f_delete_worker(fpath);
}

u32 f_copy_from_nand(const char* path, u32 size, u32 nand_offset, u32 nand_keyslot) {
    FIL file;
    u32 ret = FR_OK;
    
    if ((ret = f_open(&file, path, FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK)
        return ret;
    f_lseek(&file, 0);
    f_sync(&file);
    ShowProgress(0, 0, path);
    for (u64 pos = 0; (pos < size) && (ret == FR_OK); pos += WORK_BUFFER_SIZE) {
        UINT read_bytes = min(WORK_BUFFER_SIZE, (size - pos));
        UINT bytes_written = 0;
        if ((ret = ReadNandBytes(WORK_BUFFER, nand_offset + pos, read_bytes, nand_keyslot)) != 0) break;
        if (!ShowProgress(pos + (read_bytes / 2), size, path)) {
            ret = FR_DENIED;
            break;
        }
        if ((ret = f_write(&file, WORK_BUFFER, read_bytes, &bytes_written)) != FR_OK) break;
        if (read_bytes != bytes_written) ret = FR_NO_FILE;
    }
    ShowProgress(1, 1, path);
    f_close(&file);
    
    return ret;
}

u32 f_copy_to_nand(const char* path, u32 nand_offset, u32 nand_keyslot) {
    FIL file;
    u32 ret = FR_OK;
    
    if ((ret = f_open(&file, path, FA_READ | FA_OPEN_EXISTING)) != FR_OK)
        return ret;
    f_lseek(&file, 0);
    f_sync(&file);
    u64 fsize = f_size(&file);
    ShowProgress(0, 0, path);
    for (u64 pos = 0; (pos < fsize) && (ret == FR_OK); pos += WORK_BUFFER_SIZE) {
        UINT read_bytes = min(WORK_BUFFER_SIZE, (fsize - pos));
        UINT bytes_read = 0;
        if ((ret = f_read(&file, WORK_BUFFER, read_bytes, &bytes_read)) != FR_OK) break;
        if (read_bytes != bytes_read) ret = FR_NO_FILE;
        if (!ShowProgress(pos + (read_bytes / 2), fsize, path)) {
            ShowPrompt(false, "Cancel is not allowed here.");
            ShowProgress(pos + (read_bytes / 2), fsize, path);
        }
        if ((ret = WriteNandBytes(WORK_BUFFER, nand_offset + pos, bytes_read, nand_keyslot)) != 0) break;
    }
    ShowProgress(1, 1, path);
    f_close(&file);
    
    return ret;
}
