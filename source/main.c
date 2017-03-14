#include "transfer.h"
#include "ui.h"
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
    if (ret) ShowPrompt(false, "CTR Transfer not finished!\nNo changes written to NAND.\n \nCheck lower screen for info.");
    else ShowPrompt(false, "CTR Transfer success!");
    ClearScreenF(true, true, COLOR_STD_BG);
    fs_deinit();
    Reboot();
    return 0;
}
