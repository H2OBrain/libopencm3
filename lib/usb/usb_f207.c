/*
 * This file is part of the libopencm3 project.
 *
 * Copyright (C) 2011 Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>
#include <libopencm3/cm3/common.h>
#include <libopencm3/stm32/tools.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/usb/usbd.h>
#include <libopencm3/usb/dwc/otg_hs.h>
#include "usb_private.h"
#include "usb_dwc_common.h"

/* Receive FIFO size in 32-bit words. */
#define RX_FIFO_SIZE 512

static usbd_device *stm32f207_usbd_init(void);

static struct _usbd_device usbd_dev;

const struct _usbd_driver stm32f207_usb_driver = {
	.init = stm32f207_usbd_init,
	.set_address = dwc_set_address,
	.ep_setup = dwc_ep_setup,
	.ep_reset = dwc_endpoints_reset,
	.ep_stall_set = dwc_ep_stall_set,
	.ep_stall_get = dwc_ep_stall_get,
	.ep_nak_set = dwc_ep_nak_set,
	.ep_write_packet = dwc_ep_write_packet,
	.ep_read_packet = dwc_ep_read_packet,
	.poll = dwc_poll,
	.disconnect = dwc_disconnect,
	.base_address = USB_OTG_HS_BASE,
	.set_address_before_status = 1,
	.rx_fifo_size = RX_FIFO_SIZE,
};

#define CYCLES_PER_LOOP 3
static inline void wait_cycles( uint32_t n ) {
    uint32_t l = n/CYCLES_PER_LOOP;
    __asm__ volatile( "0:" "SUBS %[count], 1;" "BNE 0b;" :[count]"+r"(l) );
}
static inline void msleep_loop(uint32_t ms) {
	wait_cycles(180000000 /1000 *ms);
}


/** Initialize the USB device controller hardware of the STM32. */
static usbd_device *stm32f207_usbd_init(void)
{
	rcc_periph_clock_enable(RCC_OTGHS);
	OTG_HS_GINTSTS = OTG_GINTSTS_MMIS;

#ifndef USE_ULPI
	OTG_HS_GUSBCFG |= OTG_GUSBCFG_PHYSEL;
	/* Enable VBUS sensing in device mode and power down the PHY. */
	OTG_HS_GCCFG |= OTG_GCCFG_VBUSBSEN | OTG_GCCFG_PWRDWN;

	/* Wait for AHB idle. */
	while (!(OTG_HS_GRSTCTL & OTG_GRSTCTL_AHBIDL));
	/* Do core soft reset. */
	OTG_HS_GRSTCTL |= OTG_GRSTCTL_CSRST;
	while (OTG_HS_GRSTCTL & OTG_GRSTCTL_CSRST);
	wait_cycles(3);

	/* Force peripheral only mode and set turnaround time to maximum */
	OTG_HS_GUSBCFG |= OTG_GUSBCFG_FDMOD | OTG_GUSBCFG_TRDT_MASK; // | (0x5<<10);

	/* Full speed device. */
	OTG_HS_DCFG |= OTG_DCFG_SPEED_FULL_INTERNAL_PHY;
#else
	rcc_periph_clock_enable(RCC_OTGHSULPI);

	/* Power up the PHY. */
	OTG_HS_GCCFG   &= ~OTG_GCCFG_PWRDWN;
	/* */
	OTG_HS_GUSBCFG &= ~(OTG_GUSBCFG_TSDPS | OTG_GUSBCFG_ULPIFSLS | OTG_GUSBCFG_PHYSEL);
	OTG_HS_GUSBCFG &= ~(OTG_GUSBCFG_ULPIEVBUSI | OTG_GUSBCFG_ULPIEVBUSD);
	/* Enable VBUS indicator */
//	OTG_HS_GUSBCFG |= OTG_GUSBCFG_ULPIEVBUSI;
	/* Enable VBUS sensing in device mode and power down the PHY. */
#ifdef USE_ULPI_EXTERNAL_VBUS
	OTG_HS_GUSBCFG |= OTG_GUSBCFG_ULPIEVBUSD;
#else
	OTG_HS_GUSBCFG &= ~OTG_GUSBCFG_ULPIEVBUSD;
#endif
	/* Wait for AHB idle. */
	while (!(OTG_HS_GRSTCTL & OTG_GRSTCTL_AHBIDL));
	/* Do core soft reset. */
	OTG_HS_GRSTCTL |= OTG_GRSTCTL_CSRST;
	while (OTG_HS_GRSTCTL & OTG_GRSTCTL_CSRST);

	wait_cycles(10);

	/* Force peripheral only mode and set turnaround time to maximum */
	OTG_HS_GUSBCFG |= OTG_GUSBCFG_FDMOD | OTG_GUSBCFG_TRDT_MASK; // | (0x5<<10);

	// sleep ~50ms (at least 25ms according to the reference manual)
	msleep_loop(50);

	/* Enable VBUS sensing */
	OTG_HS_GCCFG |= OTG_GCCFG_VBUSBSEN;
//	OTG_HS_GCCFG |= OTG_GCCFG_NOVBUSSENS; // disable bus sensing
	OTG_HS_GCCFG |= OTG_GCCFG_VBDEN;

	/* Restart the PHY clock. */
	OTG_HS_PCGCCTL = 0;

	/* Set frame interval to 80 */
//	OTG_HS_DCFG |= 0;

	/* Full speed device. */
#ifdef USE_ULPI_FULL_SPEED
	OTG_HS_DCFG |= OTG_DCFG_SPEED_FULL_EXTERNAL_PHY;
#else
	OTG_HS_DCFG |= OTG_DCFG_SPEED_HIGH_EXTERNAL_PHY;
#endif

#endif

	/* Restart the PHY clock. */
	OTG_HS_PCGCCTL = 0;

	OTG_HS_GRXFSIZ = stm32f207_usb_driver.rx_fifo_size;
	usbd_dev.fifo_mem_top = stm32f207_usb_driver.rx_fifo_size;

	/* Unmask interrupts for TX and RX. */
	OTG_HS_GAHBCFG |= OTG_GAHBCFG_GINT;
	OTG_HS_GINTMSK =
			 OTG_GINTMSK_ENUMDNEM |
			 OTG_GINTMSK_RXFLVLM |
//			 OTG_GINTSTS_OEPINT |
			 OTG_GINTMSK_IEPINT |
			 OTG_GINTMSK_USBSUSPM |
			 OTG_GINTMSK_WUIM |
			 OTG_GINTMSK_OTGINT; // | OTG_GINTMSK_SRQIM;

	OTG_HS_DAINTMSK = 0xFFFF;
	OTG_HS_DIEPMSK = OTG_DIEPMSK_XFRCM;

	// disconnect
	OTG_HS_DCTL |= OTG_DCTL_SDIS;
	msleep_loop(3);
	// connect
	OTG_HS_DCTL &= ~OTG_DCTL_SDIS;
	msleep_loop(3);

	return &usbd_dev;
}
