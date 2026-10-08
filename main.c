#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"

/*
 * USB-флешка (Mass Storage) на RAM-диске. Содержимое диска - в src/msc_disk.c.
 * Светодиод VD3 управляется файлом LED.TXT на диске, VD4 горит, когда хост настроил устройство.
 */

#define VD3  PORT_Pin_0
#define VD4  PORT_Pin_1

void msc_disk_init(void);

// Вызывается из msc_disk.c, когда в LED.TXT записали 1 или 0
void led_set(bool on)
{
    if (on) PORT_SetBits(MDR_PORTC, VD3); else PORT_ResetBits(MDR_PORTC, VD3);
}

static void gpio_init(void)
{
    PORT_InitTypeDef gpio;

    RST_CLK_PCLKcmd(RST_CLK_PCLK_PORTC, ENABLE);
    PORT_StructInit(&gpio);
    gpio.PORT_Pin   = VD3 | VD4;
    gpio.PORT_OE    = PORT_OE_OUT;
    gpio.PORT_SPEED = PORT_SPEED_SLOW;
    gpio.PORT_MODE  = PORT_MODE_DIGITAL;
    PORT_Init(MDR_PORTC, &gpio);
}

int main(void)
{
    gpio_init();
    msc_disk_init();                     // создаём образ диска в RAM

    tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
    tusb_init(0, &dev_init);

    while (1)
    {
        tud_task();                      // обработка USB-стека

        if (tud_mounted()) PORT_SetBits(MDR_PORTC, VD4); else PORT_ResetBits(MDR_PORTC, VD4);
    }
}
