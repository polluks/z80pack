/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk
 *
 * This is the main program for a Raspberry Pico (W) board,
 * substitutes z80core/simmain.c
 *
 * History:
 * 28-APR-2024 implemented first release of Z80 emulation
 */

/* Raspberry SDK includes */
#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/time.h"

/* Project includes */
#include "sim.h"
#include "simglb.h"
#include "config.h"
#include "memory.h"

/* Pico W also needs this */
#if PICO == 1
#include "pico/cyw43_arch.h"
#endif

extern void init_cpu(void);
extern void run_cpu(void);
extern void report_cpu_error(void), report_cpu_stats(void);

int main(void)
{
	stdio_init_all();	/* initialize Pico stdio */

#if PICO == 1			/* initialize Pico W hardware */
	if (cyw43_arch_init())
	{
		printf("CYW43 init failed\n");
		return -1;
	}
#else				/* initialize Pico hardware */
#define LED PICO_DEFAULT_LED_PIN	/* use builtin LED */
//#define LED 15			/* or another one */
	gpio_init(LED);		/* configure GPIO for LED output */
	gpio_set_dir(LED, GPIO_OUT);
#endif

	printf("Z80pack release %s, %s\n", RELEASE, COPYR);
	printf("%s release %s, %s\n\n", USR_COM, USR_REL, USR_CPR);

	f_flag = CPU_SPEED;
	tmax = CPU_SPEED * 10000; /* theoretically */
	tmax += tmax / 20;	  /* clock crystal tuning, screw here */

	if (f_flag > 0)
		printf("CPU speed is %d MHz", f_flag);
	else
		printf("CPU speed is unlimited");
#ifndef UNDOC_INST
	printf(", CPU doesn't execute undocumented instructions\n");
#else
	printf(", CPU executes undocumented instructions\n");
#endif
	printf("\n");

	config();		/* read system configuration */
	init_cpu();		/* initialize CPU */
	init_memory();		/* initialize memory configuration */

	cpu_start = to_ms_since_boot(get_absolute_time());
	run_cpu();		/* run the CPU with whatever is in memory */
	cpu_stop = to_ms_since_boot(get_absolute_time());

	/* switch builtin LED on */
#if PICO == 1
	cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
#else
	gpio_put(LED, 1);
#endif

	putchar('\n');
	report_cpu_error();	/* check for CPU emulation errors and report */
	report_cpu_stats();	/* print some execution statistics */
	putchar('\n');
	stdio_flush();
}
