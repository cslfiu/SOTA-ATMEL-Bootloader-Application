/****************************************************************************
Title:     STK500v2 compatible bootloader
           Modified for Wiring board ATMega128-16MHz
Author:    Peter Fleury <pfleury@gmx.ch>   http://jump.to/fleury
Compiler:  avr-gcc 3.4.5 or 4.1 / avr-libc 1.4.3
Hardware:  All AVRs with bootloader support, tested with ATmega8
License:   GNU General Public License

Modified:  Worapoht Kornkaewwattanakul <dev@avride.com>   http://www.avride.com
Date:      17 October 2007
Update:    1st, 29 Dec 2007 : Enable CMD_SPI_MULTI but ignore unused command by return 0x00 byte response..
Compiler:  WINAVR20060421
Description: add timeout feature like previous Wiring bootloader

DESCRIPTION:
    This program allows an AVR with bootloader capabilities to
    read/write its own Flash/EEprom. To enter Programming mode
    an input pin is checked. If this pin is pulled low, programming mode
    is entered. If not, normal execution is done from $0000
    "reset" vector in Application area.
    Size fits into a 1024 word bootloader section
	when compiled with avr-gcc 4.1
	(direct replace on Wiring Board without fuse setting changed)

USAGE:
    - Set AVR MCU type and clock-frequency (F_CPU) in the Makefile.
    - Set baud rate below (AVRISP only works with 115200 bps)
    - compile/link the bootloader with the supplied Makefile
    - program the "Boot Flash section size" (BOOTSZ fuses),
      for boot-size 1024 words:  program BOOTSZ01
    - enable the BOOT Reset Vector (program BOOTRST)
    - Upload the hex file to the AVR using any ISP programmer
    - Program Boot Lock Mode 3 (program BootLock 11 and BootLock 12 lock bits) // (leave them)
    - Reset your AVR while keeping PROG_PIN pulled low // (for enter bootloader by switch)
    - Start AVRISP Programmer (AVRStudio/Tools/Program AVR)
    - AVRISP will detect the bootloader
    - Program your application FLASH file and optional EEPROM file using AVRISP

Note:
    Erasing the device without flashing, through AVRISP GUI button "Erase Device"
    is not implemented, due to AVRStudio limitations.
    Flash is always erased before programming.

	AVRdude:
	Please uncomment #define REMOVE_CMD_SPI_MULTI when using AVRdude.
	Comment #define REMOVE_PROGRAM_LOCK_BIT_SUPPORT to reduce code size
	Read Fuse Bits and Read/Write Lock Bits is not supported

NOTES:
    Based on Atmel Application Note AVR109 - Self-programming
    Based on Atmel Application Note AVR068 - STK500v2 Protocol

LICENSE:
    Copyright (C) 2006 Peter Fleury

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

*****************************************************************************/

//************************************************************************
//*	Edit History
//************************************************************************
//*	Jul  7,	2010	<MLS> = Mark Sproul msproul@skycharoit.com
//*	Jul  7,	2010	<MLS> Working on mega2560. No Auto-restart
//*	Jul  7,	2010	<MLS> Switched to 8K bytes (4K words) so that we have room for the monitor
//*	Jul  8,	2010	<MLS> Found older version of source that had auto restart, put that code back in
//*	Jul  8,	2010	<MLS> Adding monitor code
//*	Jul 11,	2010	<MLS> Added blinking LED while waiting for download to start
//*	Jul 11,	2010	<MLS> Added EEPROM test
//*	Jul 29,	2010	<MLS> Added recchar_timeout for timing out on bootloading
//*	Aug 23,	2010	<MLS> Added support for atmega2561
//*	Aug 26,	2010	<MLS> Removed support for BOOT_BY_SWITCH
//*	Sep  8,	2010	<MLS> Added support for atmega16
//*	Nov  9,	2010	<MLS> Issue 392:Fixed bug that 3 !!! in code would cause it to jump to monitor
//*	Jun 24,	2011	<MLS> Removed analogRead (was not used)
//*	Dec 29,	2011	<MLS> Issue 181: added watch dog timmer support
//*	Dec 29,	2011	<MLS> Issue 505:  bootloader is comparing the seqNum to 1 or the current sequence
//*	Jan  1,	2012	<MLS> Issue 543: CMD_CHIP_ERASE_ISP now returns STATUS_CMD_FAILED instead of STATUS_CMD_OK
//*	Jan  1,	2012	<MLS> Issue 543: Write EEPROM now does something (NOT TESTED)
//*	Jan  1,	2012	<MLS> Issue 544: stk500v2 bootloader doesn't support reading fuses
//************************************************************************

//************************************************************************
//*	these are used to test issues
//*	http://code.google.com/p/arduino/issues/detail?id=505
//*	Reported by mark.stubbs, Mar 14, 2011
//*	The STK500V2 bootloader is comparing the seqNum to 1 or the current sequence
//*	(IE: Requiring the sequence to be 1 or match seqNum before continuing).
//*	The correct behavior is for the STK500V2 to accept the PC's sequence number, and echo it back for the reply message.
#define	_FIX_ISSUE_505_
//************************************************************************
//*	Issue 181: added watch dog timmer support
#define	_FIX_ISSUE_181_


#include	<inttypes.h>
#include	<avr/io.h>
#include	<avr/interrupt.h>
#include	<avr/boot.h>
#include	<avr/pgmspace.h>
#include	<util/delay.h>
#include	<avr/eeprom.h>
#include	<avr/common.h>
#include	<stdlib.h>
#include	"command.h"
#include	<string.h>
// #include 	"aes.h"

const unsigned char iv [] = { 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f };
const unsigned char key[] = {0x2b, 0x7e, 0x15, 0x16, 0x28, 0xae, 0xd2, 0xa6, 0xab, 0xf7, 0x15, 0x88, 0x09, 0xcf, 0x4f, 0x3c};

#undef ENABLE_MONITOR


/*
#if defined(_MEGA_BOARD_) || defined(_BOARD_AMBER128_) || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__)
	|| defined(__AVR_ATmega2561__) || defined(__AVR_ATmega1284P__) || defined(ENABLE_MONITOR)
	#undef		ENABLE_MONITOR
	#define		ENABLE_MONITOR
	static void	RunMonitor(void);
#endif
*/

#ifndef EEWE
	#define EEWE    1
#endif
#ifndef EEMWE
	#define EEMWE   2
#endif

//#define	_DEBUG_SERIAL_
//#define	_DEBUG_WITH_LEDS_


/*
 * Uncomment the following lines to save code space
 */
#define	REMOVE_PROGRAM_LOCK_BIT_SUPPORT		// disable program lock bits
#define	REMOVE_BOOTLOADER_LED				// no LED to show active bootloader
#define	REMOVE_CMD_SPI_MULTI				// disable processing of SPI_MULTI commands, Remark this line for AVRDUDE <Worapoht>
//



//************************************************************************
//*	LED on pin "PROGLED_PIN" on port "PROGLED_PORT"
//*	indicates that bootloader is active
//*	PG2 -> LED on Wiring board
//************************************************************************
//#define		BLINK_LED_WHILE_WAITING

#ifdef _MEGA_BOARD_
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB7
#elif defined( _BOARD_AMBER128_ )
	//*	this is for the amber 128 http://www.soc-robotics.com/
	//*	onbarod led is PORTE4
	#define PROGLED_PORT	PORTD
	#define PROGLED_DDR		DDRD
	#define PROGLED_PIN		PINE7
#elif defined( _CEREBOTPLUS_BOARD_ ) || defined(_CEREBOT_II_BOARD_)
	//*	this is for the Cerebot 2560 board and the Cerebot-ii
	//*	onbarod leds are on PORTE4-7
	#define PROGLED_PORT	PORTE
	#define PROGLED_DDR		DDRE
	#define PROGLED_PIN		PINE7
#elif defined( _PENGUINO_ )
	//*	this is for the Penguino
	//*	onbarod led is PORTE4
	#define PROGLED_PORT	PORTC
	#define PROGLED_DDR		DDRC
	#define PROGLED_PIN		PINC6
#elif defined( _ANDROID_2561_ ) || defined( __AVR_ATmega2561__ )
	//*	this is for the Boston Android 2561
	//*	onbarod led is PORTE4
	#define PROGLED_PORT	PORTA
	#define PROGLED_DDR		DDRA
	#define PROGLED_PIN		PINA3
#elif defined( _BOARD_MEGA16 )
	//*	onbarod led is PORTA7
	#define PROGLED_PORT	PORTA
	#define PROGLED_DDR		DDRA
	#define PROGLED_PIN		PINA7
	#define UART_BAUDRATE_DOUBLE_SPEED 0

#elif defined( _BOARD_BAHBOT_ )
	//*	dosent have an onboard LED but this is what will probably be added to this port
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB0

#elif defined( _BOARD_ROBOTX_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB6
#elif defined( _BOARD_CUSTOM1284_BLINK_B0_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB0
#elif defined( _BOARD_CUSTOM1284_ )
	#define PROGLED_PORT	PORTD
	#define PROGLED_DDR		DDRD
	#define PROGLED_PIN		PIND5
#elif defined( _AVRLIP_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB5
#elif defined( _BOARD_STK500_ )
	#define PROGLED_PORT	PORTA
	#define PROGLED_DDR		DDRA
	#define PROGLED_PIN		PINA7
#elif defined( _BOARD_STK502_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB5
#elif defined( _BOARD_STK525_ )
	#define PROGLED_PORT	PORTB
	#define PROGLED_DDR		DDRB
	#define PROGLED_PIN		PINB7
#else
	#define PROGLED_PORT	PORTG
	#define PROGLED_DDR		DDRG
	#define PROGLED_PIN		PING2
#endif



/*
 * define CPU frequency in Mhz here if not defined in Makefile
 */
#ifndef F_CPU
	#define F_CPU 16000000UL
#endif

#define	_BLINK_LOOP_COUNT_	(F_CPU / 2250)
/*
 * UART Baudrate, AVRStudio AVRISP only accepts 115200 bps
 */

#ifndef BAUDRATE
	#define BAUDRATE 115200
#endif

/*
 *  Enable (1) or disable (0) USART double speed operation
 */
#ifndef UART_BAUDRATE_DOUBLE_SPEED
	#if defined (__AVR_ATmega32__)
		#define UART_BAUDRATE_DOUBLE_SPEED 0
	#else
		#define UART_BAUDRATE_DOUBLE_SPEED 1
	#endif
#endif

/*
 * HW and SW version, reported to AVRISP, must match version of AVRStudio
 */
#define CONFIG_PARAM_BUILD_NUMBER_LOW	0
#define CONFIG_PARAM_BUILD_NUMBER_HIGH	0
#define CONFIG_PARAM_HW_VER				0x0F
#define CONFIG_PARAM_SW_MAJOR			2
#define CONFIG_PARAM_SW_MINOR			0x0A

/*
 * Calculate the address where the bootloader starts from FLASHEND and BOOTSIZE
 * (adjust BOOTSIZE below and BOOTLOADER_ADDRESS in Makefile if you want to change the size of the bootloader)
 */
//#define BOOTSIZE 1024
#if FLASHEND > 0x0F000
	#define BOOTSIZE 8192
#else
	#define BOOTSIZE 2048
#endif

#define APP_END  (FLASHEND -(2*BOOTSIZE) + 1)

/*
 * Signature bytes are not available in avr-gcc io_xxx.h
 */
#if defined (__AVR_ATmega8__)
	#define SIGNATURE_BYTES 0x1E9307
#elif defined (__AVR_ATmega16__)
	#define SIGNATURE_BYTES 0x1E9403
#elif defined (__AVR_ATmega32__)
	#define SIGNATURE_BYTES 0x1E9502
#elif defined (__AVR_ATmega8515__)
	#define SIGNATURE_BYTES 0x1E9306
#elif defined (__AVR_ATmega8535__)
	#define SIGNATURE_BYTES 0x1E9308
#elif defined (__AVR_ATmega162__)
	#define SIGNATURE_BYTES 0x1E9404
#elif defined (__AVR_ATmega128__)
	#define SIGNATURE_BYTES 0x1E9702
#elif defined (__AVR_ATmega1280__)
	#define SIGNATURE_BYTES 0x1E9703
#elif defined (__AVR_ATmega2560__)
	#define SIGNATURE_BYTES 0x1E9801
#elif defined (__AVR_ATmega2561__)
	#define SIGNATURE_BYTES 0x1e9802
#elif defined (__AVR_ATmega1284P__)
	#define SIGNATURE_BYTES 0x1e9705
#elif defined (__AVR_ATmega640__)
	#define SIGNATURE_BYTES  0x1e9608
#elif defined (__AVR_ATmega64__)
	#define SIGNATURE_BYTES  0x1E9602
#elif defined (__AVR_ATmega169__)
	#define SIGNATURE_BYTES  0x1e9405
#elif defined (__AVR_AT90USB1287__)
	#define SIGNATURE_BYTES  0x1e9782
#else
	#error "no signature definition for MCU available"
#endif


#if defined(_BOARD_ROBOTX_) || defined(__AVR_AT90USB1287__) || defined(__AVR_AT90USB1286__)
	#define	UART_BAUD_RATE_LOW			UBRR1L
	#define	UART_STATUS_REG				UCSR1A
	#define	UART_CONTROL_REG			UCSR1B
	#define	UART_ENABLE_TRANSMITTER		TXEN1
	#define	UART_ENABLE_RECEIVER		RXEN1
	#define	UART_TRANSMIT_COMPLETE		TXC1
	#define	UART_RECEIVE_COMPLETE		RXC1
	#define	UART_DATA_REG				UDR1
	#define	UART_DOUBLE_SPEED			U2X1

#elif defined(__AVR_ATmega8__) || defined(__AVR_ATmega16__) || defined(__AVR_ATmega32__) \
	|| defined(__AVR_ATmega8515__) || defined(__AVR_ATmega8535__)
	/* ATMega8 with one USART */
	#define	UART_BAUD_RATE_LOW			UBRRL
	#define	UART_STATUS_REG				UCSRA
	#define	UART_CONTROL_REG			UCSRB
	#define	UART_ENABLE_TRANSMITTER		TXEN
	#define	UART_ENABLE_RECEIVER		RXEN
	#define	UART_TRANSMIT_COMPLETE		TXC
	#define	UART_RECEIVE_COMPLETE		RXC
	#define	UART_DATA_REG				UDR
	#define	UART_DOUBLE_SPEED			U2X

#elif defined(__AVR_ATmega64__) || defined(__AVR_ATmega128__) || defined(__AVR_ATmega162__) \
	 || defined(__AVR_ATmega1280__) || defined(__AVR_ATmega2560__) || defined(__AVR_ATmega2561__)
	/* ATMega with two USART, use UART0 */
	#define	UART_BAUD_RATE_LOW			UBRR0L
	#define	UART_STATUS_REG				UCSR0A
	#define	UART_CONTROL_REG			UCSR0B
	#define	UART_ENABLE_TRANSMITTER		TXEN0
	#define	UART_ENABLE_RECEIVER		RXEN0
	#define	UART_TRANSMIT_COMPLETE		TXC0
	#define	UART_RECEIVE_COMPLETE		RXC0
	#define	UART_DATA_REG				UDR0
	#define	UART_DOUBLE_SPEED			U2X0
#elif defined(UBRR0L) && defined(UCSR0A) && defined(TXEN0)
	/* ATMega with two USART, use UART0 */
	#define	UART_BAUD_RATE_LOW			UBRR0L
	#define	UART_STATUS_REG				UCSR0A
	#define	UART_CONTROL_REG			UCSR0B
	#define	UART_ENABLE_TRANSMITTER		TXEN0
	#define	UART_ENABLE_RECEIVER		RXEN0
	#define	UART_TRANSMIT_COMPLETE		TXC0
	#define	UART_RECEIVE_COMPLETE		RXC0
	#define	UART_DATA_REG				UDR0
	#define	UART_DOUBLE_SPEED			U2X0
#elif defined(UBRRL) && defined(UCSRA) && defined(UCSRB) && defined(TXEN) && defined(RXEN)
	//* catch all
	#define	UART_BAUD_RATE_LOW			UBRRL
	#define	UART_STATUS_REG				UCSRA
	#define	UART_CONTROL_REG			UCSRB
	#define	UART_ENABLE_TRANSMITTER		TXEN
	#define	UART_ENABLE_RECEIVER		RXEN
	#define	UART_TRANSMIT_COMPLETE		TXC
	#define	UART_RECEIVE_COMPLETE		RXC
	#define	UART_DATA_REG				UDR
	#define	UART_DOUBLE_SPEED			U2X
#else
	#error "no UART definition for MCU available"
#endif



/*
 * Macro to calculate UBBR from XTAL and baudrate
 */
#if defined(__AVR_ATmega32__) && UART_BAUDRATE_DOUBLE_SPEED
	#define UART_BAUD_SELECT(baudRate,xtalCpu) ((xtalCpu / 4 / baudRate - 1) / 2)
#elif defined(__AVR_ATmega32__)
	#define UART_BAUD_SELECT(baudRate,xtalCpu) ((xtalCpu / 8 / baudRate - 1) / 2)
#elif UART_BAUDRATE_DOUBLE_SPEED
	#define UART_BAUD_SELECT(baudRate,xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*8.0)-1.0+0.5)
#else
	#define UART_BAUD_SELECT(baudRate,xtalCpu) (((float)(xtalCpu))/(((float)(baudRate))*16.0)-1.0+0.5)
#endif




/*
 * States used in the receive state machine
 */
#define	ST_START		0
#define	ST_GET_SEQ_NUM	1
#define ST_MSG_SIZE_1	2
#define ST_MSG_SIZE_2	3
#define ST_GET_TOKEN	4
#define ST_GET_DATA		5
#define	ST_GET_CHECK	6
#define	ST_PROCESS		7

/*
 * States used in the encrypted command packet stage.
 */
 #define	SOTA_PACKET_RETRIEVE_START		0
 #define  SOTA_PACKET_RETRIEVE_SIZE    1
 #define	SOTA_PACKET_RETRIEVE_PROCESSING		2
 #define	SOTA_PACKET_RETRIEVE_FINISHED		3

/*
 * use 16bit address variable for ATmegas with <= 64K flash
 */
#if defined(RAMPZ)
	typedef uint32_t address_t;
#else
	typedef uint16_t address_t;
#endif

/*
 * function prototypes
 */
static void sendchar(char c);
void	PrintDecInt(int theNumber, int digitCnt)
{
int	theChar;
int	myNumber;

	myNumber	=	theNumber;

	if ((myNumber > 100) || (digitCnt >= 3))
	{
		theChar		=	0x30 + myNumber / 100;
		sendchar(theChar );
	}

	if ((myNumber > 10) || (digitCnt >= 2))
	{
		theChar	=	0x30  + ((myNumber % 100) / 10 );
		sendchar(theChar );
	}
	theChar	=	0x30 + (myNumber % 10);
	sendchar(theChar );
}
static unsigned char recchar(void);
//Burak
#define Nb 4
// The number of 32 bit words in a key.
#define Nk 4

// The number of rounds in AES Cipher.
#define Nr 10

#define BLOCKLEN 16

// jcallan@github points out that declaring Multiply as a function
// reduces code size considerably with the Keil ARM compiler.
// See this link for more information: https://github.com/kokke/tiny-AES128-C/pull/3
#ifndef MULTIPLY_AS_A_FUNCTION
  #define MULTIPLY_AS_A_FUNCTION 0
#endif


/*****************************************************************************/
/* Private variables:                                                        */
/*****************************************************************************/
// state - array holding the intermediate results during decryption.
typedef unsigned char state_t[4][4];
static state_t* state;

// The array that stores the round keys.
static unsigned char RoundKey[176];

// The Key input to the AES Program
static const unsigned char* Key;

// #if defined(CBC) && CBC
  // Initial Vector used only for CBC mode
  static unsigned char* Iv;
// #endif

// The lookup-tables are marked const so they can be placed in read-only storage instead of RAM
// The numbers below can be computed dynamically trading ROM for RAM -
// This can be useful in (embedded) bootloader applications, where ROM is often limited.
static const unsigned char sbox[256] =   {
  //0     1    2      3     4    5     6     7      8    9     A      B    C     D     E     F
  0x63, 0x7c, 0x77, 0x7b, 0xf2, 0x6b, 0x6f, 0xc5, 0x30, 0x01, 0x67, 0x2b, 0xfe, 0xd7, 0xab, 0x76,
  0xca, 0x82, 0xc9, 0x7d, 0xfa, 0x59, 0x47, 0xf0, 0xad, 0xd4, 0xa2, 0xaf, 0x9c, 0xa4, 0x72, 0xc0,
  0xb7, 0xfd, 0x93, 0x26, 0x36, 0x3f, 0xf7, 0xcc, 0x34, 0xa5, 0xe5, 0xf1, 0x71, 0xd8, 0x31, 0x15,
  0x04, 0xc7, 0x23, 0xc3, 0x18, 0x96, 0x05, 0x9a, 0x07, 0x12, 0x80, 0xe2, 0xeb, 0x27, 0xb2, 0x75,
  0x09, 0x83, 0x2c, 0x1a, 0x1b, 0x6e, 0x5a, 0xa0, 0x52, 0x3b, 0xd6, 0xb3, 0x29, 0xe3, 0x2f, 0x84,
  0x53, 0xd1, 0x00, 0xed, 0x20, 0xfc, 0xb1, 0x5b, 0x6a, 0xcb, 0xbe, 0x39, 0x4a, 0x4c, 0x58, 0xcf,
  0xd0, 0xef, 0xaa, 0xfb, 0x43, 0x4d, 0x33, 0x85, 0x45, 0xf9, 0x02, 0x7f, 0x50, 0x3c, 0x9f, 0xa8,
  0x51, 0xa3, 0x40, 0x8f, 0x92, 0x9d, 0x38, 0xf5, 0xbc, 0xb6, 0xda, 0x21, 0x10, 0xff, 0xf3, 0xd2,
  0xcd, 0x0c, 0x13, 0xec, 0x5f, 0x97, 0x44, 0x17, 0xc4, 0xa7, 0x7e, 0x3d, 0x64, 0x5d, 0x19, 0x73,
  0x60, 0x81, 0x4f, 0xdc, 0x22, 0x2a, 0x90, 0x88, 0x46, 0xee, 0xb8, 0x14, 0xde, 0x5e, 0x0b, 0xdb,
  0xe0, 0x32, 0x3a, 0x0a, 0x49, 0x06, 0x24, 0x5c, 0xc2, 0xd3, 0xac, 0x62, 0x91, 0x95, 0xe4, 0x79,
  0xe7, 0xc8, 0x37, 0x6d, 0x8d, 0xd5, 0x4e, 0xa9, 0x6c, 0x56, 0xf4, 0xea, 0x65, 0x7a, 0xae, 0x08,
  0xba, 0x78, 0x25, 0x2e, 0x1c, 0xa6, 0xb4, 0xc6, 0xe8, 0xdd, 0x74, 0x1f, 0x4b, 0xbd, 0x8b, 0x8a,
  0x70, 0x3e, 0xb5, 0x66, 0x48, 0x03, 0xf6, 0x0e, 0x61, 0x35, 0x57, 0xb9, 0x86, 0xc1, 0x1d, 0x9e,
  0xe1, 0xf8, 0x98, 0x11, 0x69, 0xd9, 0x8e, 0x94, 0x9b, 0x1e, 0x87, 0xe9, 0xce, 0x55, 0x28, 0xdf,
  0x8c, 0xa1, 0x89, 0x0d, 0xbf, 0xe6, 0x42, 0x68, 0x41, 0x99, 0x2d, 0x0f, 0xb0, 0x54, 0xbb, 0x16 };

static const unsigned char rsbox[256] =
{ 0x52, 0x09, 0x6a, 0xd5, 0x30, 0x36, 0xa5, 0x38, 0xbf, 0x40, 0xa3, 0x9e, 0x81, 0xf3, 0xd7, 0xfb,
  0x7c, 0xe3, 0x39, 0x82, 0x9b, 0x2f, 0xff, 0x87, 0x34, 0x8e, 0x43, 0x44, 0xc4, 0xde, 0xe9, 0xcb,
  0x54, 0x7b, 0x94, 0x32, 0xa6, 0xc2, 0x23, 0x3d, 0xee, 0x4c, 0x95, 0x0b, 0x42, 0xfa, 0xc3, 0x4e,
  0x08, 0x2e, 0xa1, 0x66, 0x28, 0xd9, 0x24, 0xb2, 0x76, 0x5b, 0xa2, 0x49, 0x6d, 0x8b, 0xd1, 0x25,
  0x72, 0xf8, 0xf6, 0x64, 0x86, 0x68, 0x98, 0x16, 0xd4, 0xa4, 0x5c, 0xcc, 0x5d, 0x65, 0xb6, 0x92,
  0x6c, 0x70, 0x48, 0x50, 0xfd, 0xed, 0xb9, 0xda, 0x5e, 0x15, 0x46, 0x57, 0xa7, 0x8d, 0x9d, 0x84,
  0x90, 0xd8, 0xab, 0x00, 0x8c, 0xbc, 0xd3, 0x0a, 0xf7, 0xe4, 0x58, 0x05, 0xb8, 0xb3, 0x45, 0x06,
  0xd0, 0x2c, 0x1e, 0x8f, 0xca, 0x3f, 0x0f, 0x02, 0xc1, 0xaf, 0xbd, 0x03, 0x01, 0x13, 0x8a, 0x6b,
  0x3a, 0x91, 0x11, 0x41, 0x4f, 0x67, 0xdc, 0xea, 0x97, 0xf2, 0xcf, 0xce, 0xf0, 0xb4, 0xe6, 0x73,
  0x96, 0xac, 0x74, 0x22, 0xe7, 0xad, 0x35, 0x85, 0xe2, 0xf9, 0x37, 0xe8, 0x1c, 0x75, 0xdf, 0x6e,
  0x47, 0xf1, 0x1a, 0x71, 0x1d, 0x29, 0xc5, 0x89, 0x6f, 0xb7, 0x62, 0x0e, 0xaa, 0x18, 0xbe, 0x1b,
  0xfc, 0x56, 0x3e, 0x4b, 0xc6, 0xd2, 0x79, 0x20, 0x9a, 0xdb, 0xc0, 0xfe, 0x78, 0xcd, 0x5a, 0xf4,
  0x1f, 0xdd, 0xa8, 0x33, 0x88, 0x07, 0xc7, 0x31, 0xb1, 0x12, 0x10, 0x59, 0x27, 0x80, 0xec, 0x5f,
  0x60, 0x51, 0x7f, 0xa9, 0x19, 0xb5, 0x4a, 0x0d, 0x2d, 0xe5, 0x7a, 0x9f, 0x93, 0xc9, 0x9c, 0xef,
  0xa0, 0xe0, 0x3b, 0x4d, 0xae, 0x2a, 0xf5, 0xb0, 0xc8, 0xeb, 0xbb, 0x3c, 0x83, 0x53, 0x99, 0x61,
  0x17, 0x2b, 0x04, 0x7e, 0xba, 0x77, 0xd6, 0x26, 0xe1, 0x69, 0x14, 0x63, 0x55, 0x21, 0x0c, 0x7d };


// The round constant word array, Rcon[i], contains the values given by
// x to th e power (i-1) being powers of x (x is denoted as {02}) in the field GF(2^8)
// Note that i starts at 1, not 0).
static const unsigned char Rcon[255] = {
  0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a,
  0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39,
  0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a,
  0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8,
  0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef,
  0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc,
  0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40, 0x80, 0x1b,
  0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3,
  0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94,
  0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04, 0x08, 0x10, 0x20,
  0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63, 0xc6, 0x97, 0x35,
  0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd, 0x61, 0xc2, 0x9f,
  0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb, 0x8d, 0x01, 0x02, 0x04,
  0x08, 0x10, 0x20, 0x40, 0x80, 0x1b, 0x36, 0x6c, 0xd8, 0xab, 0x4d, 0x9a, 0x2f, 0x5e, 0xbc, 0x63,
  0xc6, 0x97, 0x35, 0x6a, 0xd4, 0xb3, 0x7d, 0xfa, 0xef, 0xc5, 0x91, 0x39, 0x72, 0xe4, 0xd3, 0xbd,
  0x61, 0xc2, 0x9f, 0x25, 0x4a, 0x94, 0x33, 0x66, 0xcc, 0x83, 0x1d, 0x3a, 0x74, 0xe8, 0xcb  };


/*****************************************************************************/
/* Private functions:                                                        */
/*****************************************************************************/
static unsigned char getSBoxValue(unsigned char num)
{
  return sbox[num];
}

static unsigned char getSBoxInvert(unsigned char num)
{
  return rsbox[num];
}
static void XorWithIv(unsigned char* buf)
{
  unsigned char i;
  for(i = 0; i < 16; ++i)
  {
    buf[i] ^= Iv[i];
  }
}
// This function produces Nb(Nr+1) round keys. The round keys are used in each round to decrypt the states.
static void KeyExpansion(void)
{
  uint32_t i, j, k;
  unsigned char tempa[4]; // Used for the column/row operations

  // The first round key is the key itself.
  for(i = 0; i < Nk; ++i)
  {
    RoundKey[(i * 4) + 0] = Key[(i * 4) + 0];
    RoundKey[(i * 4) + 1] = Key[(i * 4) + 1];
    RoundKey[(i * 4) + 2] = Key[(i * 4) + 2];
    RoundKey[(i * 4) + 3] = Key[(i * 4) + 3];
  }

  // All other round keys are found from the previous round keys.
  for(; (i < (Nb * (Nr + 1))); ++i)
  {
    for(j = 0; j < 4; ++j)
    {
      tempa[j]=RoundKey[(i-1) * 4 + j];
    }
    if (i % Nk == 0)
    {
      // This function rotates the 4 bytes in a word to the left once.
      // [a0,a1,a2,a3] becomes [a1,a2,a3,a0]

      // Function RotWord()
      {
        k = tempa[0];
        tempa[0] = tempa[1];
        tempa[1] = tempa[2];
        tempa[2] = tempa[3];
        tempa[3] = k;
      }

      // SubWord() is a function that takes a four-byte input word and
      // applies the S-box to each of the four bytes to produce an output word.

      // Function Subword()
      {
        tempa[0] = getSBoxValue(tempa[0]);
        tempa[1] = getSBoxValue(tempa[1]);
        tempa[2] = getSBoxValue(tempa[2]);
        tempa[3] = getSBoxValue(tempa[3]);
      }

      tempa[0] =  tempa[0] ^ Rcon[i/Nk];
    }
    else if (Nk > 6 && i % Nk == 4)
    {
      // Function Subword()
      {
        tempa[0] = getSBoxValue(tempa[0]);
        tempa[1] = getSBoxValue(tempa[1]);
        tempa[2] = getSBoxValue(tempa[2]);
        tempa[3] = getSBoxValue(tempa[3]);
      }
    }
    // uint32_t brk = 0xFF ^ tempa[0];
    // static unsigned char burakim[1024];
    // for(uint8_t i =0; i<1024;i++)
    // burakim[i] = 0xFF;
    // RoundKey[i * 4 + 0] = RoundKey[(i - Nk) * 4 + 0];
		// sendchar(0x91);
    RoundKey[i * 4 + 0] = RoundKey[(i - Nk) * 4 + 0] ^ tempa[0];
		// sendchar(0x92);
    RoundKey[i * 4 + 1] = RoundKey[(i - Nk) * 4 + 1] ^ tempa[1];
		// sendchar(0x93);
    RoundKey[i * 4 + 2] = RoundKey[(i - Nk) * 4 + 2] ^ tempa[2];
		// sendchar(0x94);
    RoundKey[i * 4 + 3] = RoundKey[(i - Nk) * 4 + 3] ^ tempa[3];
		// sendchar(0x95);
  }
}

// This function adds the round key to state.
// The round key is added to the state by an XOR function.
static void AddRoundKey(unsigned char round)
{
  unsigned char i,j;
  for(i=0;i<4;++i)
  {
    for(j = 0; j < 4; ++j)
    {
      (*state)[i][j] ^= RoundKey[round * Nb * 4 + i * Nb + j];
    }
  }
}

// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void SubBytes(void)
{
  unsigned char i, j;
  for(i = 0; i < 4; ++i)
  {
    for(j = 0; j < 4; ++j)
    {
      (*state)[j][i] = getSBoxValue((*state)[j][i]);
    }
  }
}

// The ShiftRows() function shifts the rows in the state to the left.
// Each row is shifted with different offset.
// Offset = Row number. So the first row is not shifted.
static void ShiftRows(void)
{
  unsigned char temp;

  // Rotate first row 1 columns to left
  temp           = (*state)[0][1];
  (*state)[0][1] = (*state)[1][1];
  (*state)[1][1] = (*state)[2][1];
  (*state)[2][1] = (*state)[3][1];
  (*state)[3][1] = temp;

  // Rotate second row 2 columns to left
  temp           = (*state)[0][2];
  (*state)[0][2] = (*state)[2][2];
  (*state)[2][2] = temp;

  temp       = (*state)[1][2];
  (*state)[1][2] = (*state)[3][2];
  (*state)[3][2] = temp;

  // Rotate third row 3 columns to left
  temp       = (*state)[0][3];
  (*state)[0][3] = (*state)[3][3];
  (*state)[3][3] = (*state)[2][3];
  (*state)[2][3] = (*state)[1][3];
  (*state)[1][3] = temp;
}

static unsigned char xtime(unsigned char x)
{
  return ((x<<1) ^ (((x>>7) & 1) * 0x1b));
}

// MixColumns function mixes the columns of the state matrix
static void MixColumns(void)
{
  unsigned char i;
  unsigned char Tmp,Tm,t;
  for(i = 0; i < 4; ++i)
  {
    t   = (*state)[i][0];
    Tmp = (*state)[i][0] ^ (*state)[i][1] ^ (*state)[i][2] ^ (*state)[i][3] ;
    Tm  = (*state)[i][0] ^ (*state)[i][1] ; Tm = xtime(Tm);  (*state)[i][0] ^= Tm ^ Tmp ;
    Tm  = (*state)[i][1] ^ (*state)[i][2] ; Tm = xtime(Tm);  (*state)[i][1] ^= Tm ^ Tmp ;
    Tm  = (*state)[i][2] ^ (*state)[i][3] ; Tm = xtime(Tm);  (*state)[i][2] ^= Tm ^ Tmp ;
    Tm  = (*state)[i][3] ^ t ;        Tm = xtime(Tm);  (*state)[i][3] ^= Tm ^ Tmp ;
  }
}

// Multiply is used to multiply numbers in the field GF(2^8)
#if MULTIPLY_AS_A_FUNCTION
static unsigned char Multiply(unsigned char x, unsigned char y)
{
  return (((y & 1) * x) ^
       ((y>>1 & 1) * xtime(x)) ^
       ((y>>2 & 1) * xtime(xtime(x))) ^
       ((y>>3 & 1) * xtime(xtime(xtime(x)))) ^
       ((y>>4 & 1) * xtime(xtime(xtime(xtime(x))))));
  }
#else
#define Multiply(x, y)                                \
      (  ((y & 1) * x) ^                              \
      ((y>>1 & 1) * xtime(x)) ^                       \
      ((y>>2 & 1) * xtime(xtime(x))) ^                \
      ((y>>3 & 1) * xtime(xtime(xtime(x)))) ^         \
      ((y>>4 & 1) * xtime(xtime(xtime(xtime(x))))))   \

#endif

// MixColumns function mixes the columns of the state matrix.
// The method used to multiply may be difficult to understand for the inexperienced.
// Please use the references to gain more information.
static void InvMixColumns(void)
{
  int i;
  unsigned char a,b,c,d;
  for(i=0;i<4;++i)
  {
    a = (*state)[i][0];
    b = (*state)[i][1];
    c = (*state)[i][2];
    d = (*state)[i][3];

    (*state)[i][0] = Multiply(a, 0x0e) ^ Multiply(b, 0x0b) ^ Multiply(c, 0x0d) ^ Multiply(d, 0x09);
    (*state)[i][1] = Multiply(a, 0x09) ^ Multiply(b, 0x0e) ^ Multiply(c, 0x0b) ^ Multiply(d, 0x0d);
    (*state)[i][2] = Multiply(a, 0x0d) ^ Multiply(b, 0x09) ^ Multiply(c, 0x0e) ^ Multiply(d, 0x0b);
    (*state)[i][3] = Multiply(a, 0x0b) ^ Multiply(b, 0x0d) ^ Multiply(c, 0x09) ^ Multiply(d, 0x0e);
  }
}


// The SubBytes Function Substitutes the values in the
// state matrix with values in an S-box.
static void InvSubBytes(void)
{
  unsigned char i,j;
  for(i=0;i<4;++i)
  {
    for(j=0;j<4;++j)
    {
      (*state)[j][i] = getSBoxInvert((*state)[j][i]);
    }
  }
}

static void InvShiftRows(void)
{
  unsigned char temp;

  // Rotate first row 1 columns to right
  temp=(*state)[3][1];
  (*state)[3][1]=(*state)[2][1];
  (*state)[2][1]=(*state)[1][1];
  (*state)[1][1]=(*state)[0][1];
  (*state)[0][1]=temp;

  // Rotate second row 2 columns to right
  temp=(*state)[0][2];
  (*state)[0][2]=(*state)[2][2];
  (*state)[2][2]=temp;

  temp=(*state)[1][2];
  (*state)[1][2]=(*state)[3][2];
  (*state)[3][2]=temp;

  // Rotate third row 3 columns to right
  temp=(*state)[0][3];
  (*state)[0][3]=(*state)[1][3];
  (*state)[1][3]=(*state)[2][3];
  (*state)[2][3]=(*state)[3][3];
  (*state)[3][3]=temp;
}


// Cipher is the main function that encrypts the PlainText.
static void Cipher(void)
{
  unsigned char round = 0;

  // Add the First round key to the state before starting the rounds.
  AddRoundKey(0);

  // There will be Nr rounds.
  // The first Nr-1 rounds are identical.
  // These Nr-1 rounds are executed in the loop below.
  for(round = 1; round < Nr; ++round)
  {
    SubBytes();
    ShiftRows();
    MixColumns();
    AddRoundKey(round);
  }

  // The last round is given below.
  // The MixColumns function is not here in the last round.
  SubBytes();
  ShiftRows();
  AddRoundKey(Nr);
}

static void InvCipher(void)
{
  unsigned char round=0;

  // Add the First round key to the state before starting the rounds.
  AddRoundKey(Nr);

  // There will be Nr rounds.
  // The first Nr-1 rounds are identical.
  // These Nr-1 rounds are executed in the loop below.
  for(round=Nr-1;round>0;round--)
  {
    InvShiftRows();
    InvSubBytes();
    AddRoundKey(round);
    InvMixColumns();
  }

  // The last round is given below.
  // The MixColumns function is not here in the last round.
  InvShiftRows();
  InvSubBytes();
  AddRoundKey(0);
}

static void BlockCopy(unsigned char* output, const unsigned char* input)
{
  unsigned char i;
  for (i=0;i<16;++i)
  {
    output[i] = input[i];
  }
}
static void aes_decrypt(unsigned char* output, unsigned char* input, unsigned int length)
{
	uintptr_t i;

  uint8_t extra = length % BLOCKLEN; /* Remaining bytes in the last non-full block */

  // Skip the key expansion if key is passed as 0
  if (0 != key)
  {
    Key = key;
    KeyExpansion();
  }
	// sendchar(0x16);
	// PrintDecInt(length,10);
	// sendchar(0x17);
  // If iv is passed as 0, we continue to encrypt without re-setting the Iv
  if (iv != 0)
  {
    Iv = (uint8_t*)iv;
  }
	// sendchar(0x18);
	// PrintDecInt(length,10);
	// sendchar(0x19);
  for (i = 0; i < length; i += BLOCKLEN)
  {
    memcpy(output, input, BLOCKLEN);
    state = (state_t*)output;
    InvCipher();
    XorWithIv(output);
    Iv = input;
    input += BLOCKLEN;
    output += BLOCKLEN;
  }
	// sendchar(0x20);
	// PrintDecInt(length,10);
	// sendchar(0x21);
  if (extra)
  {
    memcpy(output, input, extra);
    state = (state_t*)output;
    InvCipher();
  }

}

static void aes_encrypt(unsigned char* output, unsigned char* input, unsigned int length){
	uintptr_t i;
  uint8_t extra = length % 16; /* Remaining bytes in the last non-full block */

  // Skip the key expansion if key is passed as 0
  if (0 != key)
  {
    Key = key;
    KeyExpansion();
  }

  if (iv != 0)
  {
    Iv = (uint8_t*)iv;
  }

  for (i = 0; i < length; i += 16)
  {
    XorWithIv(input);
    memcpy(output, input, 16);
    state = (state_t*)output;
    Cipher();
    Iv = output;
    input += 16;
    output += 16;
    //printf("Step %d - %d", i/16, i);
  }

  if (extra)
  {
    memcpy(output, input, extra);
    state = (state_t*)output;
    Cipher();
  }
}


//Burak
/*
 * since this bootloader is not linked against the avr-gcc crt1 functions,
 * to reduce the code size, we need to provide our own initialization
 */
void __jumpMain	(void) __attribute__ ((naked)) __attribute__ ((section (".init9")));
#include <avr/sfr_defs.h>

//#define	SPH_REG	0x3E
//#define	SPL_REG	0x3D

//*****************************************************************************
void __jumpMain(void)
{
//*	July 17, 2010	<MLS> Added stack pointer initialzation
//*	the first line did not do the job on the ATmega128

	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );

//*	set stack pointer to top of RAM

	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_HI_ADDR) );

	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_LO_ADDR) );

	asm volatile ( "clr __zero_reg__" );									// GCC depends on register r1 set to 0
	asm volatile ( "out %0, __zero_reg__" :: "I" (_SFR_IO_ADDR(SREG)) );	// set SREG to 0
	asm volatile ( "jmp main");												// jump to main()
}


//*****************************************************************************
void delay_ms(unsigned int timedelay)
{
	unsigned int i;
	for (i=0;i<timedelay;i++)
	{
		_delay_ms(0.5);
	}
}



//*****************************************************************************
/*
 * send single byte to USART, wait until transmission is completed
 */
static void sendchar(char c)
{
	UART_DATA_REG	=	c;										// prepare transmission
	while (!(UART_STATUS_REG & (1 << UART_TRANSMIT_COMPLETE)));	// wait until byte sent
	UART_STATUS_REG |= (1 << UART_TRANSMIT_COMPLETE);			// delete TXCflag
}


//************************************************************************
static int	Serial_Available(void)
{
	return(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE));	// wait for data
}


//*****************************************************************************
/*
 * Read single byte from USART, block if no data available
 */
static unsigned char recchar(void)
{
	while (!(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE)))
	{
		// wait for data
	}
	return UART_DATA_REG;
}

#define	MAX_TIME_COUNT	(F_CPU >> 1)
//*****************************************************************************
static unsigned char recchar_timeout(void)
{
uint32_t count = 0;

	while (!(UART_STATUS_REG & (1 << UART_RECEIVE_COMPLETE)))
	{
		// wait for data
		count++;
		if (count > MAX_TIME_COUNT)
		{
		unsigned int	data;
		#if (FLASHEND > 0x10000)
			data	=	pgm_read_word_far(0);	//*	get the first word of the user program
		#else
			data	=	pgm_read_word_near(0);	//*	get the first word of the user program
		#endif
			if (data != 0xffff)					//*	make sure its valid before jumping to it.
			{
				asm volatile(
						"clr	r30		\n\t"
						"clr	r31		\n\t"
						"ijmp	\n\t"
						);
			}
			count	=	0;
		}
	}
	return UART_DATA_REG;
}
unsigned char getData(unsigned int* boot_state)
{
  unsigned char c;
  if (*boot_state==1)
  {
    *boot_state	=	0;
    c			=	UART_DATA_REG;
  }
  else
  {
  //	c	=	recchar();
    c	=	recchar_timeout();

  }
  return c;
}


//*	for watch dog timer startup
void (*app_start)(void) = 0x0000;
// static unsigned char in[]  = {
// 	0x7B,0x17,0x19,0x77,0xB7,0xA4,0x8D,0x72,0x24,0x58,0x12,0x6B,0x10,0x02,0x13,0x5F,
// 	0x91,0x76,0xC6,0x46,0xB4,0x30,0xEA,0xD1,0x47,0xBE,0x8D,0xC8,0x8B,0x28,0xCD,0x04,
// 	0x09,0x20,0xEE,0x44,0x41,0xFD,0xBC,0xED,0x8F,0x37,0x0F,0x99,0x55,0x05,0x99,0xCE,
// 	0x19,0xBD,0x0A,0xDC,0xC6,0x0C,0xD4,0x69,0x3C,0x1E,0x93,0x55,0xD0,0x70,0x8C,0x44,
// 	0xCB,0x8E,0xFB,0x7E,0x92,0x28,0xB2,0xB5,0xEB,0xE8,0xE8,0xC6,0xA1,0xDE,0x96,0x41,
// 	0x0E,0x35,0x38,0xF7,0x2F,0x16,0xFD,0x6A,0xD0,0xEE,0xBE,0x8D,0x24,0x0D,0x20,0xB7,
// 	0x0E,0x6C,0xC2,0x66,0xCC,0xDE,0x04,0xE5,0xA5,0xCD,0x9F,0x85,0xE6,0x59,0xB3,0x78,
// 	0xA9,0x1E,0xB5,0x25,0xE8,0x27,0x9E,0x1B,0x28,0x86,0x41,0x48,0x2F,0x5A,0x8D,0x20,
// 	0x2A,0x74,0x1B,0xE8,0x6E,0x02,0xE4,0xD0,0x71,0x48,0xD2,0x41,0x0F,0x61,0x83,0xEC,
// 	0x8E,0x44,0xA8,0x6D,0xEC,0xD3,0xC2,0x08,0xC7,0x51,0xBD,0xD3,0x1D,0x12,0x8A,0xB8,
// 	0x15,0x7F,0x79,0x00,0xD2,0xC5,0xB0,0xB2,0x50,0xA9,0x45,0x96,0x6D,0xEA,0x4F,0xAD,
// 	0x0B,0xFC,0xB7,0xAE,0xB7,0x1A,0xFB,0x94,0x1B,0x25,0x52,0x80,0x28,0x09,0x5B,0x4E,
// 	0x64,0xD2,0x8A,0xE1,0x2F,0x7A,0x4C,0xFA,0x74,0x4E,0x27,0xB1,0x14,0x70,0x29,0x20,
// 	0x22,0xC6,0x1C,0x39,0xAC,0xD1,0x51,0x95,0x95,0x16,0x98,0xE1,0xB2,0xE2,0x19,0x57,
// 	0xB4,0x91,0x09,0xDD,0xA6,0x53,0x07,0x73,0x47,0x3C,0x0E,0x5E,0x13,0x69,0xE9,0x99,
// 	0x4F,0x4E,0x61,0x4A,0xA3,0xF7,0x27,0x2F,0xD4,0x8B,0x00,0x27,0xAF,0x22,0x44,0xD4};

 unsigned char	receivedPacket[288] ={
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
  0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04,
	0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04

};

 unsigned char aes_buffer[288] = {
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
  0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03,
	0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03, 0x03 };

	// unsigned char dummyArray[256] = {
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87,
	// 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87, 0x87};

//*****************************************************************************

	unsigned char authenticationToken[4] = {0x53, 0xef, 0x34,0x23};
 	unsigned char isAuthenticated = 0;
	unsigned int  packetSize = 0;
  union {
	uint32_t  authenticationNumber;
	uint8_t authBytes[4];
}authenticationNumber;

address_t		address			=	0;

#define AUTHENTICATION
#define SEQUENCE_NUMBER_ENFORCEMENT;

int main(void)
{

	unsigned int residualNumber = 0;
  unsigned char packetRetrieveState;
  packetRetrieveState = SOTA_PACKET_RETRIEVE_START;
  int packetRetrieveIndex = 0;

	address_t		eraseAddress	=	0;
	unsigned char	msgParseState;
	unsigned int	ii				=	0;
	unsigned char	checksum		=	0;
	unsigned char	seqNum			=	0;
	unsigned int	msgLength		=	0;
	unsigned char	msgBuffer[285];


	unsigned int finalResponseSize;

	unsigned int receivedPacketIndex = 0;

	//unsigned char msgBuffer[290];
	unsigned long int 	sequenceNumber = 0;
	unsigned char	c, *p;
	unsigned char   isLeave = 0;
	unsigned long	boot_timeout;
	unsigned long	boot_timer;
	unsigned int	boot_state;
#ifdef ENABLE_MONITOR
	unsigned int	exPointCntr		=	0;
	unsigned int	rcvdCharCntr	=	0;
#endif

	//*	some chips dont set the stack properly
	asm volatile ( ".set __stack, %0" :: "i" (RAMEND) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND >> 8) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_HI_ADDR) );
	asm volatile ( "ldi	16, %0" :: "i" (RAMEND & 0x0ff) );
	asm volatile ( "out %0,16" :: "i" (AVR_STACK_POINTER_LO_ADDR) );

#ifdef _FIX_ISSUE_181_
	//************************************************************************
	//*	Dec 29,	2011	<MLS> Issue #181, added watch dog timmer support
	//*	handle the watch dog timer
	uint8_t	mcuStatusReg;
	mcuStatusReg	=	MCUSR;

	__asm__ __volatile__ ("cli");
	__asm__ __volatile__ ("wdr");
	MCUSR	=	0;
	WDTCSR	|=	_BV(WDCE) | _BV(WDE);
	WDTCSR	=	0;
	__asm__ __volatile__ ("sei");
	// check if WDT generated the reset, if so, go straight to app
	if (mcuStatusReg & _BV(WDRF))
	{
		app_start();
	}
	//************************************************************************
#endif

// #ifdef BLINK_LED_WHILE_WAITING
// //	boot_timeout	=	 90000;		//*	should be about 4 seconds
// //	boot_timeout	=	170000;
// 	boot_timeout	=	 20000;		//*	should be about 1 second
// #else
// 	boot_timeout	=	3500000; // 7 seconds , approx 2us per step when optimize "s"
// #endif

	boot_timer	=	0;
	boot_state	=	0;
	boot_timeout = 3500000;
	/*
	 * Init UART
	 * set baudrate and enable USART receiver and transmiter without interrupts
	 */
#if UART_BAUDRATE_DOUBLE_SPEED
	UART_STATUS_REG		|=	(1 <<UART_DOUBLE_SPEED);
#endif
	UART_BAUD_RATE_LOW	=	UART_BAUD_SELECT(BAUDRATE,F_CPU);
	UART_CONTROL_REG	=	(1 << UART_ENABLE_RECEIVER) | (1 << UART_ENABLE_TRANSMITTER);

	asm volatile ("nop");			// wait until port has changed


	while (boot_state==0)
	{
		while ((!(Serial_Available())) && (boot_state == 0))		// wait for data
		{
			_delay_ms(0.001);
			boot_timer++;
			if (boot_timer > boot_timeout)
			{
				boot_state	=	1; // (after ++ -> boot_state=2 bootloader timeout, jump to main 0x00000 )
			}
		}
		boot_state++; // ( if boot_state=1 bootloader received byte from UART, enter bootloader mode)
	}

		// PrintDecInt(boot_state,10);

// sendchar(0x32);




	if (boot_state==1)
	{
		while (!isLeave)
		{
			packetRetrieveIndex = 0;
		  packetRetrieveState = SOTA_PACKET_RETRIEVE_START;
		   while ( packetRetrieveState != SOTA_PACKET_RETRIEVE_FINISHED )
		   {

		     c = getData(&boot_state);
		     if (packetRetrieveState == SOTA_PACKET_RETRIEVE_START)
		  	{
		     if(c == SOTA_MESSAGE_START)
		     {
		       packetRetrieveState = SOTA_PACKET_RETRIEVE_SIZE;
		       }
		     }
		     else if(packetRetrieveState == SOTA_PACKET_RETRIEVE_SIZE)
		     {


		       unsigned char lowest = getData(&boot_state);

		       packetSize 	=	((c)<<8) | lowest; // ok
					 // sendchar(0x50);
					 // sendchar(c);
						// sendchar(0x51);
						// sendchar(lowest);

		       packetRetrieveState = SOTA_PACKET_RETRIEVE_PROCESSING;

		     }
		     else if(packetRetrieveState == SOTA_PACKET_RETRIEVE_PROCESSING)
		     {

		       if(packetRetrieveIndex < packetSize){
		       receivedPacket[packetRetrieveIndex] = c;
		  		//sendchar(c);
		  		packetRetrieveIndex++;
		  	}
		       else{
		       packetRetrieveState = SOTA_PACKET_RETRIEVE_FINISHED;}
		     }
		   }
			//   sendchar(0x98);
			//  for(int i=0; i<packetSize;i++)
			//  sendchar(receivedPacket[i]);
			//  sendchar(0x33);
			// sendchar(0x59);
			// PrintDecInt(packetSize,10);
			// sendchar(0x60);
				// #ifdef ECHO_INTEGRITY
				// for(int i=0; i <packetSize; i++)
				// sendchar(receivedPacket[i]);
				// #endif

// PrintDecInt(packetSize,10);
// sendchar(0x98);

		   aes_decrypt(aes_buffer, receivedPacket, packetSize);
  // sendchar(0x34);


// PrintDecInt(packetSize,10);

 // sendchar(0x99);




			 receivedPacketIndex = 0;
			msgParseState	=	ST_START;
			while (msgParseState != ST_PROCESS )
			{
				c = aes_buffer[receivedPacketIndex];
				  // sendchar(c);

				switch (msgParseState)
				{
					case ST_START:
					{
						if ( c == MESSAGE_START )
						{
							msgParseState	=	ST_GET_SEQ_NUM;
							checksum		=	MESSAGE_START^0;
						}
						break;
					}
					case ST_GET_SEQ_NUM:{
						// #ifdef SEQUENCE_NUMBER_ENFORCEMENT
						if ( (c == 1) || (c == seqNum) )
						{
							seqNum			=	c;
							msgParseState	=	ST_MSG_SIZE_1;
							checksum		^=	c;
						}
						else
						{
							msgParseState	=	ST_START;
						}
					 // 		 #else
						// seqNum			=	c;
						// msgParseState	=	ST_MSG_SIZE_1;
						// checksum		^=	c;
					 // #endif
						break;
					}

					case ST_MSG_SIZE_1:
					{
						msgLength		=	c<<8;
						msgParseState	=	ST_MSG_SIZE_2;
						checksum		^=	c;
						break;
					}

					case ST_MSG_SIZE_2:
					{
						msgLength		|=	c;
						msgParseState	=	ST_GET_TOKEN;
						checksum		^=	c;
						break;
					}

					case ST_GET_TOKEN:
					{
						if ( c == TOKEN )
						{
							msgParseState	=	ST_GET_DATA;
							checksum		^=	c;
							ii				=	0;
						}
						else
						{
							msgParseState	=	ST_START;
						}
						break;
						}
					case ST_GET_DATA:
					{
						msgBuffer[ii++]	=	c;
						checksum		^=	c;
						if (ii == msgLength )
						{
							msgParseState	=	ST_GET_CHECK;
						}
						break;
					}

					case ST_GET_CHECK:
					{
						if ( c == checksum )
						{
							msgParseState	=	ST_PROCESS;
						}
						else
						{
							msgParseState	=	ST_START;
						}
						break;
					}
				}	//	switch
				receivedPacketIndex++;
			}	//	while(msgParseState)
			/*
			 * Now process the STK500 commands, see Atmel Appnote AVR068
			 */


			// sendchar(0x61);
/*

union{
	float authenticationNumber;
	char authBytes[4]
}authenticationToken;

*/

// sendchar(0x99);
// sendchar(msgBuffer[0]);
// sendchar(0x98);

			switch (msgBuffer[0])
			{
	// #ifndef AUTHENTICATION
				case CMD_AUTH:
				{

					if((msgBuffer[5] == authenticationToken[0]) && (msgBuffer[6] == authenticationToken[1]) && (msgBuffer[7] == authenticationToken[2]) && (msgBuffer[8] == authenticationToken[3]))
					{
						authenticationNumber.authBytes[0] = msgBuffer[1];
						authenticationNumber.authBytes[1] = msgBuffer[2];
						authenticationNumber.authBytes[2] = msgBuffer[3];
						authenticationNumber.authBytes[3] = msgBuffer[4];

						//PrintDecInt((uint32_t)msgBuffer[4]);
						uint32_t number = (((uint32_t)msgBuffer[4])); // i dont have any idea why it's working.

						authenticationNumber.authenticationNumber = authenticationNumber.authenticationNumber +  number;

						msgBuffer[0] = STATUS_CMD_OK;
						msgBuffer[1] = authenticationNumber.authBytes[0];
						msgBuffer[2] = authenticationNumber.authBytes[1];
						msgBuffer[4] = authenticationNumber.authBytes[3];

						msgLength = 5;
						isAuthenticated = 1;

					}
					else
					{
							msgBuffer[1] = STATUS_CMD_FAILED;
							msgLength = 2;
							isAuthenticated = 0;

					}
					break;
				}
	// #endif
	#ifndef REMOVE_CMD_SPI_MULTI
				case CMD_SPI_MULTI:
					{
						if(isAuthenticated == 1){
						unsigned char answerByte;
						unsigned char flag=0;

						if ( msgBuffer[4]== 0x30 )
						{
							unsigned char signatureIndex	=	msgBuffer[6];

							if ( signatureIndex == 0 )
							{
								answerByte	=	(SIGNATURE_BYTES >> 16) & 0x000000FF;
							}
							else if ( signatureIndex == 1 )
							{
								answerByte	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
							}
							else
							{
								answerByte	=	SIGNATURE_BYTES & 0x000000FF;
							}
						}
						else if ( msgBuffer[4] & 0x50 )
						{
						//*	Issue 544: 	stk500v2 bootloader doesn't support reading fuses
						//*	I cant find the docs that say what these are supposed to be but this was figured out by trial and error
						//	answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
						//	answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
						//	answerByte	=	boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
							if (msgBuffer[4] == 0x50)
							{
								answerByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
							}
							else if (msgBuffer[4] == 0x58)
							{
								answerByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
							}
							else
							{
								answerByte	=	0;
							}
						}
						else
						{
							answerByte	=	0; // for all others command are not implemented, return dummy value for AVRDUDE happy <Worapoht>
						}
						if ( !flag )
						{
							msgLength		=	7;
							msgBuffer[1]	=	STATUS_CMD_OK;
							msgBuffer[2]	=	0;
							msgBuffer[3]	=	msgBuffer[4];
							msgBuffer[4]	=	0;
							msgBuffer[5]	=	answerByte;
							msgBuffer[6]	=	STATUS_CMD_OK;
						}
					}
					}
					break;
	#endif
				case CMD_SIGN_ON:
				{
					 if(isAuthenticated == 1){

					msgLength		=	11;
					msgBuffer[1] 	=	STATUS_CMD_OK;
					msgBuffer[2] 	=	8;
					msgBuffer[3] 	=	'A';
					msgBuffer[4] 	=	'V';
					msgBuffer[5] 	=	'R';
					msgBuffer[6] 	=	'I';
					msgBuffer[7] 	=	'S';
					msgBuffer[8] 	=	'P';
					msgBuffer[9] 	=	'_';
					msgBuffer[10]	=	'2';
				}
				else
				{
					msgBuffer[1] 	=	STATUS_CMD_FAILED;
					msgLength		=	2;
				}
					break;
				}

				case CMD_GET_PARAMETER:
					{
						unsigned char value;
						switch(msgBuffer[1])
						{
						case PARAM_BUILD_NUMBER_LOW:
							value	=	CONFIG_PARAM_BUILD_NUMBER_LOW;
							break;
						case PARAM_BUILD_NUMBER_HIGH:
							value	=	CONFIG_PARAM_BUILD_NUMBER_HIGH;
							break;
						case PARAM_HW_VER:
							value	=	CONFIG_PARAM_HW_VER;
							break;
						case PARAM_SW_MAJOR:
							value	=	CONFIG_PARAM_SW_MAJOR;
							break;
						case PARAM_SW_MINOR:
							value	=	CONFIG_PARAM_SW_MINOR;
							break;
						default:
							value	=	0;
							break;
						}
						msgLength		=	3;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	value;
					}
					break;

				case CMD_LEAVE_PROGMODE_ISP:{
	 if(isAuthenticated == 1){
					isLeave	=	1;
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
				}
				else
				{
					msgBuffer[1] 	=	STATUS_CMD_FAILED;
					msgLength		=	2;
				}
				break;
				}
				case CMD_SET_PARAMETER:
				case CMD_ENTER_PROGMODE_ISP:{
				if(isAuthenticated == 1){
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_OK;
				}
				else
				{
					msgBuffer[1] 	=	STATUS_CMD_FAILED;
					msgLength		=	2;
				}
					break;
				}
				case CMD_READ_SIGNATURE_ISP:
					{
						unsigned char signatureIndex	=	msgBuffer[4];
						unsigned char signature;

						if ( signatureIndex == 0 )
							signature	=	(SIGNATURE_BYTES >>16) & 0x000000FF;
						else if ( signatureIndex == 1 )
							signature	=	(SIGNATURE_BYTES >> 8) & 0x000000FF;
						else
							signature	=	SIGNATURE_BYTES & 0x000000FF;

						msgLength		=	4;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	signature;
						msgBuffer[3]	=	STATUS_CMD_OK;
					}
					break;

				case CMD_READ_LOCK_ISP:
					msgLength		=	4;
					msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[2]	=	boot_lock_fuse_bits_get( GET_LOCK_BITS );
					msgBuffer[3]	=	STATUS_CMD_OK;
					break;

				case CMD_READ_FUSE_ISP:
					{
						unsigned char fuseBits;

						if ( msgBuffer[2] == 0x50 )
						{
							if ( msgBuffer[3] == 0x08 )
								fuseBits	=	boot_lock_fuse_bits_get( GET_EXTENDED_FUSE_BITS );
							else
								fuseBits	=	boot_lock_fuse_bits_get( GET_LOW_FUSE_BITS );
						}
						else
						{
							fuseBits	=	boot_lock_fuse_bits_get( GET_HIGH_FUSE_BITS );
						}
						msgLength		=	4;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	fuseBits;
						msgBuffer[3]	=	STATUS_CMD_OK;
					}
					break;

	#ifndef REMOVE_PROGRAM_LOCK_BIT_SUPPORT
				case CMD_PROGRAM_LOCK_ISP:
					{
						unsigned char lockBits	=	msgBuffer[4];

						lockBits	=	(~lockBits) & 0x3C;	// mask BLBxx bits
						boot_lock_bits_set(lockBits);		// and program it
						boot_spm_busy_wait();

						msgLength		=	3;
						msgBuffer[1]	=	STATUS_CMD_OK;
						msgBuffer[2]	=	STATUS_CMD_OK;
					}
					break;
	#endif
				case CMD_CHIP_ERASE_ISP:
					eraseAddress	=	0;
					msgLength		=	2;
				//	msgBuffer[1]	=	STATUS_CMD_OK;
					msgBuffer[1]	=	STATUS_CMD_FAILED;	//*	isue 543, return FAILED instead of OK
					break;

					case CMD_LOAD_ADDRESS:
					{
							if(isAuthenticated == 1){
		#if defined(RAMPZ)
						address	=	( ((address_t)(msgBuffer[1])<<24)|((address_t)(msgBuffer[2])<<16)|((address_t)(msgBuffer[3])<<8)|(msgBuffer[4]) )<<1;
		#else
						address	=	( ((msgBuffer[3])<<8)|(msgBuffer[4]) )<<1;		//convert word to byte address
		#endif
						msgLength		=	2;
						msgBuffer[1]	=	STATUS_CMD_OK;
					}
					else
					{
						msgBuffer[1] 	=	STATUS_CMD_FAILED;
						msgLength		=	2;
					}
						break;
					}

					case CMD_PROGRAM_FLASH_ISP:
					case CMD_PROGRAM_EEPROM_ISP:
						{
								if(isAuthenticated == 1){
							unsigned int	size	=	((msgBuffer[1])<<8) | msgBuffer[2];
							unsigned char	*p	=	msgBuffer+10;
							// unsigned char *p = dummyArray;
							unsigned int	data;
							unsigned char	highByte, lowByte;
							address_t		tempaddress	=	address;

							if ( msgBuffer[0] == CMD_PROGRAM_FLASH_ISP )
							{
								// erase only main section (bootloader protection)
								if (eraseAddress < APP_END )
								{
									boot_page_erase(eraseAddress);	// Perform page erase
									boot_spm_busy_wait();		// Wait until the memory is erased.
									eraseAddress += SPM_PAGESIZE;	// point to next page to be erase
								}

								/* Write FLASH */
								do {
									lowByte		=	*p++;
									highByte 	=	*p++;

									data		=	(highByte << 8) | lowByte;
									boot_page_fill(address,data);

									address	=	address + 2;	// Select next word in memory
									size	-=	2;				// Reduce number of bytes to write by two
								} while (size);					// Loop until all bytes written

								boot_page_write(tempaddress);
								boot_spm_busy_wait();
								boot_rww_enable();				// Re-enable the RWW section
							}
							else
							{
								//*	issue 543, this should work, It has not been tested.
								uint16_t ii = address >> 1;
								/* write EEPROM */
								while (size) {
									eeprom_write_byte((uint8_t*)ii, *p++);
									address+=2;						// Select next EEPROM byte
									ii++;
									size--;
								}
							}
							msgLength		=	2;
							msgBuffer[1]	=	STATUS_CMD_OK;
						}
						else
						{
							msgBuffer[1] 	=	STATUS_CMD_FAILED;
							msgLength		=	2;
						}
						break;
						}

				case CMD_READ_FLASH_ISP:
				case CMD_READ_EEPROM_ISP:
					{
						unsigned int	size	=	((msgBuffer[1])<<8) | msgBuffer[2];
						unsigned char	*p		=	msgBuffer+1;
						msgLength				=	size+3;

						*p++	=	STATUS_CMD_OK;
						if (msgBuffer[0] == CMD_READ_FLASH_ISP )
						{
							unsigned int data;

							// Read FLASH
							do {
						//#if defined(RAMPZ)
						#if (FLASHEND > 0x10000)
								data	=	pgm_read_word_far(address);
						#else
								data	=	pgm_read_word_near(address);
						#endif
								*p++	=	(unsigned char)data;		//LSB
								*p++	=	(unsigned char)(data >> 8);	//MSB
								address	+=	2;							// Select next word in memory
								size	-=	2;
							}while (size);
						}
						else
						{
							/* Read EEPROM */
							do {
								EEARL	=	address;			// Setup EEPROM address
								EEARH	=	((address >> 8));
								address++;					// Select next EEPROM byte
								EECR	|=	(1<<EERE);			// Read EEPROM
								*p++	=	EEDR;				// Send EEPROM data
								size--;
							} while (size);
						}
						*p++	=	STATUS_CMD_OK;
					}
					break;

				default:
					msgLength		=	2;
					msgBuffer[1]	=	STATUS_CMD_FAILED;
					break;
			}

			/*
			 * Now send answer message back
			 */

			 // encryption kismi burada olmali

// aes_encrypt(receivedPacket, aes_buffer, packetSize);
			receivedPacketIndex = 0;

			receivedPacket[receivedPacketIndex++] = MESSAGE_START;

			checksum	=	MESSAGE_START^0;
			receivedPacket[receivedPacketIndex++] = seqNum;
			checksum	^=	seqNum;

			c			=	((msgLength>>8)&0xFF);
			receivedPacket[receivedPacketIndex++] = c;
			checksum	^=	c;

			c			=	msgLength&0x00FF;
			receivedPacket[receivedPacketIndex++] = c;
			checksum ^= c;
			residualNumber = (msgLength+6) % 16;

			finalResponseSize = ((msgLength+(16-residualNumber)+6));

			if(residualNumber != 0)
			{

				for(int excessiveNumberIndex = (msgLength+6); excessiveNumberIndex<finalResponseSize; excessiveNumberIndex++)
				{

					receivedPacket[excessiveNumberIndex] = 0xff;
				}

			}

			receivedPacket[receivedPacketIndex++] = TOKEN;
			checksum ^= TOKEN;
			p	=	msgBuffer;
			while ( msgLength )
			{
				c	=	*p++;
				receivedPacket[receivedPacketIndex++] = c;
				checksum ^=c;
				msgLength--;
			}

			receivedPacket[receivedPacketIndex++] = checksum;
			seqNum++;







			aes_encrypt(aes_buffer, receivedPacket, finalResponseSize);
			sendchar(SOTA_MESSAGE_START);
			sendchar((finalResponseSize>>8)&0xFF);
			sendchar(finalResponseSize&0x00FF);
			for(int i =0; i<finalResponseSize; i++)
			sendchar(aes_buffer[i]);

		#ifndef REMOVE_BOOTLOADER_LED
			//*	<MLS>	toggle the LED
			PROGLED_PORT	^=	(1<<PROGLED_PIN);	// active high LED ON
		#endif

		}
	}



#ifdef _DEBUG_WITH_LEDS_
	//*	this is for debugging it can be removed
	for (ii=0; ii<10; ii++)
	{
		PROGLED_PORT	&=	~(1<<PROGLED_PIN);	// turn LED off
		delay_ms(200);
		PROGLED_PORT	|=	(1<<PROGLED_PIN);	// turn LED on
		delay_ms(200);
	}
	PROGLED_PORT	&=	~(1<<PROGLED_PIN);	// turn LED off
#endif

#ifdef _DEBUG_SERIAL_
	sendchar('j');
//	sendchar('u');
//	sendchar('m');
//	sendchar('p');
//	sendchar(' ');
//	sendchar('u');
//	sendchar('s');
//	sendchar('r');
	sendchar(0x0d);
	sendchar(0x0a);

	delay_ms(100);
#endif


#ifndef REMOVE_BOOTLOADER_LED
	PROGLED_DDR		&=	~(1<<PROGLED_PIN);	// set to default
	PROGLED_PORT	&=	~(1<<PROGLED_PIN);	// active low LED OFF
//	PROGLED_PORT	|=	(1<<PROGLED_PIN);	// active high LED OFf
	delay_ms(100);							// delay after exit
#endif


	asm volatile ("nop");			// wait until port has changed

	/*
	 * Now leave bootloader
	 */

	UART_STATUS_REG	&=	0xfd;
	boot_rww_enable();				// enable application section


	asm volatile(
			"clr	r30		\n\t"
			"clr	r31		\n\t"
			"ijmp	\n\t"
			);
//	asm volatile ( "push r1" "\n\t"		// Jump to Reset vector in Application Section
//					"push r1" "\n\t"
//					"ret"	 "\n\t"
//					::);

	 /*
	 * Never return to stop GCC to generate exit return code
	 * Actually we will never reach this point, but the compiler doesn't
	 * understand this
	 */
	for(;;);
}

/*
base address = f800

avrdude: Device signature = 0x1e9703
avrdude: safemode: lfuse reads as FF
avrdude: safemode: hfuse reads as DA
avrdude: safemode: efuse reads as F5
avrdude>


base address = f000
avrdude: Device signature = 0x1e9703
avrdude: safemode: lfuse reads as FF
avrdude: safemode: hfuse reads as D8
avrdude: safemode: efuse reads as F5
avrdude>
*/

//************************************************************************
#ifdef ENABLE_MONITOR
#include	<math.h>

unsigned long	gRamIndex;
unsigned long	gFlashIndex;
unsigned long	gEepromIndex;


#define	true	1
#define	false	0

#include	"avr_cpunames.h"

#ifndef _AVR_CPU_NAME_
	#error cpu name not defined
#endif

#ifdef _VECTORS_SIZE
	#define	kInterruptVectorCount (_VECTORS_SIZE / 4)
#else
	#define	kInterruptVectorCount 23
#endif


// void	PrintDecInt(int theNumber, int digitCnt);

#ifdef _AVR_CPU_NAME_
	const char	gTextMsg_CPU_Name[]			PROGMEM	=	_AVR_CPU_NAME_;
#else
	const char	gTextMsg_CPU_Name[]			PROGMEM	=	"UNKNOWN";
#endif

	const char	gTextMsg_Explorer[]			PROGMEM	=	"Arduino explorer stk500V2 by MLS";
	const char	gTextMsg_Prompt[]			PROGMEM	=	"Bootloader>";
	const char	gTextMsg_HUH[]				PROGMEM	=	"Huh?";
	const char	gTextMsg_COMPILED_ON[]		PROGMEM	=	"Compiled on = ";
	const char	gTextMsg_CPU_Type[]			PROGMEM	=	"CPU Type    = ";
	const char	gTextMsg_AVR_ARCH[]			PROGMEM	=	"__AVR_ARCH__= ";
	const char	gTextMsg_AVR_LIBC[]			PROGMEM	=	"AVR LibC Ver= ";
	const char	gTextMsg_GCC_VERSION[]		PROGMEM	=	"GCC Version = ";
	const char	gTextMsg_CPU_SIGNATURE[]	PROGMEM	=	"CPU ID      = ";
	const char	gTextMsg_FUSE_BYTE_LOW[]	PROGMEM	=	"Low fuse    = ";
	const char	gTextMsg_FUSE_BYTE_HIGH[]	PROGMEM	=	"High fuse   = ";
	const char	gTextMsg_FUSE_BYTE_EXT[]	PROGMEM	=	"Ext fuse    = ";
	const char	gTextMsg_FUSE_BYTE_LOCK[]	PROGMEM	=	"Lock fuse   = ";
	const char	gTextMsg_GCC_DATE_STR[]		PROGMEM	=	__DATE__;
	const char	gTextMsg_AVR_LIBC_VER_STR[]	PROGMEM	=	__AVR_LIBC_VERSION_STRING__;
	const char	gTextMsg_GCC_VERSION_STR[]	PROGMEM	=	__VERSION__;
	const char	gTextMsg_VECTOR_HEADER[]	PROGMEM	=	"V#   ADDR   op code     instruction addr   Interrupt";
	const char	gTextMsg_noVector[]			PROGMEM	=	"no vector";
	const char	gTextMsg_rjmp[]				PROGMEM	=	"rjmp  ";
	const char	gTextMsg_jmp[]				PROGMEM	=	"jmp ";
	const char	gTextMsg_WHAT_PORT[]		PROGMEM	=	"What port:";
	const char	gTextMsg_PortNotSupported[]	PROGMEM	=	"Port not supported";
	const char	gTextMsg_MustBeLetter[]		PROGMEM	=	"Must be a letter";
	const char	gTextMsg_SPACE[]			PROGMEM	=	" ";
	const char	gTextMsg_WriteToEEprom[]	PROGMEM	=	"Writting EE";
	const char	gTextMsg_ReadingEEprom[]	PROGMEM	=	"Reading EE";
	const char	gTextMsg_EEPROMerrorCnt[]	PROGMEM	=	"EE err cnt=";
	const char	gTextMsg_PORT[]				PROGMEM	=	"PORT";


//************************************************************************
//*	Help messages
	const char	gTextMsg_HELP_MSG_0[]		PROGMEM	=	"0=Zero addr";
	const char	gTextMsg_HELP_MSG_QM[]		PROGMEM	=	"?=CPU stats";
	const char	gTextMsg_HELP_MSG_AT[]		PROGMEM	=	"@=EEPROM test";
	const char	gTextMsg_HELP_MSG_B[]		PROGMEM	=	"B=Blink LED";
	const char	gTextMsg_HELP_MSG_E[]		PROGMEM	=	"E=Dump EEPROM";
	const char	gTextMsg_HELP_MSG_F[]		PROGMEM	=	"F=Dump FLASH";
	const char	gTextMsg_HELP_MSG_H[]		PROGMEM	=	"H=Help";
	const char	gTextMsg_HELP_MSG_L[]		PROGMEM	=	"L=List I/O Ports";
//	const char	gTextMsg_HELP_MSG_Q[]		PROGMEM	=	"Q=Quit & jump to user pgm";
	const char	gTextMsg_HELP_MSG_Q[]		PROGMEM	=	"Q=Quit";
	const char	gTextMsg_HELP_MSG_R[]		PROGMEM	=	"R=Dump RAM";
	const char	gTextMsg_HELP_MSG_V[]		PROGMEM	=	"V=show interrupt Vectors";
	const char	gTextMsg_HELP_MSG_Y[]		PROGMEM	=	"Y=Port blink";

	const char	gTextMsg_END[]				PROGMEM	=	"*";


//************************************************************************
void	PrintFromPROGMEM(const void *dataPtr, unsigned char offset)
{
char	theChar;

	dataPtr		+=	offset;

	do {
	#if (FLASHEND > 0x10000)
		theChar	=	pgm_read_byte_far((uint16_t)dataPtr++);
	#else
		theChar	=	pgm_read_byte_near((uint16_t)dataPtr++);
	#endif
		if (theChar != 0)
		{
			sendchar(theChar);
		}
	} while (theChar != 0);
}

//************************************************************************
void	PrintNewLine(void)
{
	sendchar(0x0d);
	sendchar(0x0a);
}


//************************************************************************
void	PrintFromPROGMEMln(const void *dataPtr, unsigned char offset)
{
	PrintFromPROGMEM(dataPtr, offset);

	PrintNewLine();
}


//************************************************************************
void	PrintString(char *textString)
{
char	theChar;
int		ii;

	theChar		=	1;
	ii			=	0;
	while (theChar != 0)
	{
		theChar	=	textString[ii];
		if (theChar != 0)
		{
			sendchar(theChar);
		}
		ii++;
	}
}

//************************************************************************
void	PrintHexByte(unsigned char theByte)
{
char	theChar;

	theChar	=	0x30 + ((theByte >> 4) & 0x0f);
	if (theChar > 0x39)
	{
		theChar	+=	7;
	}
	sendchar(theChar );

	theChar	=	0x30 + (theByte & 0x0f);
	if (theChar > 0x39)
	{
		theChar	+=	7;
	}
	sendchar(theChar );
}

//************************************************************************
// void	PrintDecInt(int theNumber, int digitCnt)
// {
// int	theChar;
// int	myNumber;
//
// 	myNumber	=	theNumber;
//
// 	if ((myNumber > 100) || (digitCnt >= 3))
// 	{
// 		theChar		=	0x30 + myNumber / 100;
// 		sendchar(theChar );
// 	}
//
// 	if ((myNumber > 10) || (digitCnt >= 2))
// 	{
// 		theChar	=	0x30  + ((myNumber % 100) / 10 );
// 		sendchar(theChar );
// 	}
// 	theChar	=	0x30 + (myNumber % 10);
// 	sendchar(theChar );
// }




//************************************************************************
static void	PrintCPUstats(void)
{
unsigned char fuseByte;

	PrintFromPROGMEMln(gTextMsg_Explorer, 0);

	PrintFromPROGMEM(gTextMsg_COMPILED_ON, 0);
	PrintFromPROGMEMln(gTextMsg_GCC_DATE_STR, 0);

	PrintFromPROGMEM(gTextMsg_CPU_Type, 0);
	PrintFromPROGMEMln(gTextMsg_CPU_Name, 0);

	PrintFromPROGMEM(gTextMsg_AVR_ARCH, 0);
	PrintDecInt(__AVR_ARCH__, 1);
	PrintNewLine();

	PrintFromPROGMEM(gTextMsg_GCC_VERSION, 0);
	PrintFromPROGMEMln(gTextMsg_GCC_VERSION_STR, 0);

	//*	these can be found in avr/version.h
	PrintFromPROGMEM(gTextMsg_AVR_LIBC, 0);
	PrintFromPROGMEMln(gTextMsg_AVR_LIBC_VER_STR, 0);

#if defined(SIGNATURE_0)
	PrintFromPROGMEM(gTextMsg_CPU_SIGNATURE, 0);
	//*	these can be found in avr/iomxxx.h
	PrintHexByte(SIGNATURE_0);
	PrintHexByte(SIGNATURE_1);
	PrintHexByte(SIGNATURE_2);
	PrintNewLine();
#endif


#if defined(GET_LOW_FUSE_BITS)
	//*	fuse settings
	PrintFromPROGMEM(gTextMsg_FUSE_BYTE_LOW, 0);
	fuseByte	=	boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
	PrintHexByte(fuseByte);
	PrintNewLine();

	PrintFromPROGMEM(gTextMsg_FUSE_BYTE_HIGH, 0);
	fuseByte	=	boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
	PrintHexByte(fuseByte);
	PrintNewLine();

	PrintFromPROGMEM(gTextMsg_FUSE_BYTE_EXT, 0);
	fuseByte	=	boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
	PrintHexByte(fuseByte);
	PrintNewLine();

	PrintFromPROGMEM(gTextMsg_FUSE_BYTE_LOCK, 0);
	fuseByte	=	boot_lock_fuse_bits_get(GET_LOCK_BITS);
	PrintHexByte(fuseByte);
	PrintNewLine();

#endif

}


//************************************************************************
static void BlinkLED(void)
{
	PROGLED_DDR		|=	(1<<PROGLED_PIN);
	PROGLED_PORT	|=	(1<<PROGLED_PIN);	// active high LED ON

	while (!Serial_Available())
	{
		PROGLED_PORT	&=	~(1<<PROGLED_PIN);	// turn LED off
		delay_ms(100);
		PROGLED_PORT	|=	(1<<PROGLED_PIN);	// turn LED on
		delay_ms(100);
	}
	recchar();	//	get the char out of the buffer
}

enum
{
	kDUMP_FLASH	=	0,
	kDUMP_EEPROM,
	kDUMP_RAM
};

//************************************************************************
static void	DumpHex(unsigned char dumpWhat, unsigned long startAddress, unsigned char numRows)
{
unsigned long	myAddressPointer;
uint8_t			ii;
unsigned char	theValue;
char			asciiDump[18];
unsigned char	*ramPtr;


	ramPtr				=	0;
	theValue			=	0;
	myAddressPointer	=	startAddress;
	while (numRows > 0)
	{
		if (myAddressPointer > 0x10000)
		{
			PrintHexByte((myAddressPointer >> 16) & 0x00ff);
		}
		PrintHexByte((myAddressPointer >> 8) & 0x00ff);
		PrintHexByte(myAddressPointer & 0x00ff);
		sendchar(0x20);
		sendchar('-');
		sendchar(0x20);

		asciiDump[0]		=	0;
		for (ii=0; ii<16; ii++)
		{
			switch(dumpWhat)
			{
				case kDUMP_FLASH:
				#if (FLASHEND > 0x10000)
					theValue	=	pgm_read_byte_far(myAddressPointer);
				#else
					theValue	=	pgm_read_byte_near(myAddressPointer);
				#endif
					break;

				case kDUMP_EEPROM:
					theValue	=	eeprom_read_byte((uint8_t *)(uint16_t)myAddressPointer);
					break;

				case kDUMP_RAM:
					theValue	=	ramPtr[myAddressPointer];
					break;

			}
			PrintHexByte(theValue);
			sendchar(0x20);
			if ((theValue >= 0x20) && (theValue < 0x7f))
			{
				asciiDump[ii % 16]	=	theValue;
			}
			else
			{
				asciiDump[ii % 16]	=	'.';
			}

			myAddressPointer++;
		}
		asciiDump[16]	=	0;
		PrintString(asciiDump);
		PrintNewLine();

		numRows--;
	}
}



//************************************************************************
//*	returns amount of extended memory
static void	EEPROMtest(void)
{
int		ii;
char	theChar;
char	theEEPROMchar;
int		errorCount;

	PrintFromPROGMEMln(gTextMsg_WriteToEEprom, 0);
	PrintNewLine();
	ii			=	0;
#if (FLASHEND > 0x10000)
	while (((theChar = pgm_read_byte_far(((uint16_t)gTextMsg_Explorer) + ii)) != '*') && (ii < 512))
#else
	while (((theChar = pgm_read_byte_near(((uint16_t)gTextMsg_Explorer) + ii)) != '*') && (ii < 512))
#endif
	{
		eeprom_write_byte((uint8_t *)ii, theChar);
		if (theChar == 0)
		{
			PrintFromPROGMEM(gTextMsg_SPACE, 0);
		}
		else
		{
			sendchar(theChar);
		}
		ii++;
	}

	//*	no go back through and test
	PrintNewLine();
	PrintNewLine();
	PrintFromPROGMEMln(gTextMsg_ReadingEEprom, 0);
	PrintNewLine();
	errorCount	=	0;
	ii			=	0;
#if (FLASHEND > 0x10000)
	while (((theChar = pgm_read_byte_far((uint16_t)gTextMsg_Explorer + ii)) != '*') && (ii < 512))
#else
	while (((theChar = pgm_read_byte_near((uint16_t)gTextMsg_Explorer + ii)) != '*') && (ii < 512))
#endif
	{
		theEEPROMchar	=	eeprom_read_byte((uint8_t *)ii);
		if (theEEPROMchar == 0)
		{
			PrintFromPROGMEM(gTextMsg_SPACE, 0);
		}
		else
		{
			sendchar(theEEPROMchar);
		}
		if (theEEPROMchar != theChar)
		{
			errorCount++;
		}
		ii++;
	}
	PrintNewLine();
	PrintNewLine();
	PrintFromPROGMEM(gTextMsg_EEPROMerrorCnt, 0);
	PrintDecInt(errorCount, 1);
	PrintNewLine();
	PrintNewLine();

	gEepromIndex	=	0;	//*	set index back to zero for next eeprom dump

}



#if (FLASHEND > 0x08000)
//*	this includes the interrupt names for the monitor portion. There is no longer enough
//*	memory to include this
//	#include	"avrinterruptnames.h"
//	#ifndef _INTERRUPT_NAMES_DEFINED_
//		#warning Interrupt vectors not defined
//	#endif
#endif

//************************************************************************
static void	VectorDisplay(void)
{
unsigned long	byte1;
unsigned long	byte2;
unsigned long	byte3;
unsigned long	byte4;
unsigned long	word1;
unsigned long	word2;
int				vectorIndex;
unsigned long	myMemoryPtr;
unsigned long	wordMemoryAddress;
unsigned long	realitiveAddr;
unsigned long	myFullAddress;
unsigned long	absoluteAddr;
#if defined(_INTERRUPT_NAMES_DEFINED_)
	long		stringPointer;
#endif

	myMemoryPtr		=	0;
	vectorIndex		=	0;
	PrintFromPROGMEMln(gTextMsg_CPU_Name, 0);
	PrintFromPROGMEMln(gTextMsg_VECTOR_HEADER, 0);
	//					 V#   ADDR   op code
	//					  1 - 0000 = C3 BB 00 00 rjmp 03BB >000776 RESET
	while (vectorIndex < kInterruptVectorCount)
	{
		wordMemoryAddress	=	myMemoryPtr / 2;
		//					 01 - 0000 = 12 34
		PrintDecInt(vectorIndex + 1, 2);
		sendchar(0x20);
		sendchar('-');
		sendchar(0x20);
		PrintHexByte((wordMemoryAddress >> 8) & 0x00ff);
		PrintHexByte((wordMemoryAddress) & 0x00ff);
		sendchar(0x20);
		sendchar('=');
		sendchar(0x20);


		//*	the AVR is LITTLE ENDIAN, swap the byte order
	#if (FLASHEND > 0x10000)
		byte1	=	pgm_read_byte_far(myMemoryPtr++);
		byte2	=	pgm_read_byte_far(myMemoryPtr++);
		byte3	=	pgm_read_byte_far(myMemoryPtr++);
		byte4	=	pgm_read_byte_far(myMemoryPtr++);
	#else
		byte1	=	pgm_read_byte_near(myMemoryPtr++);
		byte2	=	pgm_read_byte_near(myMemoryPtr++);
		byte3	=	pgm_read_byte_near(myMemoryPtr++);
		byte4	=	pgm_read_byte_near(myMemoryPtr++);
	#endif
		word1	=	(byte2 << 8) + byte1;
		word2	=	(byte4 << 8) + byte3;


		PrintHexByte(byte2);
		sendchar(0x20);
		PrintHexByte(byte1);
		sendchar(0x20);
		PrintHexByte(byte4);
		sendchar(0x20);
		PrintHexByte(byte3);
		sendchar(0x20);

		if (word1 == 0xffff)
		{
			PrintFromPROGMEM(gTextMsg_noVector, 0);
		}
		else if ((word1 & 0xc000) == 0xc000)
		{
			//*	rjmp instruction
			realitiveAddr	=	word1 & 0x3FFF;
			absoluteAddr	=	wordMemoryAddress + realitiveAddr;	//*	add the offset to the current address
			absoluteAddr	=	absoluteAddr << 1;					//*	multiply by 2 for byte address

			PrintFromPROGMEM(gTextMsg_rjmp, 0);
			PrintHexByte((realitiveAddr >> 8) & 0x00ff);
			PrintHexByte((realitiveAddr) & 0x00ff);
			sendchar(0x20);
			sendchar('>');
			PrintHexByte((absoluteAddr >> 16) & 0x00ff);
			PrintHexByte((absoluteAddr >> 8) & 0x00ff);
			PrintHexByte((absoluteAddr) & 0x00ff);

		}
		else if ((word1 & 0xfE0E) == 0x940c)
		{
			//*	jmp instruction, this is REALLY complicated, refer to the instruction manual (JMP)
			myFullAddress	=	((byte1 & 0x01) << 16) +
								((byte1 & 0xf0) << 17) +
								((byte2 & 0x01) << 21) +
								word2;

			absoluteAddr	=	myFullAddress << 1;

			PrintFromPROGMEM(gTextMsg_jmp, 0);
			PrintHexByte((myFullAddress >> 16) & 0x00ff);
			PrintHexByte((myFullAddress >> 8) & 0x00ff);
			PrintHexByte((myFullAddress) & 0x00ff);
			sendchar(0x20);
			sendchar('>');
			PrintHexByte((absoluteAddr >> 16) & 0x00ff);
			PrintHexByte((absoluteAddr >> 8) & 0x00ff);
			PrintHexByte((absoluteAddr) & 0x00ff);
		}

	#if defined(_INTERRUPT_NAMES_DEFINED_)
		sendchar(0x20);
	#if (FLASHEND > 0x10000)
		stringPointer	=	pgm_read_word_far(&(gInterruptNameTable[vectorIndex]));
	#else
		stringPointer	=	pgm_read_word_near(&(gInterruptNameTable[vectorIndex]));
	#endif
		PrintFromPROGMEM((char *)stringPointer, 0);
	#endif
		PrintNewLine();

		vectorIndex++;
	}
}

//************************************************************************
static void	PrintAvailablePort(char thePortLetter)
{
	PrintFromPROGMEM(gTextMsg_PORT, 0);
	sendchar(thePortLetter);
	PrintNewLine();
}

//************************************************************************
static void	ListAvailablePorts(void)
{

#ifdef DDRA
	PrintAvailablePort('A');
#endif

#ifdef DDRB
	PrintAvailablePort('B');
#endif

#ifdef DDRC
	PrintAvailablePort('C');
#endif

#ifdef DDRD
	PrintAvailablePort('D');
#endif

#ifdef DDRE
	PrintAvailablePort('E');
#endif

#ifdef DDRF
	PrintAvailablePort('F');
#endif

#ifdef DDRG
	PrintAvailablePort('G');
#endif

#ifdef DDRH
	PrintAvailablePort('H');
#endif

#ifdef DDRI
	PrintAvailablePort('I');
#endif

#ifdef DDRJ
	PrintAvailablePort('J');
#endif

#ifdef DDRK
	PrintAvailablePort('K');
#endif

#ifdef DDRL
	PrintAvailablePort('L');
#endif

}

//************************************************************************
static void	AVR_PortOutput(void)
{
char	portLetter;
char	getCharFlag;

	PrintFromPROGMEM(gTextMsg_WHAT_PORT, 0);

	portLetter	=	recchar();
	portLetter	=	portLetter & 0x5f;
	sendchar(portLetter);
	PrintNewLine();

	if ((portLetter >= 'A') && (portLetter <= 'Z'))
	{
		getCharFlag	=	true;
		switch(portLetter)
		{
		#ifdef DDRA
			case 'A':
				DDRA	=	0xff;
				while (!Serial_Available())
				{
					PORTA	^=	0xff;
					delay_ms(200);
				}
				PORTA	=	0;
				break;
		#endif

		#ifdef DDRB
			case 'B':
				DDRB	=	0xff;
				while (!Serial_Available())
				{
					PORTB	^=	0xff;
					delay_ms(200);
				}
				PORTB	=	0;
				break;
		#endif

		#ifdef DDRC
			case 'C':
				DDRC	=	0xff;
				while (!Serial_Available())
				{
					PORTC	^=	0xff;
					delay_ms(200);
				}
				PORTC	=	0;
				break;
		#endif

		#ifdef DDRD
			case 'D':
				DDRD	=	0xff;
				while (!Serial_Available())
				{
					PORTD	^=	0xff;
					delay_ms(200);
				}
				PORTD	=	0;
				break;
		#endif

		#ifdef DDRE
			case 'E':
				DDRE	=	0xff;
				while (!Serial_Available())
				{
					PORTE	^=	0xff;
					delay_ms(200);
				}
				PORTE	=	0;
				break;
		#endif

		#ifdef DDRF
			case 'F':
				DDRF	=	0xff;
				while (!Serial_Available())
				{
					PORTF	^=	0xff;
					delay_ms(200);
				}
				PORTF	=	0;
				break;
		#endif

		#ifdef DDRG
			case 'G':
				DDRG	=	0xff;
				while (!Serial_Available())
				{
					PORTG	^=	0xff;
					delay_ms(200);
				}
				PORTG	=	0;
				break;
		#endif

		#ifdef DDRH
			case 'H':
				DDRH	=	0xff;
				while (!Serial_Available())
				{
					PORTH	^=	0xff;
					delay_ms(200);
				}
				PORTH	=	0;
				break;
		#endif

		#ifdef DDRI
			case 'I':
				DDRI	=	0xff;
				while (!Serial_Available())
				{
					PORTI	^=	0xff;
					delay_ms(200);
				}
				PORTI	=	0;
				break;
		#endif

		#ifdef DDRJ
			case 'J':
				DDRJ	=	0xff;
				while (!Serial_Available())
				{
					PORTJ	^=	0xff;
					delay_ms(200);
				}
				PORTJ	=	0;
				break;
		#endif

		#ifdef DDRK
			case 'K':
				DDRK	=	0xff;
				while (!Serial_Available())
				{
					PORTK	^=	0xff;
					delay_ms(200);
				}
				PORTK	=	0;
				break;
		#endif

		#ifdef DDRL
			case 'L':
				DDRL	=	0xff;
				while (!Serial_Available())
				{
					PORTL	^=	0xff;
					delay_ms(200);
				}
				PORTL	=	0;
				break;
		#endif

			default:
				PrintFromPROGMEMln(gTextMsg_PortNotSupported, 0);
				getCharFlag	=	false;
				break;
		}
		if (getCharFlag)
		{
			recchar();
		}
	}
	else
	{
		PrintFromPROGMEMln(gTextMsg_MustBeLetter, 0);
	}
}


//*******************************************************************
static void PrintHelp(void)
{
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_0, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_QM, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_AT, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_B, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_E, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_F, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_H, 0);

	PrintFromPROGMEMln(gTextMsg_HELP_MSG_L, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_Q, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_R, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_V, 0);
	PrintFromPROGMEMln(gTextMsg_HELP_MSG_Y, 0);
}

//************************************************************************
static void	RunMonitor(void)
{
char			keepGoing;
unsigned char	theChar;
int				ii, jj;

	for (ii=0; ii<5; ii++)
	{
		for (jj=0; jj<25; jj++)
		{
			sendchar('!');
		}
		PrintNewLine();
	}

	gRamIndex			=	0;
	gFlashIndex			=	0;
	gEepromIndex		=	0;

	PrintFromPROGMEMln(gTextMsg_Explorer, 0);

	keepGoing	=	1;
	while (keepGoing)
	{
		PrintFromPROGMEM(gTextMsg_Prompt, 0);
		theChar	=	recchar();
		if (theChar >= 0x60)
		{
			theChar	=	theChar & 0x5F;
		}

		if (theChar >= 0x20)
		{
			sendchar(theChar);
			sendchar(0x20);
		}

		switch(theChar)
		{
			case '0':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_0, 2);
				gFlashIndex		=	0;
				gRamIndex		=	0;
				gEepromIndex	=	0;
				break;

			case '?':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_QM, 2);
				PrintCPUstats();
				break;

			case '@':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_AT, 2);
				EEPROMtest();
				break;

			case 'B':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_B, 2);
				BlinkLED();
				break;

			case 'E':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_E, 2);
				DumpHex(kDUMP_EEPROM, gEepromIndex, 16);
				gEepromIndex	+=	256;
				if (gEepromIndex > E2END)
				{
					gEepromIndex	=	0;
				}
				break;

			case 'F':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_F, 2);
				DumpHex(kDUMP_FLASH, gFlashIndex, 16);
				gFlashIndex	+=	256;
				break;

			case 'H':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_H, 2);
				PrintHelp();
				break;

			case 'L':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_L, 2);
				ListAvailablePorts();
				break;

			case 'Q':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_Q, 2);
				keepGoing	=	false;
				break;

			case 'R':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_R, 2);
				DumpHex(kDUMP_RAM, gRamIndex, 16);
				gRamIndex	+=	256;
				break;

			case 'V':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_V, 2);
				VectorDisplay();
				break;

			case 'Y':
				PrintFromPROGMEMln(gTextMsg_HELP_MSG_Y, 2);
				AVR_PortOutput();
				break;

			default:
				PrintFromPROGMEMln(gTextMsg_HUH, 0);
				break;
		}
	}
}

#endif
