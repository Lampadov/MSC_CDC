/**
  ******************************************************************************
  * @file    MDR32FxQI_config.h
  * @author  Milandr Application Team
  * @version V2.3.0i
  * @date    09/12/2024
  * @brief   Library configuration file.
  ******************************************************************************
  * <br><br>
  * THE PRESENT FIRMWARE IS FOR GUIDANCE ONLY. IT AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING MILANDR'S PRODUCTS IN ORDER TO FACILITATE
  * THE USE AND SAVE TIME. MILANDR SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES RESULTING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR A USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2025 Milandr</center></h2>
  */

/**
  * @mainpage MDR32FxQI Standard Peripherals Library.
  * MDR32FxQI Standard Peripherals Library is a package consisting of
  * all standard peripheral device drivers for MDR32F1QI, K1986VE1xI,
  * MDR32F9Q2I, K1986VE9xI, MDR32FG16S1QI microcontrollers.
  * This library is a firmware package which contains a collection of routines,
  * data structures and macros covering the features of Milandr MDR32FxQI
  * peripherals. It includes a description of the device drivers plus a set of
  * examples for each peripheral. The firmware library allows any device to be
  * used in the user application without the need for in-depth study of each
  * peripherals specifications. Using the Standard Peripherals Library has two
  * advantages: it saves significant time that would otherwise be spent in
  * coding, while simultaneously reducing application development and
  * integration costs.
  *
  * The MDR32FxQI Standard Peripherals Library is full CMSIS compliant.
  */

// <<< Use Configuration Wizard in Context Menu >>>


/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MDR32FxQI_CONFIG_H
#define __MDR32FxQI_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <RTE_Components.h> // Keil uVision specific inclusion

/* MDR32FxQI SPL version numbers - major, minor, patch */
#define _MDR32FxQI_SPL_VERSION_MAJOR (1U) /*!< [23:16] major version */
#define _MDR32FxQI_SPL_VERSION_MINOR (3U) /*!< [15:8]  minor version */
#define _MDR32FxQI_SPL_VERSION_PATCH (0U) /*!< [7:0]   patch version */

/* MDR32FxQI SPL version number (combined value) */
#define MDR32FxQI_SPL_VERSION ((_MDR32FxQI_SPL_VERSION_MAJOR << 16) | \
                               (_MDR32FxQI_SPL_VERSION_MINOR << 8) |  \
                               (_MDR32FxQI_SPL_VERSION_PATCH))

/* Uncomment the line corresponding to the target microcontroller */
/* When using Keil uVision, the definition is set automatically
   (when RTE_Components.h included) according to the selected
   microcontroller in "Options for Target->Device" window.
   For MDR32F1QI revision is selected in 
   "Device->Startup" variant in "Manage Run-Time Environment" window */
// #define USE_MDR32F9Q2I
// #define USE_MDR32F1QI_REV3_4
// #define USE_MDR32F1QI_REV6
// #define USE_MDR32FG16S1QI
// #define USE_K1986VE92xI
// #define USE_K1986VE94GI
// #define USE_K1986VE1xI

/* Select the header file for target microcontroller */
#if defined (USE_MDR32F9Q2I)
    #define USE_MDR32F9xI
    #define USE_K1986VE9xI
    #include "MDR32F9Q2I.h"
#elif defined (USE_K1986VE92xI) || defined (USE_K1986VE94GI)
    #define USE_K1986VE9xI
    #include "K1986VE9xI.h"
#elif defined (USE_MDR32F1QI_REV3_4) || defined (USE_MDR32F1QI_REV6)
    #define USE_MDR32F1QI
    #define USE_K1986VE1xI
    #include "MDR32F1QI.h"
#elif defined (USE_K1986VE1xI)
    #include "K1986VE1xI.h"
#elif defined (USE_MDR32FG16S1QI)
    #include "MDR32FG16S1QI.h"
#else
    #error "Microcontroller not selected in MDR32FxQI_config.h file"
#endif


#if (defined(USE_K1986VE9xI) || defined (USE_MDR32FG16S1QI))
// <h> JTAG pins protection for MCU MDR32F9Q2I, K1986VE9xI and MDR32FG16S1QI
// <i> Prevents RXTX and OE bits setting for JTAG when doing GPIO read-modify-write
// <i> Uncomment the definition(s) below to define used JTAG port(s).
// <i> Leave all commented/unchecked if there is no GPIO pins combined with JTAG.

// <c> JTAG_A pins protection
     //#define USE_JTAG_A
// </c>

// <c> JTAG_B pins protection
    #define USE_JTAG_B
// </c>
// </h>
#endif

#if (defined(USE_K1986VE9xI) && !defined(USE_MDR32F9xI)) || (defined(USE_K1986VE1xI) && !defined(USE_MDR32F1QI))
// <h> Factory data management for MCU K1986VE92xI (K1986VE92FI, K1986VE92F1I), K1986VE94GI and K1986VE1xI (K1986VE1FI, K1986VE1GI)
// <o> Factory data initialization in SystemInit() (called before entering main())
//   <0=> 0: REG_0E (Trim[2:0] bits) and REG_0F (HSI TRIM[5:0] and LSI TRIM[4:0] bits) are NOT initialized from factory data.
//   <1=> 1: REG_0E (Trim[2:0] bits) and REG_0F (HSI TRIM[5:0] and LSI TRIM[4:0] bits) are initialized from factory data.
// <i> Default: 1 (Use initialization).
// <i> ATTENTION: Factory data stored in EEPROM must ALWAYS be used to ensure the correct operation of MCU. Setting this parameter to 0 is allowed only if the use of factory data is implemented elsewhere in the software.
#define FACTORY_DATA_SYSTEM_INIT 1
// </h>
#endif

// <h> Target system parameters
// <h> RST_CLK generators frequencies
// <o> HSI clock value [Hz]
// <i> Default: 8000000 (8MHz)
#define HSI_Value       ((uint32_t)8000000)
// <o> HSE clock value [Hz]
// <i> Default: 8000000 (8MHz)
#define HSE_Value       ((uint32_t)8000000)
// <o> HSE2 clock value [Hz] for MDR32F1QI, K1986VE1xI
// <i> Default: 25000000 (25MHz)
#define HSE2_Value      ((uint32_t)25000000)
// <o> LSI clock value [Hz]
// <i> Default: 40000 (40kHz)
#define LSI_Value       ((uint32_t)40000)
// <o> LSE clock value [Hz]
// <i> Default: 32768 (32.768kHz)
#define LSE_Value       ((uint32_t)32768)
// </h>


// <h> RST_CLK frequencies startup timeouts settings
// <o> HSE timeout startup value
// <i> Default: 0x0600
#define HSEonTimeOut    ((uint16_t)0x0600)
// <o> HSE2 timeout startup value for MCU MDR32F1QI, K1986VE1xI
// <i> Default: 0x8000
#define HSE2onTimeOut   ((uint16_t)0x8000)
// <o> LSE timeout startup value
// <i> Default: 0x0600
#define LSEonTimeOut    ((uint16_t)0x0600)
// <o> HSI timeout startup value
// <i> Default: 0x0600
#define HSIonTimeOut    ((uint16_t)0x0600)
// <o> LSI timeout startup value
// <i> Default: 0x0600
#define LSIonTimeOut    ((uint16_t)0x0600)
// <o> PLLCPU timeout startup value
// <i> Default: 0x0600
#define PLLCPUonTimeOut ((uint16_t)0x0600)
// <o> PLLUSB timeout startup value
// <i> Default: 0x0600
#define PLLUSBonTimeOut ((uint16_t)0x0600)
// <o> PLLDSP timeout startup value for MCU MDR32FG16S1QI
// <i> Default: 0x0600
#define PLLDSPonTimeOut ((uint16_t)0x0600)
// </h>

// <o> EEPROM controller freq [MHz]
 //<i> Default: 8MHz
#define FLASH_PROG_FREQ_MHZ     (8.0)
// </h>


// <h> Controller blocks settings
// <h> DMA configuration parameters

// <o> Alternate Control Data Structure Usage
//   <0=> 0: DMA_ALternateDataDisabled
//   <1=> 1: DMA_ALternateDataEnabled
// <i> Default: 1 (DMA_ALternateDataEnabled)
#define DMA_AlternateData 1

#if (DMA_AlternateData != 0) && (DMA_AlternateData != 1)
#error "DMA_AlternateData  should be 0 (DMA_ALternateDataDisabled) or 1 (DMA_ALternateDataEnabled)"
#endif

#if (DMA_AlternateData == 0)
// <o.0..5> Number of DMA channels main structure to use.
// <i> Specifies size of structure table for channels in range [0; DMA_Channels_Number-1] where 0 is channel 0.
// <i> Could be changed to reduce RAM usage.
// <i> If DMA_AlternateData = 1, 32 channels for main structure are always used.
// <i> This parameter is in range [1; 32]
// <i> Default: 32
#define DMA_Channels_Number 32

#if (DMA_Channels_Number < 1) || (DMA_Channels_Number > 32)
#error "DMA_Channels_Number should be in range [1; 32]"
#endif

#endif

#if (DMA_AlternateData == 1)
// <o.0..5> Number of DMA channels alternate structure to use.
// <i> Specifies size of structure table for channels in range [0; 32 + DMA_Channels_Number-1] where 0 is channel 0.
// <i> Could be changed to reduce RAM usage.
// <i> If DMA_AlternateData = 1, 32 channels for main structure are always used.
// <i> This parameter is in range [1; 32]
// <i> Default: 32
#define DMA_Channels_Number 32

#if (DMA_Channels_Number < 1) || (DMA_Channels_Number > 32)
#error "DMA_Channels_Number should be in range [1; 32]"
#endif

#endif

// </h>
// </h>


// <h> Retarget standard input/output (STDIO)
// <c1> Use retarget STDIO via UART
// <i> Default: _USE_DEBUG_UART_ commented/not defined
    //#define _USE_DEBUG_UART_
// </c>
#if defined (_USE_DEBUG_UART_)

#define DEBUG_BAUD_RATE                 115200
#define DEBUG_WORD_LENGTH               UART_WordLength8b
#define DEBUG_STOP_BITS                 UART_StopBits1
#define DEBUG_BAUD_PARITY               UART_Parity_No

#if defined (USE_K1986VE1xI)
    #define DEBUG_UART                  MDR_UART1
    #define DEBUG_UART_PORT             MDR_PORTC
    #define DEBUG_UART_PINS             (PORT_Pin_3 | PORT_Pin_4)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_MAIN
#elif defined (USE_K1986VE9xI)
    #define DEBUG_UART                  MDR_UART2
    #define DEBUG_UART_PORT             MDR_PORTF
    #define DEBUG_UART_PINS             (PORT_Pin_0 | PORT_Pin_1)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_OVERRID
#elif defined (USE_MDR32FG16S1QI)
    #define DEBUG_UART                  MDR_UART3
    #define DEBUG_UART_PORT             MDR_PORTF
    #define DEBUG_UART_PINS             (PORT_Pin_0 | PORT_Pin_1)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_ALTER
#endif

#endif /* #if defined (_USE_DEBUG_UART_) */
// </h>


// <h> Examples settings
#if defined (USE_K1986VE1xI)
    // <o> MIL STD 1553 terminal address for MDR32F1QI, K1986VE1xI
    // <i> Default: 0x01
    #define MIL_STD_1553_TERMINAL_ADDRESS    0x01
#endif /* #if defined (USE_K1986VE1xI) */

// <h> RTC configuration parameters
// <o> RTC calibration value
// <i> Default: 0
#define RTC_CalibratorValue     0
// <o> RTC prescaler value
// <i> Default: 32768
#define RTC_PRESCALER_VALUE     32768
// </h>

/* ---------------------- USB configuration parameters -----------------------*/
/* Supported USB Device Classes */
#define USB_DEVICE_CLASS_CDC 2

/* Uncomment one of the lines below to select the Device Class. Leave all commented
 * if the desired device class is not currently supported by USB library */
#define USB_DEVICE_CLASS  USB_DEVICE_CLASS_CDC

/* USB Device management */
/* Uncomment the line below to enable appropriate functionality. */
// #define USB_REMOTE_WAKEUP_SUPPORTED
// #define USB_SELF_POWERED_SUPPORTED

/* Uncomment the line below to let the library provide USB interrupt handler.
 * Leave this line commented if you are willing to implement the handler yourself. */
#define USB_INT_HANDLE_REQUIRED

/* USB CDC management */
/* Uncomment the lines below to enable appropriate functionality. */
//#define USB_CDC_STATE_REPORTING_SUPPORTED
//#define USB_CDC_ENCAPSULATION_SUPPORTED
//#define USB_CDC_COMM_FEATURE_SUPPORTED
#define USB_CDC_LINE_CODING_SUPPORTED
//#define USB_CDC_CONTROL_LINE_STATE_SUPPORTED
//#define USB_CDC_LINE_BREAK_SUPPORTED

/* VCOM Echo example flags */

/* Uncomment USB_VCOM_SYNC to enable "reliable delivery" mode: no new data
 * would be received (EP3 will reply NAK) until all previous data is sent
 * to host. */
//#define USB_VCOM_SYNC

/* Uncomment USB_DEBUG_PROTO to utilize the ring buffer for received setup
 * packets and send/receive byte counters (for debug purposes). */
//#define USB_DEBUG_PROTO

// </h>


// <h> Known errors workaround control
// <c2> MDR32F1QI, K1986VE1xI Series Errata Notice, ETH PHY data inversion error in 10Base-T mode
// <i>  Workaround for "10Base-T HD/FD transmitted data inverted after EthernetPHY reset" error
// <i> Default: not commented/defined
#define WORKAROUND_K1986VE1xI_ERROR_ETH_PHY_10BASE_T_DATA_INVERSION
// </c>
// </h>

#ifdef WORKAROUND_K1986VE1xI_ERROR_ETH_PHY_10BASE_T_DATA_INVERSION
#define WORKAROUND_MDR32F1QI_ERROR_ETH_PHY_10BASE_T_DATA_INVERSION
#endif

#if defined (__ICCARM__) /* IAR Compiler */
    #define __attribute__(name_section)
    #if defined (USE_K1986VE1xI)
        #pragma section = "EXECUTABLE_MEMORY_SECTION"
        #define IAR_SECTION(section) @ section
    #elif defined (USE_K1986VE9xI)
        #define IAR_SECTION(section)
    #endif
    #define __RAMFUNC __ramfunc
#endif
#if defined (__CMCARM__) /* Phyton CMC-ARM Compiler */
    #define __attribute__(name_section)
    #define IAR_SECTION(section)
    #define __RAMFUNC __ramfunc
#endif
#if defined (__ARMCC_VERSION) /* ARM Compiler */
    #define IAR_SECTION(section)
    #define __RAMFUNC __attribute__((section("EXECUTABLE_MEMORY_SECTION")))
#endif


// <h> Parameter run-time check support

// <o> Paramater checking level
//   <0=> 0: no parameter checks ("assert_param" macro is disabled)
//   <1=> 1: check enabled, source file ID and line number are available
//   <2=> 2: check enabled, source file ID, line number and checking expression (as string) are available (increased code size)
// <i> Default: 0 ("assert_param" macro is disabled)
#define USE_ASSERT_INFO    0

/**
  * @brief  The assert_param macro is used for function's parameters check.
  * @param  expr: If expr is false, it calls assert_failed user's function which 
  *         reports the name of the source file, source line number
  *         and expression text (if USE_ASSERT_INFO == 2) of the call that failed.
  *         That function should not return. If expr is true, nothing is done.
  * @retval None
  */
#if (USE_ASSERT_INFO == 0)
    #define assert_param(expr) ((void)0U)
#elif (USE_ASSERT_INFO == 1)
    #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__))
    void assert_failed(uint8_t* file, uint32_t line);
#elif (USE_ASSERT_INFO == 2)
    #define assert_param(expr) ((expr) ? (void)0U : assert_failed((uint8_t *)__FILE__, __LINE__, (const uint8_t*) #expr))
    void assert_failed(uint8_t* file, uint32_t line, const uint8_t* expr);
#else
    #error "Unsupported USE_ASSERT_INFO level"
#endif /* USE_ASSERT_INFO */

// </h>

#ifdef __cplusplus
}
#endif

#endif /* __MDR32FxQI_CONFIG_H */

/*********************** (C) COPYRIGHT 2025 Milandr ****************************
*
* END OF FILE MDR32FxQI_config.h */

