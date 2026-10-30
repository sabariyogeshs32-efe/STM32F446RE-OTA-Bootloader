#include "uart3.h"

/* GPIOCEN is bit 2 for Port C */
#define GPIOCEN 	(1U<<2)
#define USART3EN	(1U<<18)
#define SYS_FREQ	16000000U
#define APB1_clk	(SYS_FREQ)
#define CR1_TE		(1U<<3)
#define CR1_RE		(1U<<2)
#define CR1_UE		(1U<<13)
#define USART_BR	115200U

void uart3_set_Baud_Rate(USART_TypeDef *USARTx, uint32_t periph_clk, uint32_t Baud_Rate);
static uint16_t USART_bd(uint32_t periph_clk, uint32_t Baud_Rate);

int __io_putchar(int ch)
{
    uart3_write(ch);
    return ch;
}

void uart3_rxtx_init(void)
{
    /* 1. Enable Clock to USART3 in APB1 BUS */
    RCC->APB1ENR |= USART3EN;

    /* 2. Enable Clock for GPIOC (USART3 uses PC10 and PC11) */
    RCC->AHB1ENR |= GPIOCEN;

    /* 3. Configure PC10 (TX) and PC11 (RX) for Alternate Function Mode (AFM) */
    /* Clear MODER bits for Pin 10 and Pin 11 */
    GPIOC->MODER &= ~((3U<<20) | (3U<<22));
    /* Set MODER to 10 (Alternate Function) for Pin 10 and Pin 11 */
    GPIOC->MODER |= ((2U<<20) | (2U<<22));

    /* 4. Set Alternate Function to AF7 (USART3) for PC10 and PC11 */
    /* Pins 8-15 use AFR[1] (AFRH), not AFR[0] (AFRL) */
    GPIOC->AFR[1] &= ~((0xFU<<8) | (0xFU<<12));
    GPIOC->AFR[1] |= ((7U<<8) | (7U<<12));

    /* Enable Pull-Up on PC11 (RX) to prevent floating line noise / glitches */
    GPIOC->PUPDR &= ~(3U << 22);
    GPIOC->PUPDR |=  (1U << 22); // 01 = Pull-Up

    /* 5. Set Data Format (1 Start bit, 8 Data bits, 1 Stop bit) */
    USART3->CR1 &= ~(1U<<12); // M bit: 0 for 8 data bits
    USART3->CR2 &= ~(3U<<12); // STOP bits: 00 for 1 stop bit

    /* Disable Parity Control */
    USART3->CR1 &= ~(1U<<10); // PCE (Parity Control Enable)
    USART3->CR1 &= ~(1U<<9);  // PS (Parity Selection)

    /* 6. Set Baud Rate */
    uart3_set_Baud_Rate(USART3, APB1_clk, USART_BR);

    /* 7. Enable Transmitter and Receiver */
    USART3->CR1 |= (CR1_TE | CR1_RE);

    /* 8. Enable USART3 */
    USART3->CR1 |= CR1_UE;
}

void uart3_tx_init(void)
{
    /* 1. Enable Clock to USART3 in APB1 BUS */
    RCC->APB1ENR |= USART3EN;

    /* 2. Enable Clock for GPIOC */
    RCC->AHB1ENR |= GPIOCEN;

    /* 3. Configure ONLY PC10 (TX) for Alternate Function Mode */
    GPIOC->MODER &= ~(3U<<20);
    GPIOC->MODER |= (2U<<20);

    GPIOC->AFR[1] &= ~(0xFU<<8);
    GPIOC->AFR[1] |= (7U<<8);

    /* 4. Configure Data Format */
    USART3->CR1 &= ~(1U<<12);
    USART3->CR2 &= ~(3U<<12);
    USART3->CR1 &= ~(1U<<10);
    USART3->CR1 &= ~(1U<<9);

    /* 5. Set Baud Rate */
    uart3_set_Baud_Rate(USART3, APB1_clk, USART_BR);

    /* 6. Enable Transmitter */
    USART3->CR1 |= CR1_TE;

    /* 7. Enable USART3 */
    USART3->CR1 |= CR1_UE;
}

void uart3_interrupt_handler_init(void)
{
    /* 1. Standard RX/TX Hardware Init */
    uart3_rxtx_init(); // Reuse the robust init function to prevent duplicating code

    /* 2. Enable the RXNEIE Interrupt in CR1 */
    USART3->CR1 |= RXNEIE;

    /* 3. Enable NVIC Interrupt For USART3 */
    NVIC_EnableIRQ(USART3_IRQn);
}

char uart3_read(void)
{
    /* Wait patiently for Receive Data Register Not Empty (RXNE) */
    while (!(USART3->SR & SR_RXNE))
    {
        /* Clear Overrun Error (ORE) flag if set so it doesn't freeze the bus */
        if (USART3->SR & (1U << 3))
        {
            (void)USART3->SR;
            (void)USART3->DR;
        }
    }

    return (char)(USART3->DR & 0xFF);
}

void uart3_write(int ch)
{
    /* Wait for Transmit Data Register Empty (TXE) before writing */
    while (!(USART3->SR & SR_TXE)) {}

    USART3->DR = (ch & 0xFF);

    /* Wait for Transmission Complete (TC) */
    while (!(USART3->SR & (1U << 6))) {}
}

void uart3_set_Baud_Rate(USART_TypeDef *USARTx, uint32_t periph_clk, uint32_t Baud_Rate)
{
    USARTx->BRR = USART_bd(periph_clk, Baud_Rate);
}

static uint16_t USART_bd(uint32_t periph_clk, uint32_t Baud_Rate)
{
    return ((periph_clk + (Baud_Rate/2)) / Baud_Rate);
}
