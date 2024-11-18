/*
 * lin1d3_driver_Cfg.h
 *
 *  Created on: Sep 14, 2018
 *      Author: Nico
 */

#ifndef LIN1D3_DRIVER_CFG_H_
#define LIN1D3_DRIVER_CFG_H_

#define lin1d3_max_supported_messages_per_node_cfg_d	(16)

#define MASTER_UART_RXBREAK_IRQn      UART3_RX_TX_IRQn
#define LOCAL_SLAVE_UART_RXBREAK_IRQn UART3_RX_TX_IRQn
#define SLAVE_UART_RXBREAK_IRQn       UART4_RX_TX_IRQn

#endif /* LIN1D3_DRIVER_CFG_H_ */
