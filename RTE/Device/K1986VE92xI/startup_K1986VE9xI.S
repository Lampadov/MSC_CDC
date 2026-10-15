;/**
;  ******************************************************************************
;  * @file    startup_K1986VE9xI.S
;  * @author  Milandr Application Team
;  * @version V2.1
;  * @date    22/07/2024
;  * @brief   CMSIS core device startup file for K1986VE9xI.
;  ******************************************************************************
;  * <br><br>
;  *
;  * THE PRESENT FIRMWARE IS FOR GUIDANCE ONLY. IT AIMS AT PROVIDING CUSTOMERS
;  * WITH CODING INFORMATION REGARDING MILANDR'S PRODUCTS IN ORDER TO FACILITATE
;  * THE USE AND SAVE TIME. MILANDR SHALL NOT BE HELD LIABLE FOR ANY
;  * DIRECT, INDIRECT OR CONSEQUENTIAL DAMAGES RESULTING
;  * FROM THE CONTENT OF SUCH FIRMWARE AND/OR A USE MADE BY CUSTOMERS OF THE
;  * CODING INFORMATION CONTAINED HEREIN IN THEIR PRODUCTS.
;  *
;  * <h2><center>&copy; COPYRIGHT 2025 Milandr</center></h2>
;  ******************************************************************************
;  */

;//--------- <<< Use Configuration Wizard in Context Menu >>> ------------------

;<h> Stack Configuration
;  <o> Stack Size (in Bytes) <0x0-0xFFFFFFFF:8>
;</h>
Stack_Size      EQU     0x00000400

                AREA    STACK, NOINIT, READWRITE, ALIGN=3
__stack_limit
Stack_Mem       SPACE   Stack_Size
__initial_sp


;<h> Heap Configuration
;  <o>  Heap Size (in Bytes) <0x0-0xFFFFFFFF:8>
;</h>

Heap_Size       EQU     0x00000200

                AREA    HEAP, NOINIT, READWRITE, ALIGN=3
__heap_base
Heap_Mem        SPACE   Heap_Size
__heap_limit

                PRESERVE8
                THUMB


; Vector Table Mapped to Address 0 at Reset
                AREA    RESET, DATA, READONLY
                EXPORT  __Vectors
                EXPORT  __Vectors_End
                EXPORT  __Vectors_Size

; Attention for NMI_Handler:
; Called by software only (bit [31] of the ICSR register). Used for mission critical applications
__Vectors       DCD     __initial_sp               ;     Top of Stack
                DCD     Reset_Handler              ;     Reset Handler
                DCD     NMI_Handler                ; -14 NMI Handler
                DCD     HardFault_Handler          ; -13 Hard Fault Handler
                DCD     MemManage_Handler          ; -12 MPU Fault Handler
                DCD     BusFault_Handler           ; -11 Bus Fault Handler
                DCD     UsageFault_Handler         ;  -9 Usage Fault Handler
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     SVC_Handler                ;  -5 SVCall Handler
                DCD     DebugMon_Handler           ;  -4 Debug Monitor Handler
                DCD     0                          ;     Reserved
                DCD     PendSV_Handler             ;  -2 PendSV Handler
                DCD     SysTick_Handler            ;  -1 SysTick Handler

                ; External Interrupts
                DCD     CAN1_IRQHandler            ;   0 CAN1 Handler
                DCD     CAN2_IRQHandler            ;   1 CAN2 Handler
                DCD     USB_IRQHandler             ;   2 USB Host Handler
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     DMA_IRQHandler             ;   5 DMA Handler
                DCD     UART1_IRQHandler           ;   6 UART1 Handler
                DCD     UART2_IRQHandler           ;   7 UART2 Handler
                DCD     SSP1_IRQHandler            ;   8 SSP1 Handler
                DCD     0                          ;     Reserved
                DCD     I2C_IRQHandler             ;  10 I2C Handler
                DCD     POWER_IRQHandler           ;  11 POWER Handler
                DCD     WWDG_IRQHandler            ;  12 WWDG Handler
                DCD     0                          ;     Reserved
                DCD     Timer1_IRQHandler          ;  14 Timer1 Handler
                DCD     Timer2_IRQHandler          ;  15 Timer2 Handler
                DCD     Timer3_IRQHandler          ;  16 Timer3 Handler
                DCD     ADC_IRQHandler             ;  17 ADC Handler
                DCD     0                          ;     Reserved
                DCD     COMPARATOR_IRQHandler      ;  19 Comparator Handler
                DCD     SSP2_IRQHandler            ;  20 SSP2 Handler
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     0                          ;     Reserved
                DCD     BACKUP_IRQHandler          ;  27 BKP Handler
                DCD     EXT_INT1_IRQHandler        ;  28 EXT_INT1 Handler
                DCD     EXT_INT2_IRQHandler        ;  29 EXT_INT2 Handler
                DCD     EXT_INT3_IRQHandler        ;  30 EXT_INT3 Handler
                DCD     EXT_INT4_IRQHandler        ;  31 EXT_INT4 Handler
__Vectors_End

__Vectors_Size  EQU  __Vectors_End - __Vectors

                AREA    |.text|, CODE, READONLY

; Reset handler
Reset_Handler   PROC
                EXPORT  Reset_Handler               [WEAK]
                IMPORT  SystemInit
                IMPORT  __main
                LDR     R0, =SystemInit
                BLX     R0
                LDR     R0, =__main
                BX      R0
                ENDP

; Dummy Exception Handlers (infinite loops which can be modified)

NMI_Handler     PROC
                EXPORT  NMI_Handler                 [WEAK]
                B       .
                ENDP

HardFault_Handler       PROC
                EXPORT  HardFault_Handler           [WEAK]
                B       .
                ENDP

MemManage_Handler       PROC
                EXPORT  MemManage_Handler           [WEAK]
                B       .
                ENDP

BusFault_Handler        PROC
                EXPORT  BusFault_Handler            [WEAK]
                B       .
                ENDP

UsageFault_Handler      PROC
                EXPORT  UsageFault_Handler          [WEAK]
                B       .
                ENDP

SVC_Handler     PROC
                EXPORT  SVC_Handler                 [WEAK]
                B       .
                ENDP

DebugMon_Handler    PROC
                EXPORT  DebugMon_Handler            [WEAK]
                B       .
                ENDP

PendSV_Handler  PROC
                EXPORT  PendSV_Handler              [WEAK]
                B       .
                ENDP

SysTick_Handler PROC
                EXPORT  SysTick_Handler             [WEAK]
                B       .
                ENDP


; External Interrupts

CAN1_IRQHandler PROC
                EXPORT  CAN1_IRQHandler             [WEAK]
                B       .
                ENDP

CAN2_IRQHandler PROC
                EXPORT  CAN2_IRQHandler             [WEAK]
                B       .
                ENDP

USB_IRQHandler  PROC
                EXPORT  USB_IRQHandler              [WEAK]
                B       .
                ENDP

DMA_IRQHandler  PROC
                EXPORT  DMA_IRQHandler              [WEAK]
                B       .
                ENDP

UART1_IRQHandler        PROC
                EXPORT  UART1_IRQHandler            [WEAK]
                B       .
                ENDP

UART2_IRQHandler        PROC
                EXPORT  UART2_IRQHandler            [WEAK]
                B       .
                ENDP

SSP1_IRQHandler PROC
                EXPORT  SSP1_IRQHandler             [WEAK]
                B       .
                ENDP

I2C_IRQHandler  PROC
                EXPORT  I2C_IRQHandler              [WEAK]
                B       .
                ENDP

POWER_IRQHandler        PROC
                EXPORT  POWER_IRQHandler            [WEAK]
                B       .
                ENDP

WWDG_IRQHandler PROC
                EXPORT  WWDG_IRQHandler             [WEAK]
                B       .
                ENDP

Timer1_IRQHandler       PROC
                EXPORT  Timer1_IRQHandler           [WEAK]
                B       .
                ENDP

Timer2_IRQHandler       PROC
                EXPORT  Timer2_IRQHandler           [WEAK]
                B       .
                ENDP

Timer3_IRQHandler       PROC
                EXPORT  Timer3_IRQHandler           [WEAK]
                B       .
                ENDP

ADC_IRQHandler  PROC
                EXPORT  ADC_IRQHandler              [WEAK]
                B       .
                ENDP

COMPARATOR_IRQHandler   PROC
                EXPORT  COMPARATOR_IRQHandler       [WEAK]
                B       .
                ENDP

SSP2_IRQHandler PROC
                EXPORT  SSP2_IRQHandler             [WEAK]
                B       .
                ENDP

BACKUP_IRQHandler       PROC
                EXPORT  BACKUP_IRQHandler           [WEAK]
                B       .
                ENDP

EXT_INT1_IRQHandler     PROC
                EXPORT  EXT_INT1_IRQHandler         [WEAK]
                B       .
                ENDP

EXT_INT2_IRQHandler     PROC
                EXPORT  EXT_INT2_IRQHandler         [WEAK]
                B       .
                ENDP

EXT_INT3_IRQHandler     PROC
                EXPORT  EXT_INT3_IRQHandler         [WEAK]
                B       .
                ENDP

EXT_INT4_IRQHandler     PROC
                EXPORT  EXT_INT4_IRQHandler         [WEAK]
                B       .
                ENDP

                ALIGN



; User Stack and Heap initialization

                IF      :DEF:__MICROLIB

                EXPORT  __initial_sp
                EXPORT  __stack_limit
                EXPORT  __heap_base
                EXPORT  __heap_limit

                ELSE

                IMPORT  __use_two_region_memory
                EXPORT  __user_initial_stackheap
__user_initial_stackheap

                LDR     R0, = Heap_Mem
                LDR     R1, = (Stack_Mem + Stack_Size)
                LDR     R2, = (Heap_Mem +  Heap_Size)
                LDR     R3, = Stack_Mem
                BX      LR

                ALIGN

                ENDIF

                END


