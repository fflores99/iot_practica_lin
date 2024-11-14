/*
 * lin1d3_driver.h
 *
 *  Created on: Sep 14, 2018
 *      Author: Nico
 */

#ifndef LIN1D3_DRIVER_H_
#define LIN1D3_DRIVER_H_
/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "timers.h"

/* Freescale includes. */
#include "board.h"

#include "fsl_uart_freertos.h"
#include "fsl_uart.h"

#include "lin1d3_driver_Cfg.h"
/*
 * Enumeration for the type of supported nodes
 * */
typedef enum {
	lin1d3_master_nodeType,
	lin1d3_slave_nodeType,
	lin1d3_max_nodeType
}lin1d3_nodeType_t;

#define message_size_2_bytes_d (0x01)
#define message_size_4_bytes_d (0x02)
#define message_size_8_bytes_d (0x03)
/*
 * Function pointer type for the message handlers
 * */
typedef void (*lin1d3_messageHandler_t)(void*);

/*
 * Structure to link message IDs with message handlers
 * */
typedef struct {
	uint8_t ID; // ID of the message
	uint8_t rx;  // set to 1 if you are just interested on the response
	lin1d3_messageHandler_t handler; // Message handler function
}lin1d3_messageConfig_t;

/*
 * Structure for the node Configuration set by the user.
 * for now it just supports up to 10 messages and the memory is statically allocated
 * TODO: change the messageTable table to be dynamically allocated
 * */
typedef struct {
	lin1d3_nodeType_t type; /* type of LIN node */
	UART_Type *uartBase;    /* UART base address */
	uart_rtos_handle_t*	uart_rtos_handle;
	uint8_t skip_uart_init;
	uint32_t srcclk;		/* UART Clock */
	uint32_t bitrate;		/* LIN bitrate to set */
	lin1d3_messageConfig_t messageTable[lin1d3_max_supported_messages_per_node_cfg_d]; /* Table of supported IDs with its callbacks */
}lin1d3_nodeConfig_t;

/*
 * Handle structure for the LIN node, this structure allows having more than one
 * instance of the driver running at the same time
 * */
typedef struct {
	lin1d3_nodeConfig_t	config;
	uart_rtos_handle_t*	uart_rtos_handle;
	uint8_t skip_uart_init;
	struct _uart_handle* uart_handle;
	TaskHandle_t 		task_handle;
	uart_rtos_config_t 	uart_config;
	QueueHandle_t		node_queue;
}lin1d3_handle_t;

extern lin1d3_handle_t* lin1d3_InitNode(lin1d3_nodeConfig_t config);
extern uint32_t lin1d3_masterSendMessage(lin1d3_handle_t* handle, uint8_t ID);

#endif /* LIN1D3_DRIVER_H_ */
