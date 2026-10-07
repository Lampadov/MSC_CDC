/**
  ******************************************************************************
  * @file    MDR32F9Qx_config.h
  * @author  Milandr Application Team
  * @version V2.0.2
  * @date    22/09/2021
  * @brief   Library configuration file.
  ******************************************************************************
  * <br><br>
  * THE PRESENT FIRMWARE WHICH IS FOR GUIDANCE ONLY AIMS AT PROVIDING CUSTOMERS
  * WITH CODING INFORMATION REGARDING THEIR PRODUCTS IN ORDER FOR THEM TO SAVE
  * TIME. AS A RESULT, MILANDR SHALL NOT BE HELD LIABLE FOR ANY
  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES WITH RESPECT TO ANY CLAIMS ARISING
  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR THE USE MADE BY CUSTOMERS OF THE
  * CODING INFORMATION CONTAINED HEREIN IN CONNECTION WITH THEIR PRODUCTS.
  *
  * <h2><center>&copy; COPYRIGHT 2021 Milandr</center></h2>
  */

/**
  * @mainpage MDR32Fx Standard Peripherals Library.
  * MDR32Fx Standard Peripherals Library is a package consisting of
  * all standard peripheral device drivers for 1986VE9x, K1986VE9x, MDR32F9Qx,
  * 1986VE1T, 1986VE3T, 1901VC1T microcontrollers.
  * This library is a firmware package which contains a collection of routines,
  * data structures and macros covering the features of Milandr MDR32Fx
  * peripherals. It includes a description of the device drivers plus a set of
  * examples for each peripheral. The firmware library allows any device to be
  * used in the user application without the need for in-depth study of each
  * peripherals specifications. Using the Standard Peripherals Library has two
  * advantages: it saves significant time that would otherwise be spent in
  * coding, while simultaneously reducing application development and
  * integration costs.
  *
  * The MDR32Fx Standard Peripherals Library is compatible with Milandr
  * 1986BE9x evaluation boards, Milandr evaluation board for MC 1986VE1T
  * (EVAL 22.0 B) and evaluation board for MC 1986VE3T.
  *
  * The MDR32Fx Standard Peripherals Library is full CMSIS compliant.
  */

// <<< Use Configuration Wizard in Context Menu >>>


/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MDR32F9Qx_CONFIG_H
#define __MDR32F9Qx_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include <stdint.h>
#include <RTE_Components.h> // Keil uVision specific inclusion

/* Uncomment the line corresponding to the target microcontroller */
/* When using Keil uVision, the definition is set automatically
   (when RTE_Components.h included) according to the selected
   microcontroller in "Options for Target->Device" window.
   For MDR1986VE1T and MDR1986VE3T revision is selected in 
   "Device->Startup" variant in "Manage Run-Time Environment" window */
// #define USE_MDR1986VE91
// #define USE_MDR1986VE92
// #define USE_MDR1986VE93
// #define USE_MDR1986VE94
// #define USE_MDR1986VE1T_REV3_4
// #define USE_MDR1986VE1T_REV6
// #define USE_MDR1986VE3T_REV2
// #define USE_MDR1986VE3T_REV3
// #define USE_MDR1901VC1T


#if defined (USE_MDR1986VE91) || defined (USE_MDR1986VE92) || defined (USE_MDR1986VE93) || defined (USE_MDR1986VE94)
    #undef USE_MDR1986VE9x
    #define USE_MDR1986VE9x
#endif

/* Select the header file for target microcontroller */
#if defined (USE_MDR1986VE9x)
    #include "MDR32Fx.h"
#elif defined (USE_MDR1986VE1T_REV3_4) || defined (USE_MDR1986VE1T_REV6)
    #define USE_MDR1986VE1T
    #include "MDR1986VE1T.h"
#elif defined (USE_MDR1986VE3T_REV2) || defined (USE_MDR1986VE3T_REV3)
    #define USE_MDR1986VE3
    #include "MDR1986VE3.h"
#elif defined (USE_MDR1901VC1T)
    #include "MDR1901VC1T.h"
#else
    #error "Microcontroller not selected in MDR32F9Qx_config.h file"
#endif


// <o> Legacy support definition
// <i> Specifies level of legacy support. If legacy support is not needed, outcomment this definition or change default value.
// <i> Support for the legacy version is removed after 2 updates.
// <i> Default value for version 2.0.x: 153 - to support previous version (1.5.3)
#define MDR_LEGACY_SUPPORT 153


#if (defined(USE_MDR1986VE9x) || defined (USE_MDR1901VC1T))
// <h> JTAG pins protection for MCU MDR1986VE9x and MDR1901VC1T
// <i> Prevents RTXT and OE bits setting for JTAG when doing GPIO read-modify-write
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


// <h> Target system parameters
// <h> RST_CLK generators frequencies
// <o> HSI clock value [Hz]
// <i> Default: 8000000 (8MHz)
#define HSI_Value       ((uint32_t)8000000)
// <o> HSE clock value [Hz]
// <i> Default: 8000000 (8MHz)
#define HSE_Value       ((uint32_t)8000000)
// <o> HSE2 clock value [Hz] for MDR1986VE1 and MDR1986VE3
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
// <o> HSE2 timeout startup value for MCU MDR1986VE1 and MDR1986VE3
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
// <o> PLLDSP timeout startup value for MCU MDR1901VC1
// <i> Default: 0x0600
#define PLLDSPonTimeOut ((uint16_t)0x0600)
// </h>

// <o> EEPROM controller freq [MHz]
 //<i> Default: 8MHz
#define FLASH_PROG_FREQ_MHZ     (8.0)
// </h>


// <h> Controller blocks settings
// <h> DMA configuration parameters
// <o.0..5> Number of DMA channels to use
// <i> This parameter is in range 1..32
// <i> Default: 32
#define DMA_Channels_Number   32

// <o> Alternate Control Data Structure Usage
//   <0=> 0: DMA_ALternateDataDisabled
//   <1=> 1: DMA_ALternateDataEnabled
// <i> Default: 1 (DMA_ALternateDataEnabled)
#define DMA_AlternateData     1
// </h>

// </h>

// <h> Examples settings
// <c2> Use debug UART
// <i> Definition used in examples
// <i> Default: commented/not defined
    //#define _USE_DEBUG_UART_
// </c>

#if defined (_USE_DEBUG_UART_)

#if defined (USE_MDR1986VE3)
    #define DEBUG_UART                  MDR_UART2
    #define DEBUG_UART_PORT             MDR_PORTD
    #define DEBUG_UART_PINS             (PORT_Pin_13 | PORT_Pin_14)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_MAIN
    #define DEBUG_BAUD_RATE             115200
#elif defined (USE_MDR1986VE1T)
    #define DEBUG_UART                  MDR_UART1
    #define DEBUG_UART_PORT             MDR_PORTC
    #define DEBUG_UART_PINS             (PORT_Pin_3 | PORT_Pin_4)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_MAIN
    #define DEBUG_BAUD_RATE             115200
#elif defined (USE_MDR1986VE9x)
    #define DEBUG_UART                  MDR_UART2
    #define DEBUG_UART_PORT             MDR_PORTF
    #define DEBUG_UART_PINS             (PORT_Pin_0 | PORT_Pin_1)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_OVERRID
    #define DEBUG_BAUD_RATE             115200
#elif defined (USE_MDR1901VC1T)
    #define DEBUG_UART                  MDR_UART3
    #define DEBUG_UART_PORT             MDR_PORTF
    #define DEBUG_UART_PINS             (PORT_Pin_0 | PORT_Pin_1)
    #define DEBUG_UART_PINS_FUNCTION    PORT_FUNC_ALTER
    #define DEBUG_BAUD_RATE             115200
#endif

#endif /* #if defined (_USE_DEBUG_UART_) */


#if defined (USE_MDR1986VE3) || defined (USE_MDR1986VE1T)
    // <o> MIL STD 1553 terminal address for MDR1986VE1 and MDR1986VE3
    // <i> Default: 0x01
    #define MIL_STD_1553_TERMINAL_ADDRESS    0x01
#endif /* #if defined ( USE_MDR1986VE3 ) || defined ( USE_MDR1986VE1T ) */


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
// <c2> MDR1986VE9x Series Errata Notice, Error 0002
// <i> CAN error workaround
// <i> Default: not commented/defined
#define WORKAROUND_MDR32F9QX_ERROR_0002
// </c>
// </h>

#if defined (__ICCARM__)
    #define __attribute__(name_section)
    #if defined (USE_MDR1986VE3) || defined (USE_MDR1986VE1T)
        #pragma section = "EXECUTABLE_MEMORY_SECTION"
        #define IAR_SECTION(section) @ section
    #elif defined (USE_MDR1986VE9x)
        #define IAR_SECTION(section)
    #endif
#endif
#if defined (__CMCARM__)
    #define __attribute__(name_section)
    #define IAR_SECTION(section)
#endif
#if defined (__ARMCC_VERSION)
    #define IAR_SECTION(section)
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

#endif /* __MDR32F9Qx_CONFIG_H */

/*********************** (C) COPYRIGHT 2021 Milandr ****************************
*
* END OF FILE MDR32F9Qx_config.h */
