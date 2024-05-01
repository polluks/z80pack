/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 1987-2024 Udo Munk
 * Copyright (C) 2021 David McNaughton
 * Copyright (C) 2022-2024 Thomas Eberhardt
 */

/*
 *	This module contains the global variables other than memory management
 */

#include <stddef.h>
#include "sim.h"
#include "simglb.h"

#define MAXCHAN	5	/* max number of channels for I/O busy detect */

/*
 *	Type of CPU, either Z80 or 8080
 */
int cpu = DEF_CPU;

/*
 *	CPU Registers
 */
BYTE A, B, C, D, E, H, L;	/* primary registers */
int  F;				/* normally 8-Bit, but int is faster */
#ifndef EXCLUDE_Z80
WORD IX, IY;			/* Z80 index registers */
BYTE A_, B_, C_, D_, E_, H_, L_; /* Z80 alternate registers */
BYTE I;				/* Z80 interrupt register */
BYTE R;				/* Z80 refresh register (7-bit counter) */
BYTE R_;			/* 8th bit of R (can be loaded with LD R,A) */
int  F_;
#endif
WORD PC;			/* program counter */
WORD SP;			/* stack pointer */
BYTE IFF;			/* interrupt flags */
Tstates_t T;			/* CPU clock */
unsigned long long cpu_start, cpu_stop; /* timestamps in ms */

#ifdef BUS_8080
BYTE cpu_bus;			/* CPU bus status, for frontpanels */
int m1_step;			/* flag for waiting at M1 in single step */
#endif

BYTE io_port;			/* I/O port used */
BYTE io_data;			/* data on I/O port */
int busy_loop_cnt[MAXCHAN];	/* counters for I/O busy loop detection */

BYTE cpu_state;			/* state of CPU emulation */
int cpu_error;			/* error status of CPU emulation */
#ifndef EXCLUDE_Z80
int int_mode;			/* CPU interrupt mode (IM 0, IM 1, IM 2) */
int int_nmi;			/* non-maskable interrupt request */
#endif
int int_int;			/* interrupt request */
int int_data = -1;		/* data from interrupting device on data bus */
int int_protection;		/* to delay interrupts after EI */
BYTE bus_request;		/* request address/data bus from CPU */
BusDMA_t bus_mode;		/* current bus mode for DMA */
Tstates_t (*dma_bus_master)(BYTE bus_ack); /* DMA bus master call back func */
int tmax;			/* max t-states to execute in 10ms */
int cpu_needed;			/* don't adjust CPU freq if needed */

/*
 *	Variables for history memory
 */
#ifdef HISIZE
struct history his[HISIZE];	/* memory to hold trace information */
int h_next;			/* index into trace memory */
int h_flag;			/* flag for trace memory overrun */
#endif

/*
 *	Variables for breakpoint memory
 */
#ifdef SBSIZE
struct softbreak soft[SBSIZE];	/* memory to hold breakpoint information */
int sb_next;			/* index into breakpoint memory */
#endif

/*
 *	Variables for runtime measurement
 */
long t_states;			/* number of counted T states */
int t_flag;			/* flag, 1 = on, 0 = off */
WORD t_start = 65535;		/* start address for measurement */
WORD t_end = 65535;		/* end address for measurement */

/*
 *	Variables for frontpanel emulation
 */
#ifdef FRONTPANEL
unsigned long long fp_clock;	/* simulation clock */
float fp_fps = 30.0;		/* frame rate, default 30 usually works */
WORD fp_led_address;		/* lights for address bus */
BYTE fp_led_data;		/* lights for data bus */
WORD address_switch;		/* address and programmed input switches */
BYTE fp_led_output = 0xff;	/* inverted IMSAI/Cromemco programmed output */
#endif

/*
 *	Flags to control operation of simulation
 */
int s_flag;			/* flag for -s option */
int l_flag;			/* flag for -l option */
int m_flag = -1;		/* flag for -m option */
int x_flag;			/* flag for -x option */
int i_flag;			/* flag for -i option */
int f_flag;			/* flag for -f option */
int u_flag;			/* flag for -u option */
int r_flag;			/* flag for -r option */
int c_flag;			/* flag for -c option */
int M_flag = 0;			/* flag for -M option */
int R_flag = 0;			/* flag for -R option */

/*
 *	Variables for configuration and disk images
 */
char xfn[MAX_LFN];		/* buffer for filename (option -x) */
char *diskdir = NULL;		/* path for disk images (option -d) */
char diskd[MAX_LFN];		/* disk image directory in use */
char confdir[MAX_LFN];		/* path for configuration files */
char conffn[MAX_LFN];		/* configuration file (option -c) */
char rompath[MAX_LFN];		/* path for boot ROM files */

/*
 *	Precompiled table to get parity as fast as possible
 */
char parity[256] = {
	0 /* 00000000 */, 1 /* 00000001 */, 1 /* 00000010 */, 0 /* 00000011 */,
	1 /* 00000100 */, 0 /* 00000101 */, 0 /* 00000110 */, 1 /* 00000111 */,
	1 /* 00001000 */, 0 /* 00001001 */, 0 /* 00001010 */, 1 /* 00001011 */,
	0 /* 00001100 */, 1 /* 00001101 */, 1 /* 00001110 */, 0 /* 00001111 */,
	1 /* 00010000 */, 0 /* 00010001 */, 0 /* 00010010 */, 1 /* 00010011 */,
	0 /* 00010100 */, 1 /* 00010101 */, 1 /* 00010110 */, 0 /* 00010111 */,
	0 /* 00011000 */, 1 /* 00011001 */, 1 /* 00011010 */, 0 /* 00011011 */,
	1 /* 00011100 */, 0 /* 00011101 */, 0 /* 00011110 */, 1 /* 00011111 */,
	1 /* 00100000 */, 0 /* 00100001 */, 0 /* 00100010 */, 1 /* 00100011 */,
	0 /* 00100100 */, 1 /* 00100101 */, 1 /* 00100110 */, 0 /* 00100111 */,
	0 /* 00101000 */, 1 /* 00101001 */, 1 /* 00101010 */, 0 /* 00101011 */,
	1 /* 00101100 */, 0 /* 00101101 */, 0 /* 00101110 */, 1 /* 00101111 */,
	0 /* 00110000 */, 1 /* 00110001 */, 1 /* 00110010 */, 0 /* 00110011 */,
	1 /* 00110100 */, 0 /* 00110101 */, 0 /* 00110110 */, 1 /* 00110111 */,
	1 /* 00111000 */, 0 /* 00111001 */, 0 /* 00111010 */, 1 /* 00111011 */,
	0 /* 00111100 */, 1 /* 00111101 */, 1 /* 00111110 */, 0 /* 00111111 */,
	1 /* 01000000 */, 0 /* 01000001 */, 0 /* 01000010 */, 1 /* 01000011 */,
	0 /* 01000100 */, 1 /* 01000101 */, 1 /* 01000110 */, 0 /* 01000111 */,
	0 /* 01001000 */, 1 /* 01001001 */, 1 /* 01001010 */, 0 /* 01001011 */,
	1 /* 01001100 */, 0 /* 01001101 */, 0 /* 01001110 */, 1 /* 01001111 */,
	0 /* 01010000 */, 1 /* 01010001 */, 1 /* 01010010 */, 0 /* 01010011 */,
	1 /* 01010100 */, 0 /* 01010101 */, 0 /* 01010110 */, 1 /* 01010111 */,
	1 /* 01011000 */, 0 /* 01011001 */, 0 /* 01011010 */, 1 /* 01011011 */,
	0 /* 01011100 */, 1 /* 01011101 */, 1 /* 01011110 */, 0 /* 01011111 */,
	0 /* 01100000 */, 1 /* 01100001 */, 1 /* 01100010 */, 0 /* 01100011 */,
	1 /* 01100100 */, 0 /* 01100101 */, 0 /* 01100110 */, 1 /* 01100111 */,
	1 /* 01101000 */, 0 /* 01101001 */, 0 /* 01101010 */, 1 /* 01101011 */,
	0 /* 01101100 */, 1 /* 01101101 */, 1 /* 01101110 */, 0 /* 01101111 */,
	1 /* 01110000 */, 0 /* 01110001 */, 0 /* 01110010 */, 1 /* 01110011 */,
	0 /* 01110100 */, 1 /* 01110101 */, 1 /* 01110110 */, 0 /* 01110111 */,
	0 /* 01111000 */, 1 /* 01111001 */, 1 /* 01111010 */, 0 /* 01111011 */,
	1 /* 01111100 */, 0 /* 01111101 */, 0 /* 01111110 */, 1 /* 01111111 */,
	1 /* 10000000 */, 0 /* 10000001 */, 0 /* 10000010 */, 1 /* 10000011 */,
	0 /* 10000100 */, 1 /* 10000101 */, 1 /* 10000110 */, 0 /* 10000111 */,
	0 /* 10001000 */, 1 /* 10001001 */, 1 /* 10001010 */, 0 /* 10001011 */,
	1 /* 10001100 */, 0 /* 10001101 */, 0 /* 10001110 */, 1 /* 10001111 */,
	0 /* 10010000 */, 1 /* 10010001 */, 1 /* 10010010 */, 0 /* 10010011 */,
	1 /* 10010100 */, 0 /* 10010101 */, 0 /* 10010110 */, 1 /* 10010111 */,
	1 /* 10011000 */, 0 /* 10011001 */, 0 /* 10011010 */, 1 /* 10011011 */,
	0 /* 10011100 */, 1 /* 10011101 */, 1 /* 10011110 */, 0 /* 10011111 */,
	0 /* 10100000 */, 1 /* 10100001 */, 1 /* 10100010 */, 0 /* 10100011 */,
	1 /* 10100100 */, 0 /* 10100101 */, 0 /* 10100110 */, 1 /* 10100111 */,
	1 /* 10101000 */, 0 /* 10101001 */, 0 /* 10101010 */, 1 /* 10101011 */,
	0 /* 10101100 */, 1 /* 10101101 */, 1 /* 10101110 */, 0 /* 10101111 */,
	1 /* 10110000 */, 0 /* 10110001 */, 0 /* 10110010 */, 1 /* 10110011 */,
	0 /* 10110100 */, 1 /* 10110101 */, 1 /* 10110110 */, 0 /* 10110111 */,
	0 /* 10111000 */, 1 /* 10111001 */, 1 /* 10111010 */, 0 /* 10111011 */,
	1 /* 10111100 */, 0 /* 10111101 */, 0 /* 10111110 */, 1 /* 10111111 */,
	0 /* 11000000 */, 1 /* 11000001 */, 1 /* 11000010 */, 0 /* 11000011 */,
	1 /* 11000100 */, 0 /* 11000101 */, 0 /* 11000110 */, 1 /* 11000111 */,
	1 /* 11001000 */, 0 /* 11001001 */, 0 /* 11001010 */, 1 /* 11001011 */,
	0 /* 11001100 */, 1 /* 11001101 */, 1 /* 11001110 */, 0 /* 11001111 */,
	1 /* 11010000 */, 0 /* 11010001 */, 0 /* 11010010 */, 1 /* 11010011 */,
	0 /* 11010100 */, 1 /* 11010101 */, 1 /* 11010110 */, 0 /* 11010111 */,
	0 /* 11011000 */, 1 /* 11011001 */, 1 /* 11011010 */, 0 /* 11011011 */,
	1 /* 11011100 */, 0 /* 11011101 */, 0 /* 11011110 */, 1 /* 11011111 */,
	1 /* 11100000 */, 0 /* 11100001 */, 0 /* 11100010 */, 1 /* 11100011 */,
	0 /* 11100100 */, 1 /* 11100101 */, 1 /* 11100110 */, 0 /* 11100111 */,
	0 /* 11101000 */, 1 /* 11101001 */, 1 /* 11101010 */, 0 /* 11101011 */,
	1 /* 11101100 */, 0 /* 11101101 */, 0 /* 11101110 */, 1 /* 11101111 */,
	0 /* 11110000 */, 1 /* 11110001 */, 1 /* 11110010 */, 0 /* 11110011 */,
	1 /* 11110100 */, 0 /* 11110101 */, 0 /* 11110110 */, 1 /* 11110111 */,
	1 /* 11111000 */, 0 /* 11111001 */, 0 /* 11111010 */, 1 /* 11111011 */,
	0 /* 11111100 */, 1 /* 11111101 */, 1 /* 11111110 */, 0 /* 11111111 */
};
