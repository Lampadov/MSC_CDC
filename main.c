#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"
#include "stdio.h"

void Delay(int waitTicks);
void cdc_task(void);

#define LED_PERIOD 50000
#define VD3 PORT_Pin_0
#define VD4 PORT_Pin_1

#define LED_ALL VD3 | VD4

int main()
{	
  PORT_InitTypeDef GPIOInitStruct;
	
  RST_CLK_PCLKcmd (RST_CLK_PCLK_PORTC, ENABLE);
  PORT_StructInit(&GPIOInitStruct);
  
  GPIOInitStruct.PORT_Pin        = LED_ALL;
  GPIOInitStruct.PORT_OE         = PORT_OE_OUT;
  GPIOInitStruct.PORT_SPEED      = PORT_SPEED_SLOW;
  GPIOInitStruct.PORT_MODE       = PORT_MODE_DIGITAL;
  PORT_Init(MDR_PORTC, &GPIOInitStruct);
	
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
  tusb_init(0, &dev_init);
	
  while (1)
  {
		tud_task(); // tinyusb device task
		cdc_task();
  }      
}


void cdc_task(void)
{
  // Ёхо: всЄ, что пришло в CDC, отправл€ем обратно
  // „итаем не больше, чем влезет в TX-буфер: иначе при медленном хосте байты тер€ютс€
  uint32_t room = tud_cdc_write_available();
  if (room > 0 && tud_cdc_available()) {
    uint8_t buf[64];
    uint32_t n = tud_cdc_read(buf, room < sizeof(buf) ? room : sizeof(buf));
    tud_cdc_write(buf, n);
    tud_cdc_write_flush();
  }
}

void Delay(int waitTicks)
{
  int i;
  for (i = 0; i < waitTicks; i++)
  {
    __NOP();
  }	
}

