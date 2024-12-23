/*
 * Z80SIM  -  a Z80-CPU simulator
 *
 * Copyright (C) 2024 by Udo Munk & Thomas Eberhardt
 *
 * This is the main program for a Raspberry Pico (W) board,
 * substitutes z80core/simmain.c
 *
 * History:
 * 28-APR-2024 implemented first release of Z80 emulation
 * 09-MAY-2024 test 8080 emulation
 * 27-MAY-2024 add access to files on MicroSD
 * 28-MAY-2024 implemented boot from disk images with some OS
 * 31-MAY-2024 use USB UART
 * 09-JUN-2024 implemented boot ROM
 */

/* Raspberry SDK and FatFS includes */
#include <stdio.h>
#include <string.h>
#if LIB_PICO_STDIO_USB || LIB_STDIO_MSC_USB
#include <tusb.h>
#endif
#if defined(RASPBERRYPI_PICO_W) || defined(RASPBERRYPI_PICO2_W)
#include "pico/cyw43_arch.h"
#endif
#include "pico/binary_info.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include "hardware/adc.h"
#include "hardware/uart.h"
#include "hardware/watchdog.h"

#include "gpio.h"
#include "hw_config.h"
#include "my_rtc.h"

/* Project includes */
#include "sim.h"
#include "simdefs.h"
#include "simglb.h"
#include "simcfg.h"
#include "simmem.h"
#include "simcore.h"
#include "simport.h"
#include "simio.h"
#ifdef WANT_ICE
#include "simice.h"
#endif

#include "disks.h"
#include "rgbled.h"

#define BS  0x08 /* ASCII backspace */
#define DEL 0x7f /* ASCII delete */

/* CPU speed */
int speed = CPU_SPEED;

/* PIO and sm used for RGB LED */
PIO pio = pio1;
uint sm;

/*
 *	callback for TinyUSB when terminal sends a break
 *	stops CPU
 */
#if LIB_PICO_STDIO_USB || (LIB_STDIO_MSC_USB && !STDIO_MSC_USB_DISABLE_STDIO)
void tud_cdc_send_break_cb(uint8_t itf, uint16_t duration_ms)
{
	UNUSED(itf);
	UNUSED(duration_ms);

	cpu_error = USERINT;
	cpu_state = ST_STOPPED;
}
#endif

/*
 *	interrupt handler for break switch
 *	stops CPU
 */
static void gpio_callback(uint gpio, uint32_t events)
{
	UNUSED(gpio);
	UNUSED(events);

	cpu_error = USERINT;
	cpu_state = ST_STOPPED;
}

/*
 *	read the onboard temperature sensor
 */
float read_onboard_temp(void)
{
	/* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
	const float conversionFactor = 3.3f / (1 << 12);

	float adc = (float) adc_read() * conversionFactor;
	float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

	return tempC;
}

int main(void)
{
	char s[2];
	uint32_t rgb = 0x005500;/* initial value for the RGB LED */

	/* strings for picotool, so that it shows used pins */
	bi_decl(bi_1pin_with_name(SWITCH_BREAK, "Interrupt switch"));
	bi_decl(bi_1pin_with_name(WS2812_PIN, "WS2812 RGB LED"));
	bi_decl(bi_4pins_with_names(SD_SPI_CLK, "SD card CLK",
				    SD_SPI_SI, "SD card SI",
				    SD_SPI_SO, "SD card SO",
				    SD_SPI_CS, "SD card CS"));
	bi_decl(bi_2pins_with_names(DS3231_I2C_SDA_PIN, "DS3231 I2C SDA",
				    DS3231_I2C_SCL_PIN, "DS3231 I2C SCL"));

	stdio_init_all();	/* initialize stdio */
#if LIB_STDIO_MSC_USB
	sd_init_driver();	/* initialize SD card driver */
	tusb_init();		/* initialize TinyUSB */
	stdio_msc_usb_init();	/* initialize MSC USB stdio */
#endif
	time_init();		/* initialize FatFS RTC */

	/*
	 * initialize hardware AD converter, enable onboard
	 * temperature sensor and select its channel
	 */
	adc_init();
	adc_set_temp_sensor_enabled(true);
	adc_select_input(4);

	/* setup interrupt for break switch */
	gpio_init(SWITCH_BREAK);
	gpio_set_dir(SWITCH_BREAK, GPIO_IN);
	gpio_set_irq_enabled_with_callback(SWITCH_BREAK, GPIO_IRQ_EDGE_RISE,
					   true, &gpio_callback);

	/* initialize RGB LED */
	sm = pio_claim_unused_sm(pio, true);
	uint offset = pio_add_program(pio, &ws2812_program);
	ws2812_program_init(pio, sm, offset, WS2812_PIN, 800000, true);
	put_pixel(rgb); /* LED red */

#if LIB_PICO_STDIO_UART
	uart_inst_t *my_uart = uart_default;
	/* destroy random input from UART after activation */
	if (uart_is_readable(my_uart))
		getchar();
#endif

	/* when using USB UART wait until it is connected */
	/* but also get out if there is input at default UART */
#if LIB_PICO_STDIO_USB || (LIB_STDIO_MSC_USB && !STDIO_MSC_USB_DISABLE_STDIO)
	while (!tud_cdc_connected()) {
#if LIB_PICO_STDIO_UART
		if (uart_is_readable(my_uart)) {
			getchar();
			break;
		}
#endif
		rgb = rgb - 0x000100;	/* while waiting make */
		if (rgb == 0)		/* RGB LED fading red */
			rgb = 0x005500;
		put_pixel(rgb);
		sleep_ms(50);
	}
#endif
	put_pixel(0x000044); /* LED blue */

	/* print banner */
	printf("\fZ80pack release %s, %s\n", RELEASE, COPYR);
	printf("%s release %s\n", USR_COM, USR_REL);
#if PICO_RP2350
#if PICO_RISCV
	puts("running on Hazard3 RISC-V cores");
#else
	puts("running on ARM Cortex-M33 cores");
#endif
#endif
	printf("%s\n\n", USR_CPR);

#if defined(RASPBERRYPI_PICO_W) || defined(RASPBERRYPI_PICO2_W)
	/* initialize Pico W hardware */
	if (cyw43_arch_init())
		panic("CYW43 init failed\n");
#endif

	init_cpu();		/* initialize CPU */
	init_disks();		/* initialize disk drives */
	init_memory();		/* initialize memory configuration */
	init_io();		/* initialize I/O devices */
	PC = 0xff00;		/* power on jump into the boot ROM */
	config();		/* configure the machine */

	f_flag = speed;		/* setup speed of the CPU */
	tmax = speed * 10000;	/* theoretically */

	put_pixel(0x440000);	/* LED green */

	/* run the CPU with whatever is in memory */
#ifdef WANT_ICE
	ice_cmd_loop(0);
#else
	run_cpu();
#endif

	put_pixel(0x000000);	/* LED off */
	exit_disks();		/* stop disk drives */

#ifndef WANT_ICE
	putchar('\n');
	report_cpu_error();	/* check for CPU emulation errors and report */
	report_cpu_stats();	/* print some execution statistics */
#endif
	puts("\nPress any key to restart CPU");
	get_cmdline(s, 2);

	/* reset machine */
	watchdog_reboot(0, 0, 0);
	for (;;) {
		__nop();
	}
}

/*
 * Read an ICE or config command line of maximum length len - 1
 * from the terminal. For single character requests (len == 2),
 * returns immediately after input is received.
 */
int get_cmdline(char *buf, int len)
{
	int i = 0;
	char c;

	for (;;) {
		c = getchar();
		if ((c == BS) || (c == DEL)) {
			if (i >= 1) {
				putchar(BS);
				putchar(' ');
				putchar(BS);
				i--;
			}
		} else if (c != '\r') {
			if (i < len - 1) {
				buf[i++] = c;
				putchar(c);
				if (len == 2)
					break;
			}
		} else {
			break;
		}
	}
	buf[i] = '\0';
	putchar('\n');
	return 0;
}
