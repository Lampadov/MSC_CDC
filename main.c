#include <MDR32FxQI_port.h>
#include <MDR32FxQI_rst_clk.h>

#include "tusb.h"
#include "stdio.h"

void Delay(int waitTicks);
void hid_task(void);

#define LED_PERIOD 50000
#define VD3 PORT_Pin_0
#define VD4 PORT_Pin_1

#define LED_ALL VD3 | VD4

typedef struct {
    MDR_PORT_TypeDef* port;
    uint16_t pin;
    uint8_t keycode;        
} button_t;

static const button_t buttons[] = {
    {MDR_PORTC, PORT_Pin_2, HID_KEY_ENTER},      // SELECT 
    {MDR_PORTB, PORT_Pin_5, HID_KEY_ARROW_UP},   // UP 
    {MDR_PORTE, PORT_Pin_1, HID_KEY_ARROW_DOWN}, // DOWN 
    {MDR_PORTE, PORT_Pin_3, HID_KEY_ARROW_LEFT}, // LEFT
    {MDR_PORTB, PORT_Pin_6, HID_KEY_ARROW_RIGHT} // RIGHT
};
#define NUM_BUTTONS 5

// Хранение предыдущего состояния для обнаружения изменений
static uint8_t prev_buttons = 0;
static int8_t prev_dx = 0;
static int8_t prev_dy = 0;

int main()
{	
  PORT_InitTypeDef GPIOInitStruct;
	
  RST_CLK_PCLKcmd (RST_CLK_PCLK_PORTC | RST_CLK_PCLK_PORTB | RST_CLK_PCLK_PORTE, ENABLE);
  PORT_StructInit(&GPIOInitStruct);
  
  GPIOInitStruct.PORT_Pin        = LED_ALL;
  GPIOInitStruct.PORT_OE         = PORT_OE_OUT;
  GPIOInitStruct.PORT_SPEED      = PORT_SPEED_SLOW;
  GPIOInitStruct.PORT_MODE       = PORT_MODE_DIGITAL;
  PORT_Init(MDR_PORTC, &GPIOInitStruct);
	
	GPIOInitStruct.PORT_Pin        = PORT_Pin_2;
  GPIOInitStruct.PORT_OE         = PORT_OE_IN;
  GPIOInitStruct.PORT_SPEED      = PORT_SPEED_SLOW;
  GPIOInitStruct.PORT_MODE       = PORT_MODE_DIGITAL;
  PORT_Init(MDR_PORTC, &GPIOInitStruct);
	
	GPIOInitStruct.PORT_Pin        = PORT_Pin_5 | PORT_Pin_6;
  GPIOInitStruct.PORT_OE         = PORT_OE_IN;
  GPIOInitStruct.PORT_SPEED      = PORT_SPEED_SLOW;
  GPIOInitStruct.PORT_MODE       = PORT_MODE_DIGITAL;
  PORT_Init(MDR_PORTB, &GPIOInitStruct);	
	
	GPIOInitStruct.PORT_Pin        = PORT_Pin_1 | PORT_Pin_3;
  GPIOInitStruct.PORT_OE         = PORT_OE_IN;
  GPIOInitStruct.PORT_SPEED      = PORT_SPEED_SLOW;
  GPIOInitStruct.PORT_MODE       = PORT_MODE_DIGITAL;
  PORT_Init(MDR_PORTE, &GPIOInitStruct);	
	
  tusb_rhport_init_t dev_init = {.role = TUSB_ROLE_DEVICE, .speed = TUSB_SPEED_FULL};
  tusb_init(0, &dev_init);
	
  while (1)
  {
		tud_task(); // tinyusb device task
		hid_task();
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


//void hid_task(void) {
//    if (!tud_hid_ready()) return;
//	
//    uint8_t current_mask = 0;
//    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
//        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
//            current_mask |= (1 << i);
//        }
//    }
//    if (current_mask != prev_buttons) {
//        uint8_t keycodes[6] = {0};
//        uint8_t idx = 0;
//        for (uint8_t i = 0; i < NUM_BUTTONS && idx < 6; i++) {
//            if (current_mask & (1 << i)) {
//                keycodes[idx++] = buttons[i].keycode;
//            }
//        }
//        tud_hid_keyboard_report(0, 0, keycodes);
//        prev_buttons = current_mask;
//    }
//}

void hid_task(void) {
    if (!tud_hid_ready()) return;

    // Опрашиваем все кнопки (нажатие = низкий уровень)
    uint8_t current_mask = 0;
    for (uint8_t i = 0; i < NUM_BUTTONS; i++) {
        if (PORT_ReadInputDataBit(buttons[i].port, buttons[i].pin) == 0) {
            current_mask |= (1 << i);
        }
    }

    // Кнопка SELECT - левая кнопка мыши
    uint8_t mouse_buttons = (current_mask & (1 << 0)) ? 0x01 : 0;

    // Остальные кнопки задают смещение курсора по x, y
    int8_t dx = 0, dy = 0;
    if (current_mask & (1 << 4)) dx += 20; // RIGHT
    if (current_mask & (1 << 3)) dx -= 20; // LEFT
    if (current_mask & (1 << 2)) dy += 20; // DOWN 
    if (current_mask & (1 << 1)) dy -= 20; // UP 

    // Отправляем отчёт только при изменении состояния
		if (mouse_buttons != prev_buttons || dx != prev_dx || dy != prev_dy) {
				tud_hid_mouse_report(0, mouse_buttons, dx, dy, 0, 0);
				prev_buttons = mouse_buttons;
				prev_dx = dx;
				prev_dy = dy;
		}
}


// Вызывается при получении отчёта от хоста (например, индикаторы клавиатуры)
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer, uint16_t bufsize) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)bufsize;

    // Здесь можно обработать, например, состояние NumLock/CapsLock
}

// Вызывается, когда хост запрашивает отчёт
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t* buffer, uint16_t reqlen) {
    (void)instance;
    (void)report_id;
    (void)report_type;
    (void)buffer;
    (void)reqlen;

    return 0;
}
