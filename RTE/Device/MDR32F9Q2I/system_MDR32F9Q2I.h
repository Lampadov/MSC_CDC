/**
  * FILE system_MDR32F9Q2I.h
  */

/** @addtogroup __CMSIS CMSIS
  * @{
  */

/** @defgroup MDR32F9Q2I MDR32F9Q2I
 *  @{
 */

/** @addtogroup MDR32F9Q2I_System MDR32F9Q2I System
  * @{
  */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __SYSTEM_MDR32F9Q2I_H
#define __SYSTEM_MDR32F9Q2I_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h> /* standard types definitions */

/** @addtogroup __MDR32F9Q2I_System_Exported_Variables MDR32F9Q2I System Exported Variables
  * @{
  */

extern uint32_t SystemCoreClock;    /*!< System Clock Frequency (Core Clock) default value */

/** @} */ /* End of group __MDR32F9Q2I_System_Exported_Variables */

/** @addtogroup __MDR32F9Q2I_System_Exported_Functions MDR32F9Q2I System Exported Functions
  * @{
  */

extern void SystemInit(void);
extern void SystemCoreClockUpdate(void);

/** @} */ /* End of group __MDR32F9Q2I_System_Exported_Functions */

#ifdef __cplusplus
}
#endif

#endif /*__SYSTEM_MDR32F9Q2I_H */

/** @} */ /* End of group MDR32F9Q2I_System */

/** @} */ /* End of group MDR32F9Q2I */

/** @} */ /* End of group __CMSIS */

/*
*
* END OF FILE system_MDR32F9Q2I.h */

