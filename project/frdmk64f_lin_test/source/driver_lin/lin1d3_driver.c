/*
 * lin1d3_driver.c
 *
 *  Created on: Sep 14, 2018
 *      Author: Nico
 */
#include "lin1d3_driver.h"
#include <string.h>
#include <fsl_debug_console.h>
#include <stdio.h>
//#include <assert.h>

#define master_stack_size_d	(256)
#define master_task_priority (configMAX_PRIORITIES - 1)
#define master_queue_size_d	(8)

#define slave_stack_size_d	(256)
#define slave_task_priority (configMAX_PRIORITIES - 1)

#define size_of_uart_buffer	(10)

#define size_of_lin_header_d (2)

/*Static function prototypes */
static void master_task(void *pvParameters);
static void slave_task(void *pvParameters);



/******************************************************************************
 * Public functions
 *
 *****************************************************************************/

/*
 * Init a LIN node
 * */
lin1d3_handle_t* lin1d3_InitNode(lin1d3_nodeConfig_t config)
{
	lin1d3_handle_t* handle = NULL;
	static uint8_t node_idx = 0;
	char master_task_name[] = "linMaster0";
	char slave_task_name[] = "linSlave0";
	/* Check init parameters */
	if(config.type >= lin1d3_max_nodeType) {
		return NULL;
	}

	/* Create the handle structure and */
	handle = (lin1d3_handle_t*)pvPortMalloc(sizeof(lin1d3_handle_t));
	if(handle ==  NULL) {
		/* Failed to allocate memory for the node handle */
		return NULL;
	}
	/* Init the handle structure with 0s */
	memset(handle, 0, sizeof(lin1d3_handle_t));
	/* Copy the config */
	memcpy(&(handle->config), &config, sizeof(lin1d3_nodeConfig_t));

	/* Init/Configure the UART */
	handle->uart_config.base = handle->config.uartBase;
	handle->uart_config.srcclk = handle->config.srcclk;
	handle->uart_config.baudrate = handle->config.bitrate;
	handle->uart_config.parity = kUART_ParityDisabled;
	handle->uart_config.stopbits = kUART_OneStopBit;
	handle->uart_config.buffer = pvPortMalloc(size_of_uart_buffer);
	handle->uart_config.buffer_size = size_of_uart_buffer;
	if(handle->uart_config.buffer == NULL){
		return NULL;
	}

	if(config.skip_uart_init == 0) {
		/* Create the handle UART handle structures */
		handle->uart_rtos_handle = (uart_rtos_handle_t*)pvPortMalloc(sizeof(uart_rtos_handle_t));
		if(handle->uart_rtos_handle ==  NULL) {
			/* Failed to allocate memory for the node handle */
			return NULL;
		}

		handle->uart_handle = (uart_handle_t*)pvPortMalloc(sizeof(uart_handle_t));
		if(handle->uart_handle ==  NULL) {
			/* Failed to allocate memory for the node handle */
			return NULL;
		}

		if (0 > UART_RTOS_Init(handle->uart_rtos_handle, handle->uart_handle, &(handle->uart_config)))
		{
			return NULL;
		}
	}
	else {
		handle->uart_rtos_handle = config.uart_rtos_handle;
	}
	/* Create the Node Task */
	if(lin1d3_master_nodeType == config.type) {
		/* Create a queue for User message requests */
		handle->node_queue = xQueueCreate( master_queue_size_d, sizeof(uint8_t));
		if(handle->node_queue == NULL){
			vPortFree(handle);
			return NULL;
		}
		/* Create a task for the node */
		master_task_name[strlen(master_task_name)-1] += node_idx++;
		if (xTaskCreate(master_task, master_task_name, master_stack_size_d, handle, master_task_priority, &(handle->task_handle)) != pdPASS) {
			vPortFree(handle);
			return NULL;
		}
	}
	else if(lin1d3_slave_nodeType == config.type) {
		/* Create a task for the node */
		slave_task_name[strlen(slave_task_name)-1] += node_idx++;
		if (xTaskCreate(slave_task, slave_task_name, slave_stack_size_d, handle, slave_task_priority, &(handle->task_handle)) != pdPASS) {
			vPortFree(handle);
			return NULL;
		}
	}

	return handle;
}

/*
 * Send a message frame from a LIN Master node
 * */
uint32_t lin1d3_masterSendMessage(lin1d3_handle_t* handle, uint8_t ID)
{
	if(handle !=  NULL) {
		/* Put the requested ID on the master queue */
		xQueueSend( handle->node_queue, &ID, ( TickType_t ) 0 );
	}
	return 0;
}

/******************************************************************************
 * Static functions
 *
 *****************************************************************************/
static void master_send_break(uart_rtos_handle_t *handle)
{
    /* Send the break signal */
    /* Enable break */
    handle->base->C2 |= UART_C2_SBK_MASK;
    /* Disable break */
    handle->base->C2 &= ~UART_C2_SBK_MASK;
    vTaskDelay(1);// make a small pause to prevent issues due to Rx slow break detection
}

uint8_t GenerateChecksum(uint8_t* message, uint8_t messageSize)
{
	uint16_t crcProcedure = 0;
	uint8_t  checksum;
	uint8_t  invertedChecksum;

	for (uint8_t i = 0; i < messageSize-1; i++)
	{
		crcProcedure += message[i];

		if ((crcProcedure & (0x100)) == 0x100)
		{
			crcProcedure &= ~(1 << 8);
			crcProcedure += 0x1;
		}
	}

	checksum = crcProcedure;
	invertedChecksum = ~checksum;

	assert ((checksum + invertedChecksum) == 0xFF);

	return invertedChecksum;
}

static void master_task(void *pvParameters)
{
	lin1d3_handle_t* handle = (lin1d3_handle_t*)pvParameters;
	uint8_t  ID;
	uint8_t  P0;
	uint8_t  P1;
	uint8_t  synch_break_byte = 0;
	uint8_t  lin1p3_header[] = {0x55, 0x00};
	uint8_t  lin1p3_message[size_of_uart_buffer];
	uint8_t  message_size = 0;

	if(handle == NULL) {
		vTaskSuspend(NULL);
	}

    while(1) {
    	/* Wait for messages on the Queue */
        if(xQueueReceive(handle->node_queue, &ID, portMAX_DELAY)){
        	/* Build and send the LIN Header */
        	/*Parity bits*/
        	/* Calculate parity bits */
        	P0 =   ((ID & 0x20) >> 5) ^ ((ID & 0x10) >> 4) ^ ((ID & 0x08) >> 3) ^ ((ID & 0x02) >> 1);
        	P1 = ~(((ID & 0x10) >> 4) ^ ((ID & 0x04) >> 2) ^ ((ID & 0x02) >> 1) ^ (ID & 0x01));
        	/* Put the ID into the header */
        	/* TODO: put the parity bits */
        	lin1p3_header[1] = (ID <<2 ) | ((P1 & 0x1) << 1) | (P0 & 0x1);

        	/* Init the message recevie buffer */
        	memset(lin1p3_message, 0, size_of_uart_buffer);
        	/* Calc the message size */
        	switch(ID&0x03) {
        		case 0x00: message_size = 2;
        		break;
        		case 0x01: message_size = 2;
        		break;
        		case 0x02: message_size = 4;
        		break;
        		case 0x03: message_size = 8;
        		break;
        	}
        	message_size+=1;
        	/* TODO Send a Break It is just sending one byte 0, *** CHANGE THIS WITH A REAL SYNCH BREAK ****/
        	master_send_break(handle->uart_rtos_handle);
//        	UART_RTOS_Send(handle->uart_rtos_handle, (uint8_t *)&synch_break_byte, 1);
//        	vTaskDelay(1);
        	/* Send the header */
        	UART_RTOS_Send(handle->uart_rtos_handle, (uint8_t *)lin1p3_header, size_of_lin_header_d);
        	vTaskDelay(1);
        }
    }
}

static void slave_wait_for_break(uart_rtos_handle_t *handle)
{
	DisableIRQ (MASTER_UART_RXBREAK_IRQn);
	DisableIRQ (SLAVE_UART_RXBREAK_IRQn); //Disable RX interrupt so the break won't mess with the UART_RTOS driver
	handle->base->S2 |= 0x01<<7; //Clear the LIN Break Detect Interrupt Flag
	handle->base->S2 |= 0x01<<1; //Enable LIN Break Detection
	while((handle->base->S2 &  0x01<<7) == 0x00) vTaskDelay(1); //Wait for the flag to be set
	handle->base->S2 &= ~(0x01<<1); //Disable LIN Break Detection
	handle->base->S2 |= 0x01<<7; //Clear the LIN Break Detect Interrupt Flag
	EnableIRQ (MASTER_UART_RXBREAK_IRQn);
	EnableIRQ (SLAVE_UART_RXBREAK_IRQn); //Enable RX interrupt so the UART_RTOS driver works again
}

static void slave_task(void *pvParameters)
{
	lin1d3_handle_t* handle = (lin1d3_handle_t*)pvParameters;
	uint8_t  ID;
	uint8_t  P0;
	uint8_t  P1;
	uint8_t  lin1p3_header[size_of_lin_header_d];
	uint8_t  lin1p3_message[size_of_uart_buffer];
	uint8_t  message_size = 0;
	uint8_t  checksum = 0;
	size_t n;
	uint8_t  msg_idx;
	uint8_t synch_break_byte = 0;

	if(handle == NULL) {
		vTaskSuspend(NULL);
	}

    while(1) {
    	/* Init the message header buffer */
    	memset(lin1p3_header, 0, size_of_lin_header_d);
    	/* Wait for a synch break This code is just waiting for one byte 0, *** CHANGE THIS WITH A REAL SYNCH BREAK ****/

    	/*TODO Add sync break wait*/
    	/* Wait for break */
//    	slave_wait_for_break(handle->uart_rtos_handle);
    	UART_RTOS_WaitForSyncBreak(handle->uart_rtos_handle);

//    	UART_RTOS_Receive(handle->uart_rtos_handle, &synch_break_byte, 1, &n);

    	/* Wait for header on the UART */
    	UART_RTOS_Receive(handle->uart_rtos_handle, lin1p3_header, size_of_lin_header_d, &n);
    	/* Check header */
    	if(/*(lin1p3_header[0] != 0x00) &&*/
    	   (lin1p3_header[0] != 0x55)) {
    		/* TODO: Check ID parity bits */
    		/* Header is not correct we are ignoring the header */
    		continue;
    	}
    	/* Get the message ID */
    	ID = (lin1p3_header[1] & 0xFC)>>2;

    	P0 =   ((ID & 0x20) >> 5) ^ ((ID & 0x10) >> 4) ^ ((ID & 0x08) >> 3) ^ ((ID & 0x02) >> 1);
    	P1 = ~(((ID & 0x10) >> 4) ^ ((ID & 0x04) >> 2) ^ ((ID & 0x02) >> 1) ^ (ID & 0x01));

    	/* Check parity bits */
    	if ( ( (P1 & 0x1) != (lin1p3_header[1] & 0x1)) || ( (P0 & 0x1) != ((lin1p3_header[1] >> 1 )& 0x1)))
    	{
    		/* If Parity bits are not correct we are ignoring the header */
    		continue;
    	}

    	/* If the header is correct, check if the message is in the table */
    	msg_idx = 0;
    	/*Look for the ID in the message table */
    	while(msg_idx < lin1d3_max_supported_messages_per_node_cfg_d) {
    		if(handle->config.messageTable[msg_idx].ID == ID) {
    			break;
    		}
    		msg_idx++;
    	}
    	/* If the message ID was not found then ignore it */
    	if(msg_idx == lin1d3_max_supported_messages_per_node_cfg_d) continue;

    	/* Calc the message size */
    	switch(ID&0x03) {
    		case 0x00: message_size = 2;
    		break;
    		case 0x01: message_size = 2;
    		break;
    		case 0x02: message_size = 4;
    		break;
    		case 0x03: message_size = 8;
    		break;
    	}

    	message_size+=1;
    	/* Init the message transmit buffer */
    	memset(lin1p3_message, 0, size_of_uart_buffer);

    	if(handle->config.messageTable[msg_idx].rx == 0) {
        	/*If the message is in the table call the message callback */
    		/* User shall fill the message */
        	handle->config.messageTable[msg_idx].handler((void*)lin1p3_message);
        	/* TODO: Add the checksum to the message */
        	checksum = GenerateChecksum (lin1p3_message, message_size);
        	lin1p3_message [message_size - 1] = checksum;
        	/* Send the message data */
        	UART_RTOS_Send(handle->uart_rtos_handle, (uint8_t *)lin1p3_message, message_size);
    	}
    	else {
        	/* Wait for Response on the UART */
        	UART_RTOS_Receive(handle->uart_rtos_handle, lin1p3_message, message_size, &n);

        	/* TODO: Check the checksum on the message */
        	checksum = GenerateChecksum (lin1p3_message, message_size);
        	checksum = (~checksum) + lin1p3_message[message_size - 1];
        	if (checksum != 0xFF)
        	{
        		/* Set error flag */
        		printf("Checksum Error");
        		continue;
        	}

        	/*If the message is in the table call the message callback */
        	handle->config.messageTable[msg_idx].handler((void*)lin1p3_message);
    	}
    }
}
