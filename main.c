#include "stm32f446xx.h"
#include <stdbool.h>
#include <stdio.h>
#include <uart3.h>


#define APP_BASE	(0x08008000)

#define FLASH_BSY_SR	(1U<<16)
#define SNB_CR			(0xF<<3)
#define FLASH_STRT_CR	(1U<<16)




void *fun(void);

typedef void (*app_reset_handler)(void);

void crc_Init(void);
void Erase_old_Frmwr(void);
void Program_new_Frmwr(uint32_t *payload_word_ptr, uint8_t seq_Id);
void jumping_seq_fun(void);
void USART2_IRQHandler(void);

uint32_t *current_address = (uint32_t *)APP_BASE;


volatile bool g_meta_data_ready = 0;
int ACK = 0x79;
int NACK = 0x1F;

int main()
{
		//Erase_old_Frmwr();

		RCC->AHB1ENR |= (1U<<0);
		GPIOA->MODER |= (1U<<10);
		GPIOA->MODER &= ~(1U<<11);
//		GPIOA->ODR |= (1U<<5);

		uart3_rxtx_init();
		crc_Init();
		//uart2_interrupt_handler_init();

		uint8_t metadata[4];
		uint16_t total_file_size;
		uint8_t total_expected_packets;

		/*Check for whether new Firmware Update come through WIFI */

		/*If any Updates Came do Erase the old Firmware and Program new ones... */

//		while(g_meta_data_ready == 0)
//		{
//			/*Do Nothing just wait patiently for the MetaDATA to arrive */
//			//printf("Error");
//			//for(int i=0; i<1000000; i++) {}
//			//GPIOA->ODR &= ~(1U<<5);
//
//
//		}
		/* Triple Blink on Boot: Visual confirmation that bootloader has started */
		for (int k = 0; k < 3; k++)
		{
			GPIOA->ODR |= (1U << 5);
			for (volatile uint32_t i = 0; i < 200000; i++) {}
			GPIOA->ODR &= ~(1U << 5);
			for (volatile uint32_t i = 0; i < 200000; i++) {}
		}

		current_address = (uint32_t *)APP_BASE;

		/* Clear any stale data or errors in USART3 from hardware startup */
		while (USART3->SR & (1U << 5))
		{
			(void)USART3->DR;
		}
		if (USART3->SR & (1U << 3))
		{
			(void)USART3->SR;
			(void)USART3->DR;
		}

		uint32_t meta_index = 0;
		int frame_state = 0;
		/* frame_state:
		 * 0 = Waiting for Start of Frame (0x5A)
		 * 1 = Waiting for Command ID (0x01 = Metadata)
		 * 2 = Receiving 4 metadata bytes [flag, size_H, size_L, packets]
		 * 3 = Done! */

		while (frame_state < 3)
		{
		    /* Clear Overrun Error ONLY if receiver is blocked and RXNE is 0 */
		    if ((USART3->SR & (1U << 3)) && !(USART3->SR & (1U << 5)))
		    {
		        (void)USART3->SR;
		        (void)USART3->DR;
		    }

		    /* Wait for a byte to arrive in USART3 */
		    if (USART3->SR & (1U << 5))
		    {
		        uint8_t byte = (uint8_t)(USART3->DR & 0xFF);

		        /* Blink LED on ANY incoming byte: instant proof that PC11 is receiving! */
		        GPIOA->ODR ^= (1U << 5);

		        if (frame_state == 0)
		        {
		            if (byte == 0x5A)
		            {
		                frame_state = 1;
		            }
		        }
		        else if (frame_state == 1)
		        {
		            if (byte == 0x01)
		            {
		                frame_state = 2;
		                meta_index = 0;
		            }
		            else if (byte == 0x5A)
		            {
		                frame_state = 1; // Consecutive 0x5A, stay in state 1
		            }
		            else
		            {
		                frame_state = 0; // Invalid byte, reset to hunting 0x5A
		            }
		        }
		        else if (frame_state == 2)
		        {
		            metadata[meta_index++] = byte;
		            if (meta_index >= 4)
		            {
		                frame_state = 3; // All 4 metadata bytes cleanly captured!
		            }
		        }
		    }
		}

		/* 1. All 4 metadata bytes received! Turn ON LED visibly for 1 second */
		GPIOA->ODR |= (1U << 5);
		for (volatile uint32_t i = 0; i < 2000000; i++) {}

		if (metadata[0] == 0x00)
		{
		    /* No updates available -> Jump to main application */
		    jumping_seq_fun();
		}
		else if (metadata[0] == 0xFF)
		{
		    /* 2. Erase old firmware: LED will turn OFF during erase */
		    Erase_old_Frmwr();

		    total_file_size = ((metadata[1] << 8) | (metadata[2] << 0));
		    total_expected_packets = metadata[3];

		    /* Set PSIZE to 32-bit (x32) */
		    FLASH->CR |= (1U << 9);
		    FLASH->CR &= ~(1U << 8);

		    /* Enable PG Mode */
		    FLASH->CR |= (1U << 0);

		    for (volatile int i = 0; i < 50000; i++) {}

		    /* 3. Turn LED ON: Erase complete, ready for firmware data! */
		    GPIOA->ODR |= (1U << 5);

		    /* 4. Send ACK (0x79) to TTL / ESP8266 */
		    uart3_write(ACK);
		}

		uint8_t total_received_packets = 0;

		/* Receiving the entire .bin file packet by packet */
		while (total_received_packets < total_expected_packets)
		{
			uint8_t uart_buffer[265];
			for (uint32_t i = 0; i < 265; i++)
			{
				uart_buffer[i] = uart3_read();
			}

			/* Blink LED: turn off while processing / writing */
			GPIOA->ODR &= ~(1U << 5);

			uint8_t payload_only[256];
			for (uint32_t i = 0; i < 256; i++)
			{
				payload_only[i] = uart_buffer[5 + i];
			}

			uint32_t *payload_word_ptr = (uint32_t *)payload_only;

			/* Reset CRC Calculation unit on every packet receive */
			CRC->CR |= (1U << 0);

			for (uint32_t i = 0; i < 64; i++)
			{
				CRC->DR = payload_word_ptr[i];
			}

			uint32_t calclted_crc = CRC->DR;
			uint32_t received_crc = ((uart_buffer[261] << 24) | 
			                         (uart_buffer[262] << 16) | 
			                         (uart_buffer[263] << 8)  | 
			                         (uart_buffer[264] << 0));

			if (calclted_crc == received_crc)
			{
				/* Program new firmware chunk into flash */
				Program_new_Frmwr(payload_word_ptr, uart_buffer[2]);

				total_received_packets++;
				GPIOA->ODR |= (1U << 5); // LED ON: Packet accepted & programmed

				/* Send ACK */
				uart3_write(ACK);
			}
			else
			{
				/* Send NACK */
				uart3_write(NACK);
			}
		}

		/* Disable PG Mode and jump to newly programmed application */
		FLASH->CR &= ~(1U << 0);
		jumping_seq_fun();

}




void crc_Init(void)
{

	/*Enabling clock access for CRC peripheral */
	RCC->AHB1ENR |= (1U<<12);

}

void Erase_old_Frmwr(void)
{
	current_address = (uint32_t *)APP_BASE;

	/* Turn OFF LED to visually indicate erase in progress */
	GPIOA->ODR &= ~(1U<<5);
	for(volatile uint32_t i = 0; i < 500000; i++) {}

	/* Clear any previous error flags in FLASH->SR */
	FLASH->SR |= (1U<<0) | (1U<<1) | (1U<<4) | (1U<<5) | (1U<<6) | (1U<<7);

	/* 1. Unlock the CR of Flash using two keys if locked */
	if (FLASH->CR & (1U << 31))
	{
		FLASH->KEYR = 0x45670123;
		FLASH->KEYR = 0xCDEF89AB;
	}

	/* Erase the required sectors */
	uint32_t sector_to_erase[] = {2,3,4,5,6,7};
	uint32_t size_of_sectrs = (sizeof(sector_to_erase) / sizeof(sector_to_erase[0]));

	for(uint32_t i=0; i<size_of_sectrs; i++)
	{
		/* Wait for BSY flag to reset */
		while(FLASH->SR & FLASH_BSY_SR) {}

		/* Clear the previous SNB bits */
		FLASH->CR &= ~(0x1FU << 3);

		/* Enable Sector Erase (SER) mode */
		FLASH->CR |= (1U<<1);

		/* Select sector number */
		FLASH->CR |= (sector_to_erase[i] << 3);

		/* Start the erase */
		FLASH->CR |= FLASH_STRT_CR;

		/* Wait for erase to complete */
		while(FLASH->SR & FLASH_BSY_SR) {}
	}

	/* Disable SER mode */
	FLASH->CR &= ~(1U<<1);
	FLASH->CR &= ~(0x1FU << 3);
}

void Program_new_Frmwr(uint32_t *payload_word_ptr, uint8_t seq_Id)
{

	/*Check and wait for the BSY flag to reset*/
	while(FLASH->SR & (FLASH_BSY_SR)) {}

	/*Cast the pointer to the 32 bit unsigned integer pointer */
	uint32_t *buffer_1_word = payload_word_ptr;



	for(uint32_t i=0; i<64; i++)
	{
		uint32_t current_word = buffer_1_word[i];
		/*Check and wait for the BSY flag to reset*/
		while(FLASH->SR & (FLASH_BSY_SR)) {}

		*(volatile uint32_t *)(current_address) = current_word;



		current_address += 1;
	}


}


void jumping_seq_fun(void)
{
    // 1. Disable all global interrupts first
    __disable_irq();

    // 2. Clear all pending interrupts and disable them in the NVIC
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFF; // Disable interrupts
        NVIC->ICPR[i] = 0xFFFFFFFF; // Clear pending flags
    }

    // 3. Disable Peripherals used in the bootloader (USART3 & SPI1)
    USART3->CR1 &= ~(1U << 13); // Disable USART3 (UE = 0)
    SPI1->CR1   &= ~(1U << 6);  // Disable SPI1 (SPE = 0)

    // 4. Force Reset and disable Clocks via RCC to give the application a clean hardware state
    RCC->APB1RSTR |=  (1U << 18);  // Force reset USART3
    RCC->APB1RSTR &= ~(1U << 18);  // Release reset USART3
    RCC->APB1ENR  &= ~(1U << 18);  // Disable USART3 clock

    RCC->APB2RSTR |=  (1U << 12);  // Force reset SPI1
    RCC->APB2RSTR &= ~(1U << 12);  // Release reset SPI1
    RCC->APB2ENR  &= ~(1U << 12);  // Disable SPI1 clock

    // 5. Disable the SysTick timer
    SysTick->CTRL = 0;
    SysTick->LOAD = 0;
    SysTick->VAL  = 0;

    // 6. Set Vector Table Offset Register
    SCB->VTOR = APP_BASE;

    // 7. Extract MSP and Reset Handler using your exact implementation
    uint32_t read_msp = *(volatile uint32_t *)APP_BASE;

    uint32_t Reset_Handler_addr = *(volatile uint32_t *)(APP_BASE + 4);

    app_reset_handler jump_to_app = (app_reset_handler)Reset_Handler_addr;

    // 8. Set Main Stack Pointer and Fire!
    __set_MSP(read_msp);

    jump_to_app();
}


//void jumping_seq_fun(void)
//{
//
//		__disable_irq();
//
//	// 2. Disable the SysTick timer (crucial for RTOS or HAL tick handoffs)
//		SysTick->CTRL = 0;
//
//		SCB->VTOR = APP_BASE;
//
//		uint32_t read_msp = *(volatile uint32_t *)APP_BASE;
//
//
//
//		uint32_t Reset_Handler_addr = *(volatile uint32_t *)(APP_BASE+4);
//
//		app_reset_handler jump_to_app = (app_reset_handler)Reset_Handler_addr;
//
//
//		__set_MSP(read_msp);
//
//		jump_to_app();
//}
//
//void USART2_IRQHandler(void)
//{
//		/*Check if RXNE is set */
//		if(USART2->SR & SR_RXNE)
//		{
//			/*Do Something...*/
//			g_meta_data_ready = 1;
//
//			GPIOA->ODR |= (1U<<5);
//
//			/* 1.Enable the RXNEIE Interrupt Enable bit in CR1 */
//			USART2->CR1 &= ~RXNEIE;
//
//
//		}
//}
//


/*Bugs
 * 1.volatile keyword for bool  g_meta_data_ready
 * 2.crc reset after every packet crc calculation
 * 3.isr flag reset by reading DR or diable by writing 0 to it
 * 4.total expected packet was det to uint8_t but above 255 it cant hold so increase if you want to apply this project for real world application
 * */










