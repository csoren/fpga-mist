 /*
Copyright 2008, 2009 Jakub Bednarski

This file is part of Minimig

Minimig is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 3 of the License, or
(at your option) any later version.

Minimig is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "AT91SAM7S256.h"
#include "stdio.h"
#include "hardware.h"
#include "user_io.h"

void __init_hardware(void)
{
    *AT91C_WDTC_WDMR = AT91C_WDTC_WDDIS; // disable watchdog
    *AT91C_RSTC_RMR = (0xA5 << 24) | AT91C_RSTC_URSTEN;   // enable external user reset input
    *AT91C_MC_FMR = FWS << 8; // Flash wait states

    // configure clock generator
    *AT91C_CKGR_MOR = AT91C_CKGR_MOSCEN | (40 << 8);  
    while (!(*AT91C_PMC_SR & AT91C_PMC_MOSCS));

    *AT91C_CKGR_PLLR = AT91C_CKGR_OUT_0 | AT91C_CKGR_USBDIV_1 | (25 << 16) | (40 << 8) | 5; // DIV=5 MUL=26 USBDIV=1 (2) PLLCOUNT=40
    while (!(*AT91C_PMC_SR & AT91C_PMC_LOCK));

    *AT91C_PMC_MCKR = AT91C_PMC_PRES_CLK_2; // master clock register: clock source selection
    while (!(*AT91C_PMC_SR & AT91C_PMC_MCKRDY));

    *AT91C_PMC_MCKR = AT91C_PMC_CSS_PLL_CLK | AT91C_PMC_PRES_CLK_2; // master clock register: clock source selection
    while (!(*AT91C_PMC_SR & AT91C_PMC_MCKRDY));

    *AT91C_PIOA_PER = 0xFFFFFFFF; // enable pio on all pins
    *AT91C_PIOA_SODR = DISKLED;   // led off

#ifdef USB_PUP
    // disable usb d+/d- pullups if present
    *AT91C_PIOA_OER = USB_PUP;
    *AT91C_PIOA_PPUDR = USB_PUP;
    *AT91C_PIOA_SODR = USB_PUP;
#endif

    // enable joystick ports
#ifdef JOY0
    *AT91C_PIOA_PPUER = JOY0;
#endif

#ifdef JOY1
    *AT91C_PIOA_PPUER = JOY1;
#endif

#ifdef SD_WP
    // enable SD card signals
    *AT91C_PIOA_PPUER = SD_WP | SD_CD;
#endif

    *AT91C_PIOA_SODR = MMC_SEL | FPGA0 | FPGA1 | FPGA2; // set output data register

    // output enable register
    *AT91C_PIOA_OER = DISKLED | MMC_SEL | FPGA0 | FPGA1 | FPGA2;
    // pull-up disable register
    *AT91C_PIOA_PPUDR = DISKLED | MMC_SEL | FPGA0 | FPGA1 | FPGA2;

#ifdef XILINX_CCLK
    // xilinx interface
    *AT91C_PIOA_SODR  = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;
    *AT91C_PIOA_OER   = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B;
    *AT91C_PIOA_PPUDR = XILINX_CCLK | XILINX_DIN | XILINX_PROG_B | 
      XILINX_INIT_B | XILINX_DONE;
#endif

#ifdef ALTERA_DCLK
    // altera interface
    *AT91C_PIOA_SODR  = ALTERA_DCLK | ALTERA_DATA0 |  ALTERA_NCONFIG;
    *AT91C_PIOA_OER   = ALTERA_DCLK | ALTERA_DATA0 |  ALTERA_NCONFIG;
    *AT91C_PIOA_PPUDR = ALTERA_DCLK | ALTERA_DATA0 |  ALTERA_NCONFIG |
      ALTERA_NSTATUS | ALTERA_DONE;
#endif

#ifdef MMC_CLKEN
    // MMC_CLKEN may be present 
    // (but is not used anymore, so it's only setup passive)
    *AT91C_PIOA_SODR = MMC_CLKEN;
    *AT91C_PIOA_PPUDR = MMC_CLKEN;
#endif

#ifdef USB_SEL
    *AT91C_PIOA_SODR = USB_SEL;
    *AT91C_PIOA_OER = USB_SEL;
    *AT91C_PIOA_PPUDR = USB_SEL;
#endif

    // Enable peripheral clock in the PMC
    AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_PIOA;
}

volatile int cnt = 0;
void __attribute__((naked)) Usart0IrqHandler (void) {
  //  cnt++;
}

void USART_Init(unsigned long baudrate)
{
    // Configure PA5 and PA6 for USART0 use
    AT91C_BASE_PIOA->PIO_PDR = AT91C_PA5_RXD0 | AT91C_PA6_TXD0;

    // Enable the peripheral clock in the PMC
    AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_US0;

    // Reset and disable receiver & transmitter
    AT91C_BASE_US0->US_CR = AT91C_US_RSTRX | AT91C_US_RSTTX | AT91C_US_RXDIS | AT91C_US_TXDIS;

    // Configure USART0 mode
    AT91C_BASE_US0->US_MR = AT91C_US_USMODE_NORMAL | AT91C_US_CLKS_CLOCK | AT91C_US_CHRL_8_BITS | AT91C_US_PAR_NONE | AT91C_US_NBSTOP_1_BIT | AT91C_US_CHMODE_NORMAL;

    // Configure USART0 rate
    AT91C_BASE_US0->US_BRGR = MCLK / 16 / baudrate;

    // Enable receiver & transmitter
    AT91C_BASE_US0->US_CR = AT91C_US_RXEN | AT91C_US_TXEN;

#if 0
    // configure tx irqs
    // TODO: reset tx/rw pointers

    // http://www.procyonengineering.com/embedded/arm/armlib/docs/html/uartdma_8c-source.html
    // http://www.mikrocontroller.net/articles/DMA
    // http://svn.code.sf.net/p/lejos/code/tags/lejos_nxj_0.9.0/nxtvm/platform/nxt/hs.c

    // setup DMA controller for transmit
    //    AT91C_BASE_US0->US_TNPR = 0;

    puts("Vorher");

    // Set the USART0 IRQ handler address in AIC Source
    AT91C_BASE_AIC->AIC_SVR[AT91C_ID_US0] = (unsigned int)Usart0IrqHandler; 
    AT91C_BASE_AIC->AIC_IECR = (1<<AT91C_ID_US0);
    AT91C_BASE_US0->US_IER = AT91C_US_ENDTX;
    AT91C_BASE_US0->US_IDR = ~AT91C_US_ENDTX;

    puts("Hallo3!");

    for(;;);
#endif
}

RAMFUNC void USART_Write(unsigned char c)
{
    while (!(AT91C_BASE_US0->US_CSR & AT91C_US_TXRDY));
    AT91C_BASE_US0->US_THR = c;
}

unsigned char USART_Read(void) {
    while (!(AT91C_BASE_US0->US_CSR & AT91C_US_RXRDY));
    return AT91C_BASE_US0->US_RHR;
}

#ifndef __GNUC__
signed int fputc(signed int c, FILE *pStream)
{
    if ((pStream == stdout) || (pStream == stderr))
    {
        USART_Write((unsigned char)c);
        return c;
    }

    return EOF;
}
#endif

void SPI_Init()
{
    // Enable the peripheral clock in the PMC
    AT91C_BASE_PMC->PMC_PCER = 1 << AT91C_ID_SPI;

    // Enable SPI interface
    *AT91C_SPI_CR = AT91C_SPI_SPIEN;

    // SPI Mode Register
    *AT91C_SPI_MR = AT91C_SPI_MSTR | AT91C_SPI_MODFDIS  | (0x0E << 16);

    // SPI CS register
    AT91C_SPI_CSR[0] = AT91C_SPI_CPOL | (48 << 8) | (0x00 << 16) | (0x01 << 24);

    // Configure pins for SPI use
    AT91C_BASE_PIOA->PIO_PDR = AT91C_PA14_SPCK | AT91C_PA13_MOSI | AT91C_PA12_MISO;
}

void EnableFpga()
{
    *AT91C_PIOA_CODR = FPGA0;  // clear output
}

void DisableFpga()
{
    SPI_Wait4XferEnd();
    *AT91C_PIOA_SODR = FPGA0;  // set output
}

void EnableOsd()
{
    *AT91C_PIOA_CODR = FPGA1;  // clear output
}

void DisableOsd()
{
    SPI_Wait4XferEnd();
    *AT91C_PIOA_SODR = FPGA1;  // set output
}

#ifdef FPGA3
void EnableIO() {
    *AT91C_PIOA_CODR = FPGA3;  // clear output
}

void DisableIO() {
    SPI_Wait4XferEnd();
    *AT91C_PIOA_SODR = FPGA3;  // set output
}
#endif

unsigned long CheckButton(void)
{
#ifdef BUTTON
    return((~*AT91C_PIOA_PDSR) & BUTTON);
#else
    return user_io_menu_button();
#endif
}

void Timer_Init(void)
{
    *AT91C_PITC_PIMR = AT91C_PITC_PITEN | ((MCLK / 16 / 1000 - 1) & AT91C_PITC_PIV); // counting period 1ms
}

// 12 bits accuracy at 1ms = 4096 ms 
unsigned long GetTimer(unsigned long offset)
{
    unsigned long systimer = (*AT91C_PITC_PIIR & AT91C_PITC_PICNT);
    systimer += offset << 20;
    return (systimer); // valid bits [31:20]
}

unsigned long CheckTimer(unsigned long time)
{
    unsigned long systimer = (*AT91C_PITC_PIIR & AT91C_PITC_PICNT);
    time -= systimer;
    return(time > (1UL << 31));
}

void WaitTimer(unsigned long time)
{
    time = GetTimer(time);
    while (!CheckTimer(time));
}

void SPI_slow() {
  AT91C_SPI_CSR[0] = AT91C_SPI_CPOL | (SPI_SLOW_CLK_VALUE << 8) | (2 << 24); // init clock 100-400 kHz
}

void SPI_fast() {
  // set appropriate SPI speed for SD/SDHC card (max 25 Mhz)
  AT91C_SPI_CSR[0] = AT91C_SPI_CPOL | (SPI_SDC_CLK_VALUE << 8); // 24 MHz SPI clock
}

void SPI_fast_mmc() {
  // set appropriate SPI speed for MMC card (max 20Mhz)
  AT91C_SPI_CSR[0] = AT91C_SPI_CPOL | (SPI_MMC_CLK_VALUE << 8); // 16 MHz SPI clock
}

void TIMER_wait(unsigned long ms) {
  WaitTimer(ms);
}

void EnableDMode() {
  *AT91C_PIOA_CODR = FPGA2; // enable FPGA2 output
}

void DisableDMode() {
  *AT91C_PIOA_SODR = FPGA2; // disable FPGA2 output
}

void SPI_block(unsigned short num) {
  unsigned short i;
  unsigned long t;

  for (i = 0; i < num; i++) {
    while (!(*AT91C_SPI_SR & AT91C_SPI_TDRE)); // wait until transmiter buffer is empty
    *AT91C_SPI_TDR = 0xFF; // write dummy spi data
  }
  while (!(*AT91C_SPI_SR & AT91C_SPI_TXEMPTY)); // wait for transfer end
  t = *AT91C_SPI_RDR; // dummy read to empty receiver buffer for new data
}

RAMFUNC void SPI_block_read(char *addr) {
  *AT91C_PIOA_SODR = AT91C_PA13_MOSI; // set GPIO output register
  *AT91C_PIOA_OER = AT91C_PA13_MOSI;  // GPIO pin as output
  *AT91C_PIOA_PER = AT91C_PA13_MOSI;  // enable GPIO function
  
  // use SPI PDC (DMA transfer)
  *AT91C_SPI_TPR = (unsigned long)addr;
  *AT91C_SPI_TCR = 512;
  *AT91C_SPI_TNCR = 0;
  *AT91C_SPI_RPR = (unsigned long)addr;
  *AT91C_SPI_RCR = 512;
  *AT91C_SPI_RNCR = 0;
  *AT91C_SPI_PTCR = AT91C_PDC_RXTEN | AT91C_PDC_TXTEN; // start DMA transfer
  // wait for tranfer end
  while ((*AT91C_SPI_SR & (AT91C_SPI_ENDTX | AT91C_SPI_ENDRX)) != (AT91C_SPI_ENDTX | AT91C_SPI_ENDRX));
  *AT91C_SPI_PTCR = AT91C_PDC_RXTDIS | AT91C_PDC_TXTDIS; // disable transmitter and receiver

  *AT91C_PIOA_PDR = AT91C_PA13_MOSI; // disable GPIO function
}

void SPI_block_write(char *addr) {
  // use SPI PDC (DMA transfer)
  *AT91C_SPI_TPR = (unsigned long)addr;
  *AT91C_SPI_TCR = 512;
  *AT91C_SPI_TNCR = 0;
  *AT91C_SPI_RCR = 0;
  *AT91C_SPI_PTCR = AT91C_PDC_TXTEN; // start DMA transfer
  // wait for tranfer end
  while ((*AT91C_SPI_SR & AT91C_SPI_ENDTX) != (AT91C_SPI_ENDTX));
  *AT91C_SPI_PTCR = AT91C_PDC_TXTDIS; // disable transmitter
}

char mmc_inserted() {
  return !(*AT91C_PIOA_PDSR & SD_CD);
}

char mmc_write_protected() {
  return (*AT91C_PIOA_PDSR & SD_WP);
}

