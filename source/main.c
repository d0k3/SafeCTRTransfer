#include "transfer.h"
#include "ui.h"
#include "hid.h"
#include "qff.h"
#include "i2c.h"


void Reboot()
{
    i2cWriteRegister(I2C_DEV_MCU, 0x20, 1 << 2);
    while(true);
}


int main()
{
    u32 ret = SafeCtrTransfer();
    ShowTransferStatus(); // update transfer status one last time
    fs_deinit(); // deinitialize SD card
    if (ret) ShowPrompt(false, "CTR Transfer not finished!\nNo changes written to NAND.\n \nCheck lower screen for info.");
    else {
        ShowString("CTR Transfer completed succesfully!\n \nEject your SD card now to reboot.");
        while(InputWait() != SD_EJECT);
    }
    ClearScreenF(true, true, COLOR_STD_BG);
    Reboot();
    return 0;
}
