/*
 * Copyright (c) 2015, Freescale Semiconductor, Inc.
 * Copyright 2016-2017 NXP
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes. */
#include "fsl_device_registers.h"
#include "fsl_debug_console.h"
#include "board.h"

#include "fsl_uart_freertos.h"
#include "fsl_uart.h"

#include "pin_mux.h"
#include "clock_config.h"
/*******************************************************************************
 * Definitions
 ******************************************************************************/
#include "FreeRTOSConfig.h"
/* UART instance and clock */
#define TXBREAK_UART UART3
#define TXBREAK_UART_CLKSRC UART3_CLK_SRC
#define TXBREAK_UART_CLK_FREQ CLOCK_GetFreq(UART3_CLK_SRC)
#define TXBREAK_UART_RX_TX_IRQn UART3_RX_TX_IRQn

#define RXBREAK_UART UART4
#define RXBREAK_UART_CLKSRC UART4_CLK_SRC
#define RXBREAK_UART_CLK_FREQ CLOCK_GetFreq(UART4_CLK_SRC)
#define RXBREAK_UART_RX_TX_IRQn UART4_RX_TX_IRQn

/* Task priorities. */
#define uart_task_PRIORITY (configMAX_PRIORITIES - 1)
/*******************************************************************************
 * Prototypes
 ******************************************************************************/
static void tx_break_uart_task(void *pvParameters);
static void rx_break_uart_task(void *pvParameters);

/*******************************************************************************
 * Variables
 ******************************************************************************/

/*******************************************************************************
 * Code
 ******************************************************************************/
/*!
 * @brief Application entry point.
 */
int main(void)
{
    /* Init board hardware. */
    BOARD_InitPins();
    BOARD_BootClockRUN();
    BOARD_InitDebugConsole();
    NVIC_SetPriority(TXBREAK_UART_RX_TX_IRQn, 5);
    NVIC_SetPriority(RXBREAK_UART_RX_TX_IRQn, 5);
    /* Create tx break task */
    if (xTaskCreate(tx_break_uart_task, "txbreak_task", configMINIMAL_STACK_SIZE * 2, NULL, uart_task_PRIORITY, NULL) != pdPASS) {
        PRINTF("- txbreak_task, Task creation failed!.\r\n");
        while (1);
    }
    /* Create rx break task */
    if (xTaskCreate(rx_break_uart_task, "rxbreak_task", configMINIMAL_STACK_SIZE * 2, NULL, uart_task_PRIORITY, NULL) != pdPASS) {
        PRINTF("- rxbreak_task, Task creation failed!.\r\n");
        while (1);
    }
    PRINTF(" *** Uart Break demo ***\r\n");
    vTaskStartScheduler();
    for (;;);
}

/*!
 * @brief Task responsible for loopback.
 */
static void tx_break_uart_task(void *pvParameters)
{
    int error;
    size_t n = 0;
    uint8_t background_buffer[32];
    uint8_t recv_buffer[3];
    const char * tx_message = "tx";
    uart_rtos_handle_t handle;
    struct _uart_handle t_handle;

    uart_rtos_config_t uart_config = {
        .baudrate    = 9600,
        .parity      = kUART_ParityDisabled,
        .stopbits    = kUART_OneStopBit,
        .buffer      = background_buffer,
        .buffer_size = sizeof(background_buffer),
    };

    uart_config.srcclk = TXBREAK_UART_CLK_FREQ;
    uart_config.base   = TXBREAK_UART;

    /* Init UART */
    if (0 > UART_RTOS_Init(&handle, &t_handle, &uart_config)) {
        vTaskSuspend(NULL);
    }
    /* Make a pause here so the other task can be ready to wait for messages */
    vTaskDelay(100);

    /* Configure 13bit break transmission */
    handle.base->S2 |= (1<<2);

    /* Start the Marko-Polo loop */
    do {
        /* Send the break signal */
        handle.base->C2 |= 0x01;
        handle.base->C2 &= 0xFE;
        vTaskDelay(1);// make a small pause to prevent issues due to Rx slow break detection
        /* Send tx message. */
        if (0 > UART_RTOS_Send(&handle, (uint8_t *)tx_message, strlen(tx_message))) {
            vTaskSuspend(NULL);
        }

        memset(recv_buffer, 0, sizeof(recv_buffer));
        error = UART_RTOS_Receive(&handle, recv_buffer, sizeof(recv_buffer)-1, &n);
        if (error == kStatus_UART_RxHardwareOverrun) {
            /* Notify about hardware buffer overrun */
        	PRINTF("- txbreak_task, hardware buffer overrun.\r\n");
        }
        if (error == kStatus_UART_RxRingBufferOverrun) {
            /* Notify about ring buffer overrun */
        	PRINTF("- txbreak_task, ring buffer overrun.\r\n");
        }
        if (n > 0) {
        	PRINTF("- txbreak_task, received: %s.\r\n", recv_buffer);
        }
        vTaskDelay(200);
    } while (kStatus_Success == error);

    UART_RTOS_Deinit(&handle);
    vTaskSuspend(NULL);
}

/*!
 * @brief Task responsible for loopback.
 */
static void rx_break_uart_task(void *pvParameters)
{
    int error;
    size_t n = 0;
    uint8_t background_buffer[32];
    uint8_t recv_buffer[3];
    const char * rx_message = "rx";
    uart_rtos_handle_t handle;
    struct _uart_handle t_handle;

    uart_rtos_config_t uart_config = {
        .baudrate    = 9600,
        .parity      = kUART_ParityDisabled,
        .stopbits    = kUART_OneStopBit,
        .buffer      = background_buffer,
        .buffer_size = sizeof(background_buffer),
    };

    uart_config.srcclk = RXBREAK_UART_CLK_FREQ;
    uart_config.base   = RXBREAK_UART;

    /* Init UART */
    if (0 > UART_RTOS_Init(&handle, &t_handle, &uart_config)) {
        vTaskSuspend(NULL);
    }

    /* Receive user input and send it back to terminal. */
    do {
    	/* Wait for break */
    	DisableIRQ(RXBREAK_UART_RX_TX_IRQn); //Disable RX interrupt so the break won't mess with the UART_RTOS driver
    	handle.base->S2 |= 0x01<<7; //Clear the LIN Break Detect Interrupt Flag
    	handle.base->S2 |= 0x01<<1; //Enable LIN Break Detection
    	while((handle.base->S2 &  0x01<<7) == 0x00) vTaskDelay(1); //Wait for the flag to be set
    	handle.base->S2 &= ~(0x01<<1); //Disable LIN Break Detection
    	handle.base->S2 |= 0x01<<7; //Clear the LIN Break Detect Interrupt Flag
    	EnableIRQ(RXBREAK_UART_RX_TX_IRQn); //Enable RX interrupt so the UART_RTOS driver works again
    	/* Wait for the tx message */
        memset(recv_buffer, 0, sizeof(recv_buffer));
        error = UART_RTOS_Receive(&handle, recv_buffer, sizeof(recv_buffer)-1, &n);
        if (error == kStatus_UART_RxHardwareOverrun) {
            /* Notify about hardware buffer overrun */
        	PRINTF("- rxbreak_task, hardware buffer overrun.\r\n");
        }
        if (error == kStatus_UART_RxRingBufferOverrun) {
            /* Notify about ring buffer overrun */
        	PRINTF("- rxbreak_task, ring buffer overrun.\r\n");
        }
        if (n > 0) {
        	PRINTF("- rxbreak_task, received: %s.\r\n", recv_buffer);
        }

        /* Send rx message. */
        if (0 > UART_RTOS_Send(&handle, (uint8_t *)rx_message, strlen(rx_message))) {
            vTaskSuspend(NULL);
        }
        vTaskDelay(200);
    } while (kStatus_Success == error);

    UART_RTOS_Deinit(&handle);
    vTaskSuspend(NULL);
}

