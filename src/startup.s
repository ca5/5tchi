.syntax unified
.cpu cortex-m3
.thumb

.global g_pfnVectors
.global Reset_Handler

.section .isr_vector, "a", %progbits
.type g_pfnVectors, %object
g_pfnVectors:
    .word _estack          /* 0x00: Initial SP */
    .word Reset_Handler    /* 0x04: Reset */
    .word Default_Handler  /* 0x08: NMI */
    .word Default_Handler  /* 0x0C: HardFault */
    .word Default_Handler  /* 0x10: MemManage */
    .word Default_Handler  /* 0x14: BusFault */
    .word Default_Handler  /* 0x18: UsageFault */
    .word 0                /* 0x1C: Reserved */
    .word 0                /* 0x20: Reserved */
    .word 0                /* 0x24: Reserved */
    .word 0                /* 0x28: Reserved */
    .word Default_Handler  /* 0x2C: SVCall */
    .word Default_Handler  /* 0x30: DebugMonitor */
    .word 0                /* 0x34: Reserved */
    .word Default_Handler  /* 0x38: PendSV */
    .word SysTick_Handler  /* 0x3C: SysTick */

.section .text.Reset_Handler
.weak Reset_Handler
.type Reset_Handler, %function
Reset_Handler:
    ldr r0, =_sdata
    ldr r1, =_edata
    ldr r2, =_sidata
copy_data_loop:
    cmp r0, r1
    ittt lt
    ldrlt r3, [r2], #4
    strlt r3, [r0], #4
    blt copy_data_loop

    ldr r0, =_sbss
    ldr r1, =_ebss
    movs r2, #0
zero_bss_loop:
    cmp r0, r1
    itt lt
    strlt r2, [r0], #4
    blt zero_bss_loop

    bl main

loop_forever:
    b loop_forever

.section .text.Default_Handler, "ax", %progbits
Default_Handler:
    b Default_Handler

.weak SysTick_Handler
.thumb_set SysTick_Handler, Default_Handler
