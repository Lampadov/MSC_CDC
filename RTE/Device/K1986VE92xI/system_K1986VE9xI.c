/**
  * FILE system_K1986VE9xI.c
  */


/** @addtogroup __CMSIS CMSIS
  * @{
  */

/** @defgroup K1986VE9xI K1986VE9xI
  *  @{
  */

/** @defgroup K1986VE9xI_System K1986VE9xI System
  * @{
  */

#include "K1986VE9xI.h"
#include "MDR32FxQI_config.h"

/** @addtogroup __K1986VE9xI_System_Private_Defines K1986VE9xI System Private Defines
  * @{
  */

#if defined (__ARMCC_VERSION) /* ARM Compiler */
    extern uint32_t __Vectors;
    #define __VECTOR_TABLE_ADDRESS &__Vectors
#elif defined (__ICCARM__) /* IAR Compiler */
    extern uint32_t __vector_table;
    #define __VECTOR_TABLE_ADDRESS &__vector_table
#elif defined (__CMCARM__) /* Phyton CMC-ARM Compiler */
    extern uint32_t __Vectors;
    #define __VECTOR_TABLE_ADDRESS &__Vectors
#endif

#if defined(FACTORY_DATA_SYSTEM_INIT) && (FACTORY_DATA_SYSTEM_INIT == 1)

#define EEPROM_FACTORY_READ_WORD_FUNC_ADDR 0x20000000
#define EEPROM_FACTORY_READ_WORD_FUNC_SIZE 16


// Precompiled form of the following code:
// #define CMD1 (EEPROM_CMD_CON | EEPROM_CMD_IFREN)
// #define CMD2 (EEPROM_CMD_XE | EEPROM_CMD_YE | EEPROM_CMD_SE)
// 
// __attribute__((section("EXECUTABLE_MEMORY_SECTION"))) uint32_t EEPROM_ReadWordFactory(uint32_t Address)
// {
//     uint32_t Command;
//     uint32_t Data;
//     
//     Command = (MDR_EEPROM->CMD & EEPROM_CMD_DELAY_Msk) | CMD1;
//     MDR_EEPROM->CMD = Command;
//     MDR_EEPROM->ADR = Address;
//     MDR_EEPROM->CMD = Command | CMD2;
//     MDR_EEPROM->DO;
//     MDR_EEPROM->DO;
//     MDR_EEPROM->DO;
//     Data = MDR_EEPROM->DO;
//     Command &= EEPROM_CMD_DELAY_Msk;
//     MDR_EEPROM->CMD = Command;
//     return Data;
// }
const uint32_t EEPROM_FACTORY_READ_WORD_FUNC[EEPROM_FACTORY_READ_WORD_FUNC_SIZE] = {0x4602B510,
                                                                                    0x681B4B0D,
                                                                                    0x40232438,
                                                                                    0x31FF1DD9,
                                                                                    0x4B0A31FB,
                                                                                    0x605A6019,
                                                                                    0x430B00E3,
                                                                                    0x60234C07,
                                                                                    0x68DB4623,
                                                                                    0x68DB4623,
                                                                                    0x68DB4623,
                                                                                    0x68D84623,
                                                                                    0x40192338,
                                                                                    0x60194623,
                                                                                    0x0000BD10,
                                                                                    0x40018000};
#endif

/** @} */ /* End of group __K1986VE9xI_System_Private_Defines */

/** @addtogroup __K1986VE9xI_System_Private_Variables K1986VE9xI System Private Variables
  * @{
  */

/*******************************************************************************
*  Clock Definitions
*******************************************************************************/
uint32_t SystemCoreClock = (uint32_t)8000000;   /*!< System Clock Frequency (Core Clock)
                                                 *   default value */

/** @} */ /* End of group __K1986VE9xI_System_Private_Variables */

/** @addtogroup __K1986VE9xI_System_Private_Functions K1986VE9xI System Private Functions
  * @{
  */

/**
  * @brief  Update SystemCoreClock according to Clock Register Values
  * @note   None
  * @param  None
  * @retval None
  */
void SystemCoreClockUpdate(void)
{
    uint32_t cpu_c1_freq, cpu_c2_freq, cpu_c3_freq;
    uint32_t pll_mul;

    /* Compute CPU_CLK frequency */

    /* Determine CPU_C1 frequency */
    if ((MDR_RST_CLK->CPU_CLOCK & (uint32_t)0x00000002) == (uint32_t)0x00000002)
    {
        cpu_c1_freq = HSE_Value;
    }
    else
    {
        cpu_c1_freq = HSI_Value;
    }

    if ((MDR_RST_CLK->CPU_CLOCK & (uint32_t)0x00000001) == (uint32_t)0x00000001)
    {
        cpu_c1_freq /= 2;
    }

    /* Determine CPU_C2 frequency */
    cpu_c2_freq = cpu_c1_freq;

    if ((MDR_RST_CLK->CPU_CLOCK & (uint32_t)0x00000004) == (uint32_t)0x00000004)
    {
        /* Determine CPU PLL output frequency */
        pll_mul = ((MDR_RST_CLK->PLL_CONTROL >> 8) & (uint32_t)0x0F) + 1;
        cpu_c2_freq *= pll_mul;
    }

    /*Select CPU_CLK from HSI, CPU_C3, LSE, LSI cases */
    switch ((MDR_RST_CLK->CPU_CLOCK >> 8) & (uint32_t)0x03)
    {
        case 0 :
            /* HSI */
            SystemCoreClock = HSI_Value;
            break;
        case 1 :
            /* CPU_C3 */
            /* Determine CPU_C3 frequency */
            cpu_c3_freq = cpu_c2_freq / ((MDR_RST_CLK->CPU_CLOCK >> 4 & (uint32_t)0x0F) + 1);
            SystemCoreClock = cpu_c3_freq;
            break;
        case 2 :
            /* LSE */
            SystemCoreClock = LSE_Value;
            break;
        default : /* case 3 */
            /* LSI */
            SystemCoreClock = LSI_Value;
            break;
    }
}

/**
  * @brief  Setup the microcontroller system RST clock configuration
  *         to the default reset state. Setup SystemCoreClock variable.
  * @note   This function should be used only after reset.
  * @param  None
  * @retval None
  */
void SystemInit(void)
{
#if defined(FACTORY_DATA_SYSTEM_INIT) && (FACTORY_DATA_SYSTEM_INIT == 1)
    uint32_t i;
#endif
    /* Configure the Vector Table location address */
    SCB->VTOR = (uint32_t) __VECTOR_TABLE_ADDRESS;

    /* Reset the RST clock configuration to the default reset state */

    /* Reset all clock but RST_CLK bit */
    MDR_RST_CLK->PER_CLOCK     = (uint32_t)0x00000010;

    /* Reset CPU_CLOCK bits */
    MDR_RST_CLK->CPU_CLOCK     = (uint32_t)0x00000000;

    /* Reset PLL_CONTROL bits */
    MDR_RST_CLK->PLL_CONTROL   = (uint32_t)0x00000000;

    /* Reset HSEON and HSEBYP bits */
    MDR_RST_CLK->HS_CONTROL    = (uint32_t)0x00000000;

    /* Reset USB_CLOCK bits */
    MDR_RST_CLK->USB_CLOCK     = (uint32_t)0x00000000;

    /* Reset ADC_MCO_CLOCK bits */
    MDR_RST_CLK->ADC_MCO_CLOCK = (uint32_t)0x00000000;

    /* Reset RTC_CLOCK bits */
    MDR_RST_CLK->RTC_CLOCK     = (uint32_t)0x00000000;

    /* Reset CAN_CLOCK bits */
    MDR_RST_CLK->CAN_CLOCK     = (uint32_t)0x00000000;

    /* Reset TIM_CLOCK bits */
    MDR_RST_CLK->TIM_CLOCK     = (uint32_t)0x00000000;

    /* Reset UART_CLOCK bits */
    MDR_RST_CLK->UART_CLOCK    = (uint32_t)0x00000000;

    /* Reset SSP_CLOCK bits */
    MDR_RST_CLK->SSP_CLOCK     = (uint32_t)0x00000000;

#if defined(FACTORY_DATA_SYSTEM_INIT) && (FACTORY_DATA_SYSTEM_INIT == 1)
    MDR_RST_CLK->PER_CLOCK = RST_CLK_PER_CLOCK_PCLK_EN_EEPROM | RST_CLK_PER_CLOCK_PCLK_EN_RST_CLK | RST_CLK_PER_CLOCK_PCLK_EN_BKP;

    volatile uint32_t *targetAddr = (volatile uint32_t *)EEPROM_FACTORY_READ_WORD_FUNC_ADDR;
    for (i = 0; i < EEPROM_FACTORY_READ_WORD_FUNC_SIZE; i++) {
        *(targetAddr + i) = EEPROM_FACTORY_READ_WORD_FUNC[i];
    }

    uintptr_t addr = EEPROM_FACTORY_READ_WORD_FUNC_ADDR + 1;
    uint32_t (*func_ptr)(uint32_t) = (uint32_t (*)(uint32_t))addr;

    MDR_EEPROM->KEY = 0x8AAA5551;

    uint32_t tmpreg = MDR_BKP->REG_0E & ~BKP_REG_0E_TRIM_Msk;
    /* Set the Trim bits according to factory data value. */
    tmpreg |= ((func_ptr(EEPROM_FACTORY_DATA_LDO_TRIM) << BKP_REG_0E_TRIM_Pos) & BKP_REG_0E_TRIM_Msk);
    /* Store the new value. */
    MDR_BKP->REG_0E = tmpreg;

    tmpreg = MDR_BKP->REG_0F & ~(BKP_REG_0F_HSI_TRIM_Msk | BKP_REG_0F_LSI_TRIM_Msk);
    /* Set the HSI_TRIM and LSI_TRIM bits according to factory data values. */
    tmpreg |= ((func_ptr(EEPROM_FACTORY_DATA_HSI_TRIM) << BKP_REG_0F_HSI_TRIM_Pos) & BKP_REG_0F_HSI_TRIM_Msk);
    tmpreg |= ((func_ptr(EEPROM_FACTORY_DATA_LSI_TRIM) << BKP_REG_0F_LSI_TRIM_Pos) & BKP_REG_0F_LSI_TRIM_Msk);
    /* Store the new value. */
    MDR_BKP->REG_0F = tmpreg;

    /* Restore EEPROM registers to default state. */
    MDR_EEPROM->ADR = 0;
    MDR_EEPROM->KEY = 0;

    /* Reset all clock but RST_CLK bit. */
    MDR_RST_CLK->PER_CLOCK = RST_CLK_PER_CLOCK_PCLK_EN_RST_CLK;
#endif

    SystemCoreClockUpdate();
}

/** @} */ /* End of group __K1986VE9xI_System_Private_Functions */

/** @} */ /* End of group K1986VE9xI_System */

/** @} */ /* End of group K1986VE9xI */

/** @} */ /* End of group __CMSIS */

/*
*
* END OF FILE system_K1986VE9xI.c */

