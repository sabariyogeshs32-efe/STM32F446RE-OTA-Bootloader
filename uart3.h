/*
 * uart3.h
 *
 *  Created on: Feb 28, 2026
 *      Author: sabari yogesh
 */

#ifndef UART3_H_
#define UART3_H_
#include <stdint.h>

void uart3_rxtx_init (void);
void uart3_tx_init (void);
char uart3_read(void);
void uart3_interrupt_handler_init(void);
#define SR_RXNE			(1U<<5)
#define SR_TXE			(1U<<7)
void uart3_write(int ch);
#define TXEIE			(1U<<7)
#define RXNEIE			(1U<<5)

/* Firmware modular Driver for USART3 */
#include "stm32f446xx.h"

#endif /* UART3_H_ */
