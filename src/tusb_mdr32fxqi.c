#include "tusb.h"
#include <MDR32FxQI_rst_clk.h>
#include "MDR32FxQI_usb.h"
#include "device/dcd.h"

// Внутренние структуры для хранения состояния конечных точек
typedef struct {
    uint8_t* buffer;
    uint16_t total_len;
    uint8_t  class_ep;
} ep_state_t;

static ep_state_t ep_state[4][2]; // Инициализация массива состояний четырех конечных точек (IN и OUT на каждую точку)
uint32_t          set_addr = 0;

uint32_t sis;
uint32_t debug;
uint8_t  MSC_endpoint;

uint32_t tusb_time_millis_api(void);
void     board_get_unique_id(uint8_t* id, uint8_t max_len);

void handle_usb_device_reset(uint8_t rhport)
{
    (void)rhport;
    set_addr = 0;
    USB_SetSA(0);

	  for (int ep = 0; ep < 4; ep++) {
        ep_state[ep][TUSB_DIR_IN].buffer     = NULL;
        ep_state[ep][TUSB_DIR_IN].total_len  = 0;
        ep_state[ep][TUSB_DIR_OUT].buffer    = NULL;
        ep_state[ep][TUSB_DIR_OUT].total_len = 0;
    }
	
    for (int ep = 0; ep < 4; ep++) {
        USB_SetSEPxTXFDC(ep, 1);
        USB_SetSEPxRXFC(ep, 1);

        USB_SetSEPxCTRL(ep,
                        USB_SEPx_CTRL_EPEN_Disable |
                            USB_SEPx_CTRL_EPRDY_NotReady |
                            USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply |
                            USB_SEPx_CTRL_EPISOEN_Reset);

        USB_SetSEPxCTRL(ep,
                        USB_SEPx_CTRL_EPEN_Enable |
                            USB_SEPx_CTRL_EPRDY_Ready |
                            USB_SEPx_CTRL_EPDATASEQ_Data0 |
                            USB_SEPx_CTRL_EPSSTALL_NotReply |
                            USB_SEPx_CTRL_EPISOEN_Reset);
    }

    dcd_event_bus_reset(rhport, TUSB_SPEED_FULL, true);
}

static void handle_ep0_in(void)
{
    ep_state_t* state = &ep_state[0][TUSB_DIR_IN];

    USB_SetSEPxTXFDC(USB_EP0, 1);

    // Обработка ZLP (Zero Length Packet)
    if (state->total_len == 0) {
        USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPRDY_Ready);
        return;
    }

    for (uint16_t i = 0; i < (state->total_len); i++) {
        USB_SetSEPxTXFD(USB_EP0, state->buffer[i]);
    }

    USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPRDY_Ready);
}

static void handle_ep_in(uint8_t ep)
{
    ep_state_t* state = &ep_state[ep][TUSB_DIR_IN];

    USB_SetSEPxTXFDC(ep, 1);

    // Обработка ZLP (Zero Length Packet)
    if (state->total_len == 0) {
        USB_SetSEPxCTRL(ep, USB_SEPx_CTRL_EPRDY_Ready);
        return;
    }

    for (uint16_t i = 0; i < state->total_len; i++) {
        USB_SetSEPxTXFD(ep, state->buffer[i]);
    }

    USB_SetSEPxCTRL(ep, USB_SEPx_CTRL_EPRDY_Ready);
}

static void handle_ep0_out(uint8_t rhport)
{
    (void)rhport;

    ep_state_t* state = &ep_state[0][TUSB_DIR_OUT];

    uint32_t count = USB_GetSEPxRXFDC(USB_EP0);

    for (uint32_t i = 0; i < count; i++) {
        state->buffer[i] = USB_GetSEPxRXFD(USB_EP0);
    }

    USB_SetSEPxRXFC(USB_EP0, 1);

    dcd_event_xfer_complete(rhport, 0x00, count, XFER_RESULT_SUCCESS, true);
}

static void handle_ep_out(uint8_t rhport, uint8_t ep)
{
    (void)rhport;

    ep_state_t* state = &ep_state[ep][TUSB_DIR_OUT];

    uint32_t count = USB_GetSEPxRXFDC(ep);

    for (uint32_t i = 0; i < count; i++) {
        state->buffer[i] = USB_GetSEPxRXFD(ep);
    }
    USB_SetSEPxRXFC(ep, 1);

    dcd_event_xfer_complete(rhport, ep | 0x00, count, XFER_RESULT_SUCCESS, true);
}

bool dcd_init(uint8_t rhport, const tusb_rhport_init_t* rh_init)
{
    (void)rh_init;

		RST_CLK_HSEconfig(RST_CLK_HSE_ON);
    while(RST_CLK_HSEstatus() == ERROR) {}
    RST_CLK_CPUclkSelectionC1(RST_CLK_CPU_C1srcHSEdiv1);

		RST_CLK_CPU_PLLconfig(RST_CLK_CPU_PLLsrcHSEdiv1, RST_CLK_CPU_PLLmul10);
			
    RST_CLK_CPU_PLLcmd(ENABLE);
		while(RST_CLK_CPU_PLLstatus() == ERROR);
		RST_CLK_CPU_PLLuse(ENABLE);
			
		RST_CLK_CPUclkPrescaler(RST_CLK_CPUclkDIV1);
		RST_CLK_CPUclkSelection(RST_CLK_CPUclkCPU_C3);
			
    RST_CLK_PCLKcmd (RST_CLK_PCLK_USB, ENABLE);

    USB_Clock_TypeDef clock_cfg = {
        .USB_USBC1_Source = USB_C1HSEdiv1,
        .USB_PLLUSBMUL    = USB_PLLUSBMUL6
    };
    USB_BRGInit(&clock_cfg);

    USB_Reset();

    USB_SetHSCR(USB_HSCR_HOST_MODE_Device |
                USB_HSCR_EN_RX_Set |
                USB_HSCR_EN_TX_Set |
                USB_HSCR_DP_PULLUP_Set);

    USB_SetSC(USB_SC_SCGEN_Set |
              USB_SC_SCFSP_Full |
              USB_SC_SCFSR_12Mb);

    USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPEN_Enable |
                                 USB_SEPx_CTRL_EPRDY_Ready);

    for (uint32_t ep = 1; ep < 4; ep++) {
        get_class_by_endpoint(ep);
    }

    USB_SetSIM(USB_SIM_SCTDONEIE_Set |
               USB_SIM_SCRESETEVIE_Set);

    return true;
}

void dcd_int_enable(uint8_t rhport)
{
    (void)rhport;

    NVIC_EnableIRQ(USB_IRQn);
}

void dcd_int_disable(uint8_t rhport)
{
    (void)rhport;

    NVIC_DisableIRQ(USB_IRQn);
}

void dcd_set_address(uint8_t rhport, uint8_t dev_addr)
{
    (void)rhport;

    set_addr = dev_addr;
}

void dcd_remote_wakeup(uint8_t rhport)
{
    (void)rhport;
}

void dcd_connect(uint8_t rhport)
{
    (void)rhport;
}

void dcd_disconnect(uint8_t rhport)
{
    (void)rhport;
}

void dcd_sof_enable(uint8_t rhport, bool enable)
{
    (void)rhport;
}

bool dcd_edpt_open(uint8_t rhport, tusb_desc_endpoint_t const* ep_desc)
{
    (void)rhport;

    uint8_t const epnum = tu_edpt_number(ep_desc->bEndpointAddress);
    uint8_t const dir   = tu_edpt_dir(ep_desc->bEndpointAddress);
    uint32_t      ctrl  = USB_SEPx_CTRL_EPEN_Enable |
                    USB_SEPx_CTRL_EPDATASEQ_Data0 |
                    USB_SEPx_CTRL_EPSSTALL_NotReply |
                    USB_SEPx_CTRL_EPISOEN_Reset;

    ctrl |= USB_SEPx_CTRL_EPRDY_Ready;

    USB_SetSEPxCTRL((USB_EP_TypeDef)epnum, ctrl);

    USB_SetSEPxRXFC((USB_EP_TypeDef)epnum, 1);

    USB_SetSEPxTXFDC((USB_EP_TypeDef)epnum, 1);

    return true;
}

void dcd_edpt_close(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
}

void dcd_edpt_close_all(uint8_t rhport)
{
    (void)rhport;
}

bool dcd_edpt_xfer(uint8_t rhport, uint8_t ep_addr, uint8_t* buffer, uint16_t total_bytes, bool is_isr)
{
    (void)rhport;
    (void)is_isr;

    uint8_t epnum = tu_edpt_number(ep_addr);
    uint8_t dir   = tu_edpt_dir(ep_addr);

    ep_state_t* state = &ep_state[epnum][dir];

    state->buffer    = buffer;
    state->total_len = total_bytes;

    if (epnum == USB_EP0) {
        if (dir == TUSB_DIR_IN) {
            // Начать передачу IN
            handle_ep0_in();
        } else {
            // Готов к приему OUT
            USB_SetSEPxRXFC(USB_EP0, 1);
            USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPRDY_Ready);
        }
    } else {
        if (dir == TUSB_DIR_IN) {
            // Начать передачу IN
            handle_ep_in(epnum);
        } else {
            // Готов к приему OUT
            USB_SetSEPxRXFC(epnum, 1);
            USB_SetSEPxCTRL(epnum, USB_SEPx_CTRL_EPRDY_Ready);
        }
    }

    return true;
}

void dcd_edpt_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t epnum = tu_edpt_number(ep_addr);

    USB_SetSEPxTXFDC(epnum, 1);
    USB_SetSEPxRXFC(epnum, 1);

    USB_SetSEPxCTRL((USB_EP_TypeDef)epnum,
                    USB_SEPx_CTRL_EPSSTALL_Reply |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPRDY_Ready);
}

void dcd_edpt_clear_stall(uint8_t rhport, uint8_t ep_addr)
{
    (void)rhport;
    uint8_t epnum = tu_edpt_number(ep_addr);

    USB_SetSEPxTXFDC(epnum, 1);
    USB_SetSEPxRXFC(epnum, 1);

    USB_SetSEPxCTRL((USB_EP_TypeDef)epnum,
                    USB_SEPx_CTRL_EPSSTALL_NotReply |
                        USB_SEPx_CTRL_EPDATASEQ_Data0 |
                        USB_SEPx_CTRL_EPRDY_Ready);
}

void dcd_int_handler(uint8_t rhport)
{
    (void)rhport;

    if (sis & USB_SIS_SCRESETEV_Set) {
        handle_usb_device_reset(rhport);
        return;
    }

    if (sis & USB_SIS_SCRESUME_Set) {
        USB_SetSIS(USB_SIS_SCRESUME_Set);
        sis = USB_GetSIS();
        dcd_event_bus_signal(rhport, DCD_EVENT_RESUME, true);
    }

    if (sis & USB_SIS_SCNAKSENT_Set) {
        USB_SetSIS(USB_SIS_SCNAKSENT_Set);
        sis = USB_GetSIS();
    }

    if (sis & USB_SIS_SCTDONE_Set) {

        // Глобальная обработка оконечной точки EP0
        uint32_t ctrl = USB_GetSEPxCTRL(USB_EP0);
        uint32_t ts   = USB_GetSEPxTS(USB_EP0);
        uint32_t sts  = USB_GetSEPxSTS(USB_EP0);

        if ((USB_GetSEPxCTRL(USB_EP0) & USB_SEPx_CTRL_EPRDY_Ready) == 0) {
            // Обработка SETUP
            if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_Setup) {

                USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPDATASEQ_Data0); // Явная установка DATA0

                uint8_t setup[8];
                for (int i = 0; i < 8; i++) {
                    setup[i] = USB_GetSEPxRXFD(USB_EP0);
                }

                USB_SetSEPxRXFC(USB_EP0, 1);
                USB_SEPxToggleEPDATASEQ(USB_EP0);
                dcd_event_setup_received(rhport, setup, true); // Передача пакета SETUP в стек TinyUSB
								tud_task();
								USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPRDY_Ready);
								return;
            }

            // Обработка IN
            else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_In) {

                if (sts & USB_SEPx_STS_SCACKRXED_Set) {
                    ep_state_t* state = &ep_state[0][TUSB_DIR_IN];
                    dcd_event_xfer_complete(rhport, 0x80, state->total_len, XFER_RESULT_SUCCESS, true);
                    USB_SetSEPxTXFDC((USB_EP_TypeDef)USB_EP0, 1);

                    if (set_addr) {
                        USB_SetSA(set_addr);
                        set_addr = 0;
                    }

                    USB_SEPxToggleEPDATASEQ(USB_EP0);
                }
            }

            // Обработка OUT
            else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_Outdata) {
                handle_ep0_out(rhport);
            }

            USB_SetSEPxCTRL(USB_EP0, USB_SEPx_CTRL_EPRDY_Ready);
        }

        // Глобальная обработка оконечных точек EP1-EP3
        for (uint32_t ep = 1; ep < 4; ep++) {

            uint32_t ctrl = USB_GetSEPxCTRL(ep);

            if (ctrl & USB_SEPx_CTRL_EPRDY_Ready) {
                continue;
            }

            uint32_t ts  = USB_GetSEPxTS(ep);
            uint32_t sts = USB_GetSEPxSTS(ep);

            if (sts & USB_SEPx_STS_SCSTALLSENT_Set) {
                USB_SEPxToggleEPDATASEQ(ep);
                USB_SetSEPxCTRL(ep, USB_SEPx_CTRL_EPRDY_Ready);
            }

            // Обработка IN
            if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_In) {

                if (sts & USB_SEPx_STS_SCACKRXED_Set) {
                    USB_SetSEPxTXFDC((USB_EP_TypeDef)ep, 1);
                    ep_state_t* state = &ep_state[ep][TUSB_DIR_IN];
                    dcd_event_xfer_complete(rhport, ep | 0x80, state->total_len, XFER_RESULT_SUCCESS, true);
                    USB_SEPxToggleEPDATASEQ(ep);
                }
            }
            // Обработка OUT
            else if ((ts & USB_SEPx_TS_SCTTYPE_Msk) == USB_SEPx_TS_SCTTYPE_Outdata) {
                handle_ep_out(rhport, ep);
            }

            /* Если класс точки не MSC, то восстановить работу EPRDY этой точки и точки с MSC,
            при отдельной обработке точки MSC её работу восстанавливать не нужно, поскольку это сделает стек */
            if ((ep_state[ep][TUSB_DIR_OUT].class_ep != TUSB_CLASS_MSC)) {
                USB_SetSEPxCTRL(ep, USB_SEPx_CTRL_EPRDY_Ready);
                uint32_t sts = USB_GetSEPxSTS(MSC_endpoint);
                if (sts & USB_SEPx_STS_SCNAKSENT_Set) {
                    USB_SetSEPxCTRL(MSC_endpoint, USB_SEPx_CTRL_EPRDY_Ready);
                }
            }
        }
    }
}

uint32_t tusb_time_millis_api(void)
{
    return 0;
}

void board_get_unique_id(uint8_t* id, uint8_t max_len)
{
    const uint8_t default_id[] = { 0xDE, 0xAD, 0xBE, 0xEF, 0xCA, 0xFE, 0xBA, 0xBE };

    for (uint8_t i = 0; i < max_len; i++) {
        id[i] = (i < sizeof(default_id)) ? default_id[i] : 0;
    }
}

tusb_class_code_t get_class_by_endpoint(uint8_t ep_addr)
{
    uint8_t const*    desc          = tud_descriptor_configuration_cb(0);
    uint8_t const*    ptr           = desc;
    uint8_t const*    end           = ptr + ((tusb_desc_configuration_t*)desc)->wTotalLength;
    tusb_class_code_t current_class = TUSB_CLASS_UNSPECIFIED;

    while (ptr < end) {
        uint8_t len  = ptr[0];
        uint8_t type = ptr[1];

        if (type == TUSB_DESC_INTERFACE) {
            current_class = (tusb_class_code_t)((tusb_desc_interface_t*)ptr)->bInterfaceClass;
        } else if (type == TUSB_DESC_ENDPOINT) {
            if (((tusb_desc_endpoint_t*)ptr)->bEndpointAddress == ep_addr) {
                ep_state[ep_addr][TUSB_DIR_OUT].class_ep = current_class;
                if (current_class == TUSB_CLASS_MSC) {
                    MSC_endpoint = ep_addr;
                }
                return current_class;
            }
        }
        ptr += len;
    }
    return TUSB_CLASS_UNSPECIFIED;
}

void USB_IRQHandler(uint8_t rhport)
{
    (void)rhport;
    sis = USB_GetSIS();
    dcd_int_handler(rhport);
    USB_SetSIS(USB_SIS_Msk);
    sis = USB_GetSIS();
}
