/*
 * Copyright (c) 2014, ARM Limited and Contributors. All rights reserved.
 * Copyright (c) 2014, Hisilicon Ltd.
 * Copyright (c) 2014, Linaro Ltd.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of ARM nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <debug.h>
#include <hi6220.h>
#include <mmio.h>
#include <partitions.h>
#include <platform_def.h>
#include <sp804_timer.h>
#include <string.h>
#include <usb.h>

#define NUM_ENDPOINTS			16

#define USB_BLOCK_HIGH_SPEED_SIZE	512

struct ep_type {
	unsigned char		active;
	unsigned char		busy;
	unsigned char		done;
	unsigned int		rc;
	unsigned int		size;
};

struct usb_endpoint {
	struct usb_endpoint	*next;
	unsigned int		maxpkt;
	struct usb_request	*req;
	unsigned char		num;
	unsigned char		in;
};

struct usb_config_bundle {
	struct usb_config_descriptor config;
	struct usb_interface_descriptor interface;
	struct usb_endpoint_descriptor ep1;
	struct usb_endpoint_descriptor ep2;
};

static setup_packet ctrl_req
__attribute__ ((section("tzfw_coherent_mem")));
static unsigned char ctrl_resp[2]
__attribute__ ((section("tzfw_coherent_mem")));

static struct ep_type endpoints[NUM_ENDPOINTS]
__attribute__ ((section("tzfw_coherent_mem")));

dwc_otg_dev_dma_desc_t dma_desc
__attribute__ ((section("tzfw_coherent_mem")));
dwc_otg_dev_dma_desc_t dma_desc_ep0
__attribute__ ((section("tzfw_coherent_mem")));
dwc_otg_dev_dma_desc_t dma_desc_in
__attribute__ ((section("tzfw_coherent_mem")));
dwc_otg_dev_dma_desc_t dma_desc_addr
__attribute__ ((section("tzfw_coherent_mem")));

static struct usb_config_bundle config_bundle
__attribute__ ((section("tzfw_coherent_mem")));
static struct usb_device_descriptor device_descriptor
__attribute__ ((section("tzfw_coherent_mem")));

static struct usb_request rx_req
__attribute__ ((section("tzfw_coherent_mem")));
static struct usb_request tx_req
__attribute__ ((section("tzfw_coherent_mem")));

static const struct usb_string_descriptor string_devicename = {
	24,
	USB_DT_STRING,
	{'A', 'n', 'd', 'r', 'o', 'i', 'd', ' ', '2', '.', '0'}
};

static const struct usb_string_descriptor serial_string_descriptor = {
	34,
	USB_DT_STRING,
	{'0','1','2','3','4','5','6','7','8','9','A','B','C','D','E','F'}
};

static const struct usb_string_descriptor lang_descriptor = {
	4,
	USB_DT_STRING,
	{0x0409}
};

static void usb_rx_cmd_complete(unsigned actual, int stat);
static void usb_rx_data_complete(unsigned actual, int status);

static unsigned int rx_desc_bytes = 0;
static unsigned rx_addr;
static unsigned rx_length;
static unsigned int last_one = 0;
static struct usb_string_descriptor *serial_string = NULL;
static char *cmdbuf;
static struct usb_endpoint ep1in, ep1out;
static int g_usb_enum_flag = 0;

int usb_need_reset = 0;

static const char *reqname(unsigned char r)  //reserved
{
	switch(r) {
	case USB_REQ_GET_STATUS:
		return "GET_STATUS";
	case USB_REQ_CLEAR_FEATURE:
		return "CLEAR_FEATURE";
	case USB_REQ_SET_FEATURE:
		return "SET_FEATURE";
	case USB_REQ_SET_ADDRESS:
		return "SET_ADDRESS";
	case USB_REQ_GET_DESCRIPTOR:
		return "GET_DESCRIPTOR";
	case USB_REQ_SET_DESCRIPTOR:
		return "SET_DESCRIPTOR";
	case USB_REQ_GET_CONFIGURATION:
		return "GET_CONFIGURATION";
	case USB_REQ_SET_CONFIGURATION:
		return "SET_CONFIGURATION";
	case USB_REQ_GET_INTERFACE:
		return "GET_INTERFACE";
	case USB_REQ_SET_INTERFACE:
		return "SET_INTERFACE";
	default:
		return "*UNKNOWN*";
	}
}

static int usb_drv_port_speed(void)
{
	/* 2'b00 High speed (PHY clock is at 30MHz or 60MHz) */
	return (mmio_read_32(DSTS) & 2) == 0 ? 1 : 0;
}

static void reset_endpoints(void)
{
	int i;
	unsigned int data;

	INFO("enter reset_endpoints.\n");
	for (i = 0; i < NUM_ENDPOINTS; i++) {
		endpoints[i].active = 0;
		endpoints[i].busy = 0;
		endpoints[i].rc = -1;
		endpoints[i].done = 1;
	}

	/* EP0 IN ACTIVE NEXT=1 */
	mmio_write_32(DIEPCTL0, 0x8800);

	/* EP0 OUT ACTIVE */
	mmio_write_32(DOEPCTL0, 0x8000);

	/* Clear any pending OTG Interrupts */
	mmio_write_32(GOTGINT, ~0);

	/* Clear any pending interrupts */
	mmio_write_32(GINTSTS, ~0);
	mmio_write_32(DIEPINT0, ~0);
	mmio_write_32(DOEPINT0, ~0);
	mmio_write_32(DIEPINT1, ~0);
	mmio_write_32(DOEPINT1, ~0);

	/* IN EP interrupt mask */
	mmio_write_32(DIEPMSK, 0x0D);
	/* OUT EP interrupt mask */
	mmio_write_32(DOEPMSK, 0x0D);
	/* Enable interrupts on Ep0 */
	mmio_write_32(DAINTMSK, 0x00010001);

	/* EP0 OUT Transfer Size:64 Bytes, 1 Packet, 3 Setup Packet, Read to receive setup packet*/
	mmio_write_32(DOEPTSIZ0, 0x60080040);
	//notes that:the compulsive conversion is expectable.
	dma_desc_ep0.status.b.bs = 0x3;
	dma_desc_ep0.status.b.mtrf = 0;
	dma_desc_ep0.status.b.sr = 0;
	dma_desc_ep0.status.b.l = 1;
	dma_desc_ep0.status.b.ioc = 1;
	dma_desc_ep0.status.b.sp = 0;
	dma_desc_ep0.status.b.bytes = 64;
	dma_desc_ep0.buf = (unsigned long)&ctrl_req;
	dma_desc_ep0.status.b.sts = 0;
	dma_desc_ep0.status.b.bs = 0x0;
	mmio_write_32(DOEPDMA0, ((unsigned long)&(dma_desc_ep0)));
	INFO("%s, &ctrl_req:%llx:%x, &dms_desc_ep0:%llx:%x\n",
		__func__, (unsigned long)&ctrl_req, (unsigned long)&ctrl_req,
		(unsigned long)&dma_desc_ep0, (unsigned long)&dma_desc_ep0);
	/* EP0 OUT ENABLE CLEARNAK */
	data = mmio_read_32(DOEPCTL0);
	mmio_write_32(DOEPCTL0, (data | 0x84000000));

	VERBOSE("exit reset_endpoints. \n");
}

static int usb_drv_request_endpoint(int type, int dir)
{
	int ep = 1;    /*FIXME*/
	unsigned int newbits, data;

	newbits = (type << 18) | 0x10000000;

	/*
	 * (type << 18):Endpoint Type (EPType)
	 * 0x10000000:Endpoint Enable (EPEna)
	 * 0x000C000:Endpoint Type (EPType);Hardcoded to 00 for control.
	 * (ep<<22):TxFIFO Number (TxFNum)
	 * 0x20000:NAK Status (NAKSts);The core is transmitting NAK handshakes on this endpoint.
	 */
	if (dir) {  // IN: to host
		data = mmio_read_32(DIEPCTL(ep));
		data &= ~0x000c0000;
		data |= newbits | (ep << 22) | 0x20000;
		mmio_write_32(DIEPCTL(ep), data);
	} else {    // OUT: to device
		data = mmio_read_32(DOEPCTL(ep));
		data &= ~0x000c0000;
		data |= newbits;
		mmio_write_32(DOEPCTL(ep), data);
	}
	endpoints[ep].active = 1;	// true

    return ep | dir;
}

void usb_drv_release_endpoint(int ep)
{
	ep = ep & 0x7f;
	if (ep < 1 || ep > USB_NUM_ENDPOINTS)
		return;

	endpoints[ep].active = 0;
}

void usb_config(void)
{
	unsigned int data;

	INFO("enter usb_config\n");

	mmio_write_32(GDFIFOCFG, DATA_FIFO_CONFIG);
	mmio_write_32(GRXFSIZ, RX_SIZE);
	mmio_write_32(GNPTXFSIZ, ENDPOINT_TX_SIZE);

	mmio_write_32(DIEPTXF1, DATA_IN_ENDPOINT_TX_FIFO1);
	mmio_write_32(DIEPTXF2, DATA_IN_ENDPOINT_TX_FIFO2);
	mmio_write_32(DIEPTXF3, DATA_IN_ENDPOINT_TX_FIFO3);
	mmio_write_32(DIEPTXF4, DATA_IN_ENDPOINT_TX_FIFO4);
	mmio_write_32(DIEPTXF5, DATA_IN_ENDPOINT_TX_FIFO5);
	mmio_write_32(DIEPTXF6, DATA_IN_ENDPOINT_TX_FIFO6);
	mmio_write_32(DIEPTXF7, DATA_IN_ENDPOINT_TX_FIFO7);
	mmio_write_32(DIEPTXF8, DATA_IN_ENDPOINT_TX_FIFO8);
	mmio_write_32(DIEPTXF9, DATA_IN_ENDPOINT_TX_FIFO9);
	mmio_write_32(DIEPTXF10, DATA_IN_ENDPOINT_TX_FIFO10);
	mmio_write_32(DIEPTXF11, DATA_IN_ENDPOINT_TX_FIFO11);
	mmio_write_32(DIEPTXF12, DATA_IN_ENDPOINT_TX_FIFO12);
	mmio_write_32(DIEPTXF13, DATA_IN_ENDPOINT_TX_FIFO13);
	mmio_write_32(DIEPTXF14, DATA_IN_ENDPOINT_TX_FIFO14);
	mmio_write_32(DIEPTXF15, DATA_IN_ENDPOINT_TX_FIFO15);

	/*Init global csr register.*/

	/*
	 * set Periodic TxFIFO Empty Level,
	 * Non-Periodic TxFIFO Empty Level,
	 * Enable DMA, Unmask Global Intr
	 */
	INFO("USB: DMA mode.\n");
	mmio_write_32(GAHBCFG, GAHBCFG_CTRL_MASK);

	/*select 8bit UTMI+, ULPI Inerface*/
	INFO("USB ULPI PHY\n");
	mmio_write_32(GUSBCFG, 0x2400);

	/* Detect usb work mode,host or device? */
	do {
		data = mmio_read_32(GINTSTS);
	} while (data & GINTSTS_CURMODE_HOST);
	VERBOSE("Enter device mode\n");
	udelay(3);

	/*Init global and device mode csr register.*/
	/*set Non-Zero-Length status out handshake */
	data = (0x20 << DCFG_EPMISCNT_SHIFT) | DCFG_NZ_STS_OUT_HSHK;
	mmio_write_32(DCFG, data);

	/* Interrupt unmask: IN event, OUT event, bus reset */
	data = GINTSTS_OEPINT | GINTSTS_IEPINT | GINTSTS_ENUMDONE |
	       GINTSTS_USBRST | GINTSTS_USBSUSP | GINTSTS_ERLYSUSP |
	       GINTSTS_GOUTNAKEFF;
	mmio_write_32(GINTMSK, data);

	do {
		data = mmio_read_32(GINTSTS) & GINTSTS_ENUMDONE;
	} while (data);
	VERBOSE("USB Enum Done.\n");

	/* Clear any pending OTG Interrupts */
	mmio_write_32(GOTGINT, ~0);
	/* Clear any pending interrupts */
	mmio_write_32(GINTSTS, ~0);
	mmio_write_32(GINTMSK, ~0);
	data = mmio_read_32(GOTGINT);
	data &= ~0x3000;
	mmio_write_32(GOTGINT, data);
	/*endpoint settings cfg*/
	reset_endpoints();

	udelay(1);

	/*init finish. and ready to transfer data*/

	/* Soft Disconnect */
	mmio_write_32(DCTL, 0x802);
	udelay(10000);

	/* Soft Reconnect */
	mmio_write_32(DCTL, 0x800);
	VERBOSE("exit usb_config.\n");
}

void usb_drv_set_address(int address)
{
	unsigned int cfg;

	cfg = mmio_read_32(DCFG);
	cfg &= ~0x7F0;
	cfg |= address << 4;
	mmio_write_32(DCFG, cfg);	// 0x7F0: device address
}

static void ep_send(int ep, const void *ptr, int len)
{
	int blocksize, packets;
	unsigned int data;

	endpoints[ep].busy = 1;		// true
	endpoints[ep].size = len;

	/* EPx OUT ACTIVE */
	data = mmio_read_32(DIEPCTL(ep)) | DXEPCTL_USBACTEP;
	mmio_write_32(DIEPCTL(ep), data);
	if (!ep) {
		blocksize = 64;
	} else {
		blocksize = usb_drv_port_speed() ? USB_BLOCK_HIGH_SPEED_SIZE : 64;
	}
	packets = (len + blocksize - 1) / blocksize;    // transfer block number

	/* set DMA Address */
	if (!len) {
		/* send one empty packet */
		dma_desc_in.buf = 0;
	} else {
		VERBOSE("%s, size = 0x%x, ptr = 0x%x, packets = %d, len = %d.\n",
			__func__, len | (packets << 19), ptr, packets, len);
		dma_desc_in.buf = (unsigned long)ptr;
	}
	dma_desc_in.status.b.bs = 0x3;
	dma_desc_in.status.b.l = 1;
	dma_desc_in.status.b.ioc = 1;
	dma_desc_in.status.b.sp = 1;
	dma_desc_in.status.b.sts = 0;
	dma_desc_in.status.b.bs = 0x0;
	dma_desc_in.status.b.bytes = len;
	mmio_write_32(DIEPDMA(ep), (unsigned long)&dma_desc_in);

	data = mmio_read_32(DIEPCTL(ep));
	data |= DXEPCTL_EPENA | DXEPCTL_CNAK | DXEPCTL_NEXTEP(ep + 1);
	mmio_write_32(DIEPCTL(ep), data);
}

void usb_drv_stall(int endpoint, char stall, char in)
{
	unsigned int data;

	/*
	 * STALL Handshake (Stall)
	 */

	data = mmio_read_32(DIEPCTL(endpoint));
	if (in) {
		if (stall)
			mmio_write_32(DIEPCTL(endpoint), data | 0x00200000);
		else
			mmio_write_32(DIEPCTL(endpoint), data & ~0x00200000);
	} else {
		if (stall)
			mmio_write_32(DOEPCTL(endpoint), data | 0x00200000);
		else
			mmio_write_32(DOEPCTL(endpoint), data & ~0x00200000);
	}
}

int usb_drv_send_nonblocking(int endpoint, const void *ptr, int len)
{
	VERBOSE("%s, endpoint = %d, ptr = 0x%x, Len=%d.\n",
		__func__, endpoint, ptr, len);
	ep_send(endpoint & 0x7f, ptr, len);
	return 0;
}

void usb_drv_cancel_all_transfers(void)
{
	reset_endpoints();
}

int hiusb_epx_tx(unsigned ep, void *buf, unsigned len)
{
	int blocksize,packets;
	unsigned int epints;
	unsigned int cycle = 0;
	unsigned int data;

	endpoints[ep].busy = 1;		//true
	endpoints[ep].size = len;

	while (mmio_read_32(GINTSTS) & 0x40) {
		data = mmio_read_32(DCTL);
		data |= 0x100;
		mmio_write_32(DCTL, data);
	}

	data = mmio_read_32(DIEPCTL(ep));
	data |= 0x08000000;
	mmio_write_32(DIEPCTL(ep), data);

	/* EPx OUT ACTIVE */
	mmio_write_32(DIEPCTL(ep), data | 0x8000);
	if (!ep) {
		blocksize = 64;
	} else {
		blocksize = usb_drv_port_speed() ? USB_BLOCK_HIGH_SPEED_SIZE : 64;
	}
	packets = (len + blocksize - 1) / blocksize;

	if (!len) {
		/* one empty packet */
		mmio_write_32(DIEPTSIZ(ep), 1 << 19);
		/* NULL */
		dma_desc_in.status.b.bs = 0x3;
		dma_desc_in.status.b.l = 1;
		dma_desc_in.status.b.ioc = 1;
		dma_desc_in.status.b.sp = last_one;
		dma_desc_in.status.b.bytes = 0;
		dma_desc_in.buf = 0;
		dma_desc_in.status.b.sts = 0;
		dma_desc_in.status.b.bs = 0x0;
		mmio_write_32(DIEPDMA(ep), (unsigned long)&dma_desc_in);
	} else {
		mmio_write_32(DIEPTSIZ(ep), len | (packets << 19));
		dma_desc_in.status.b.bs = 0x3;
		dma_desc_in.status.b.l = 1;
		dma_desc_in.status.b.ioc = 1;
		dma_desc_in.status.b.sp = last_one;
		dma_desc_in.status.b.bytes = len;
		dma_desc_in.buf = (unsigned long)buf;
		dma_desc_in.status.b.sts = 0;
		dma_desc_in.status.b.bs = 0x0;
		mmio_write_32(DIEPDMA(ep), (unsigned long)&dma_desc_in);
	}

	cycle = 0;
	while(1){
		data = mmio_read_32(DIEPINT(ep));
		if ((data & 0x2000) || (cycle > 10000)) {
			if (cycle > 10000) {
				NOTICE("Phase 2:ep(%d) status, DIEPCTL(%d) is [0x%x],"
				       "DTXFSTS(%d) is [0x%x], DIEPINT(%d) is [0x%x],"
				       "DIEPTSIZ(%d) is [0x%x] GINTSTS is [0x%x]\n",
					ep, ep, data,
					ep, mmio_read_32(DTXFSTS(ep)),
					ep, mmio_read_32(DIEPINT(ep)),
					ep, mmio_read_32(DIEPTSIZ(ep)),
					mmio_read_32(GINTSTS));
			}
			break;
		}

		cycle++;
		udelay(10);
	}
	VERBOSE("ep(%d) enable, DIEPCTL(%d) is [0x%x], DTXFSTS(%d) is [0x%x],"
		"DIEPINT(%d) is [0x%x], DIEPTSIZ(%d) is [0x%x] \n",
		ep, ep, mmio_read_32(DIEPCTL(ep)),
		ep, mmio_read_32(DTXFSTS(ep)),
		ep, mmio_read_32(DIEPINT(ep)),
		ep, mmio_read_32(DIEPTSIZ(ep)));

	__asm__ volatile("dsb	sy\n"
			 "isb	sy\n");
	data = mmio_read_32(DIEPCTL(ep));
	data |= 0x84000000;
	/* epena & cnak*/
	mmio_write_32(DIEPCTL(ep), data);
	__asm__ volatile("dsb	sy\n"
			 "isb	sy\n");

	cycle = 0;
	while (1) {
		epints = mmio_read_32(DIEPINT(ep)) & 1;
		if ((mmio_read_32(GINTSTS) & 0x40000) && epints) {
			VERBOSE("Tx succ:ep(%d), DTXFSTS(%d) is [0x%x] \n",
				ep, ep, mmio_read_32(DTXFSTS(ep)));
			mmio_write_32(DIEPINT(ep), epints);
			if (endpoints[ep].busy) {
				endpoints[ep].busy = 0;//false
				endpoints[ep].rc = 0;
				endpoints[ep].done = 1;//true
			}
			break;
		}
		cycle++;
		udelay(10);
		VERBOSE("loop for intr: ep(%d), DIEPCTL(%d) is [0x%x], ",
			"DTXFSTS(%d) is [0x%x], DIEPINT(%d) is [0x%x] \n",
			ep, ep, mmio_read_32(DIEPCTL(ep)),
			ep, mmio_read_32(DTXFSTS(ep)),
			ep, mmio_read_32(DIEPINT(ep)));

		if (cycle > 1000000) {
			WARN("Wait IOC intr over 10s! USB will reset\n");
			usb_need_reset = 1;
			return 1;
		}
	}

	cycle = 0;
	while (1) {
		if ((mmio_read_32(DIEPINT(ep)) & 0x2000) || (cycle > 100000)) {
			if (cycle > 100000){
				WARN("all wait cycle is [%d]\n",cycle);
			}
			break;
		}

		cycle++;
		udelay(10);
	}

	return 0;
}

int hiusb_epx_rx(unsigned ep, void *buf, unsigned len)
{
	unsigned int blocksize = 0, data;
	int packets;

	VERBOSE("ep%d rx, len = 0x%x, buf = 0x%x.\n", ep, len, buf);

	endpoints[ep].busy = 1;//true
	/* EPx UNSTALL */
	data = mmio_read_32(DOEPCTL(ep)) & ~0x00200000;
	mmio_write_32(DOEPCTL(ep), data);
	/* EPx OUT ACTIVE */
	data = mmio_read_32(DOEPCTL(ep)) | 0x8000;
	mmio_write_32(DOEPCTL(ep), data);

	blocksize = usb_drv_port_speed() ? USB_BLOCK_HIGH_SPEED_SIZE : 64;
	packets = (len + blocksize - 1) / blocksize;

#define MAX_RX_PACKET 0x3FF

	/*Max recv packets is 1023*/
	if (packets > MAX_RX_PACKET) {
		endpoints[ep].size = MAX_RX_PACKET * blocksize;
		len = MAX_RX_PACKET * blocksize;
	} else {
		endpoints[ep].size = len;
	}

	if (!len) {
		/* one empty packet */
		mmio_write_32(DOEPTSIZ(ep), 1 << 19);
		//NULL  /* dummy address */
		dma_desc.status.b.bs = 0x3;
		dma_desc.status.b.mtrf = 0;
		dma_desc.status.b.sr = 0;
		dma_desc.status.b.l = 1;
		dma_desc.status.b.ioc = 1;
		dma_desc.status.b.sp = 0;
		dma_desc.status.b.bytes = 0;
		dma_desc.buf = 0;
		dma_desc.status.b.sts = 0;
		dma_desc.status.b.bs = 0x0;

		mmio_write_32(DOEPDMA(ep), (unsigned long)&dma_desc);
	} else {
		if (len >= blocksize * 64) {
			rx_desc_bytes = blocksize*64;
		} else {
			rx_desc_bytes = len;
		}
		VERBOSE("rx len %d, rx_desc_bytes %d \n",len,rx_desc_bytes);
		dma_desc.status.b.bs = 0x3;
		dma_desc.status.b.mtrf = 0;
		dma_desc.status.b.sr = 0;
		dma_desc.status.b.l = 1;
		dma_desc.status.b.ioc = 1;
		dma_desc.status.b.sp = 0;
		dma_desc.status.b.bytes = rx_desc_bytes;
		dma_desc.buf = (unsigned long)buf;
		dma_desc.status.b.sts = 0;
		dma_desc.status.b.bs = 0x0;

		mmio_write_32(DOEPDMA(ep), (unsigned long)&dma_desc);
	}
	/* EPx OUT ENABLE CLEARNAK */
	data = mmio_read_32(DOEPCTL(ep));
	data |= 0x84000000;
	mmio_write_32(DOEPCTL(ep), data);
	return 0;
}

int usb_queue_req(struct usb_endpoint *ept, struct usb_request *req)
{
	if (ept->in)
		hiusb_epx_tx(ept->num, req->buf, req->length);
	else
		hiusb_epx_rx(ept->num, req->buf, req->length);

	return 0;
}

static void rx_cmd(void)
{
	struct usb_request *req = &rx_req;
	req->buf = cmdbuf;
	req->length = RX_REQ_LEN;
	req->complete = usb_rx_cmd_complete;
	usb_queue_req(&ep1out, req);
	/* BEGIN PN:DTS2014072503506, Added by g00273198, 2014/7/25 */
	memset(cmdbuf, 0, strlen(cmdbuf));
	/* END PN:DTS2014072503506, Added by g00273198, 2014/7/25 */
}

static void rx_data(void)
{
    struct usb_request *req = &rx_req;

    req->buf = (void *)((unsigned long) rx_addr);
    req->length = rx_length;
    req->complete = usb_rx_data_complete;
    usb_queue_req(&ep1out, req);
}

void tx_status(const char *status)
{
    struct usb_request *req = &tx_req;
    int len = strlen(status);

    memcpy(req->buf, status, (unsigned int)len);
    req->length = (unsigned int)len;
    req->complete = 0;
    usb_queue_req(&ep1in, req);
}

void fastboot_tx_status(const char *status)
{
    tx_status(status);
    rx_cmd();
}

void tx_dump_page(const char *ptr, int len)
{
    struct usb_request *req = &tx_req;

    memcpy(req->buf, ptr, (unsigned int)len);
    req->length = (unsigned int)len;
    req->complete = 0;
    usb_queue_req(&ep1in, req);
}


#if 0
/*
 *****************************************************************************
 * Prototype    : tx_data
 * Description  : send data to pc,for memory dump
 * Input        : data_buf：  data buffer
                  length：  length of data buffer
 * Output       : None
 * Return Value : None
 * Author       : chenyingguo 61362
 *****************************************************************************
 */
static void tx_data(unsigned int data_buf, unsigned int length)
{
    struct usb_request *req = &tx_req;
    unsigned int data = data_buf;
    unsigned int len = length;
    void * tmp = req->buf;

    while (len) {
        if(len > RX_REQ_LEN) {
            req->buf = (void *)((unsigned long)data);
            req->length = RX_REQ_LEN;
            req->complete = 0;
            len -= RX_REQ_LEN;
            data += RX_REQ_LEN;
            last_one = 0;
        }
        else {
            req->buf = (void *)((unsigned long)data);
            req->length = len;
            req->complete = 0;
            /*last_one = 1;*/
            len = 0;
        }
        usb_queue_req(&ep1in, req);

        /*如果前一次发送超时，直接返回*/
        if(usb_need_reset)
        {
           break;
        }
    }

    req->buf = tmp;
}
#endif

static void usb_rx_data_complete(unsigned actual, int status)
{

    if(status != 0)
        return;

    if(actual > rx_length) {
        actual = rx_length;
    }

    rx_addr += actual;
    rx_length -= actual;

    if(rx_length > 0) {
        rx_data();
    } else {
        tx_status("OKAY");
        rx_cmd();
    }
}

static void usb_status(unsigned online, unsigned highspeed)
{
	if (online) {
		INFO("usb: online (%s)\n", highspeed ? "highspeed" : "fullspeed");
		rx_cmd();
	}
}

void usb_handle_control_request(setup_packet* req)
{
	const void* addr = NULL;
	int size = -1;
	int maxpacket;
	unsigned int data;

	config_bundle.config.bLength = sizeof(struct usb_config_descriptor);
	config_bundle.config.bDescriptorType = USB_DT_CONFIG;
	config_bundle.config.wTotalLength = sizeof(struct usb_config_descriptor) +
		sizeof(struct usb_interface_descriptor) +
		sizeof(struct usb_endpoint_descriptor) * USB_NUM_ENDPOINTS;
	config_bundle.config.bNumInterfaces = 1;
	config_bundle.config.bConfigurationValue = 1;
	config_bundle.config.iConfiguration = 0;
	config_bundle.config.bmAttributes = USB_CONFIG_ATT_ONE;
	config_bundle.config.bMaxPower = 0x80;
	config_bundle.interface.bLength = sizeof(struct usb_interface_descriptor);
	config_bundle.interface.bDescriptorType = USB_DT_INTERFACE;
	config_bundle.interface.bInterfaceNumber = 0;
	config_bundle.interface.bAlternateSetting = 0;
	config_bundle.interface.bNumEndpoints = USB_NUM_ENDPOINTS;
	config_bundle.interface.bInterfaceClass = USB_CLASS_VENDOR_SPEC;
	config_bundle.interface.bInterfaceSubClass = 0x42;
	config_bundle.interface.bInterfaceProtocol = 0x03;
	config_bundle.interface.iInterface = 0;
	config_bundle.ep1.bLength = sizeof(struct usb_endpoint_descriptor);
	config_bundle.ep1.bDescriptorType = USB_DT_ENDPOINT;
	config_bundle.ep1.bEndpointAddress = 0x81;
	config_bundle.ep1.bmAttributes = USB_ENDPOINT_XFER_BULK;
	config_bundle.ep1.wMaxPacketSize = 0;
	config_bundle.ep1.bInterval = 0;
	config_bundle.ep2.bLength = sizeof(struct usb_endpoint_descriptor);
	config_bundle.ep2.bDescriptorType = USB_DT_ENDPOINT;
	config_bundle.ep2.bEndpointAddress = 0x01;
	config_bundle.ep2.bmAttributes = USB_ENDPOINT_XFER_BULK;
	config_bundle.ep2.wMaxPacketSize = 0;
	config_bundle.ep2.bInterval = 1;

	device_descriptor.bLength = sizeof(struct usb_device_descriptor);
	device_descriptor.bDescriptorType = USB_DT_DEVICE;
	device_descriptor.bcdUSB = 0x200;
	device_descriptor.bDeviceClass = 0;
	device_descriptor.bDeviceSubClass = 0;
	device_descriptor.bDeviceProtocol = 0;
	device_descriptor.bMaxPacketSize0 = 0x40;
	device_descriptor.idVendor = 0x18d1;
	device_descriptor.idProduct = 0xd00d;
	device_descriptor.bcdDevice = 0x0100;
	device_descriptor.iManufacturer = 1;
	device_descriptor.iProduct = 2;
	device_descriptor.iSerialNumber = 3;
	device_descriptor.bNumConfigurations = 1;

	VERBOSE("type=%x req=%x val=%x idx=%x len=%x (%s).\n",
		req->type, req->request, req->value, req->index,
		req->length, reqname(req->request));

	switch (req->request) {
	case USB_REQ_GET_STATUS:
		if (req->type == USB_DIR_IN)
			ctrl_resp[0] = 1;
		else
			ctrl_resp[0] = 0;
		ctrl_resp[1] = 0;
		addr = ctrl_resp;
		size = 2;
		break;

	case USB_REQ_CLEAR_FEATURE:
		if ((req->type == USB_RECIP_ENDPOINT) &&
		    (req->value == USB_ENDPOINT_HALT))
			usb_drv_stall(req->index & 0xf, 0, req->index >> 7);
		size = 0;
		break;

	case USB_REQ_SET_FEATURE:
		size = 0;
		break;

	case USB_REQ_SET_ADDRESS:
		size = 0;
		usb_drv_cancel_all_transfers();     // all endpoints reset
		usb_drv_set_address(req->value);   // set device address
		break;

	case USB_REQ_GET_DESCRIPTOR:
		switch (req->value >> 8) {
		case USB_DT_DEVICE:
			addr = &device_descriptor;
			size = sizeof(device_descriptor);
			VERBOSE("Get device descriptor.\n");
			break;

		case USB_DT_OTHER_SPEED_CONFIG:
		case USB_DT_CONFIG:
			if ((req->value >> 8) == USB_DT_CONFIG) {
				maxpacket = usb_drv_port_speed() ? USB_BLOCK_HIGH_SPEED_SIZE : 64;
				config_bundle.config.bDescriptorType = USB_DT_CONFIG;
			} else {
				maxpacket = usb_drv_port_speed() ? 64 : USB_BLOCK_HIGH_SPEED_SIZE;
				config_bundle.config.bDescriptorType = USB_DT_OTHER_SPEED_CONFIG;
			}
			config_bundle.ep1.wMaxPacketSize = maxpacket;
			config_bundle.ep2.wMaxPacketSize = maxpacket;
			addr = &config_bundle;
			size = sizeof(config_bundle);
			VERBOSE("Get config descriptor.\n");
			break;

		case USB_DT_STRING:
			switch (req->value & 0xff) {
			case 0:
				addr = &lang_descriptor;
				size = lang_descriptor.bLength;
				break;
			case 1:
				addr = &string_devicename;
				size = 14;
				break;
			case 2:
				addr = &string_devicename;
				size = string_devicename.bLength;
				break;
			case 3:
				addr = (NULL != serial_string) ? serial_string : (&serial_string_descriptor);
				size = serial_string_descriptor.bLength;
				break;
			default:
				break;
			}
			break;

		default:
			break;
		}
		break;

	case USB_REQ_GET_CONFIGURATION:
		ctrl_resp[0] = 1;
		addr = ctrl_resp;
		size = 1;
		break;

	case USB_REQ_SET_CONFIGURATION:
		usb_drv_cancel_all_transfers();     // call reset_endpoints  reset all EPs

		usb_drv_request_endpoint(USB_ENDPOINT_XFER_BULK, USB_DIR_OUT);
		usb_drv_request_endpoint(USB_ENDPOINT_XFER_BULK, USB_DIR_IN);
		/*
		 * 0x10088800:
		 * 1:EP enable; 8:EP type:BULK; 8:USB Active Endpoint; 8:Next Endpoint
		 */
		data = mmio_read_32(DIEPCTL1) | 0x10088800;
		mmio_write_32(DIEPCTL1, data);
		data = mmio_read_32(DIEPCTL(1)) | 0x08000000;
		mmio_write_32(DIEPCTL(1), data);

		/* Enable interrupts on all endpoints */
		mmio_write_32(DAINTMSK, 0xffffffff);

		usb_status(req->value? 1 : 0, usb_drv_port_speed() ? 1 : 0);
		size = 0;
		VERBOSE("Set config descriptor.\n");

		/* USB 枚举成功点,置上标识 */
		g_usb_enum_flag = 1;
		break;

	default:
		break;
	}

	if (!size) {
		usb_drv_send_nonblocking(0, 0, 0);  // send an empty packet
	} else if (size == -1) { // stall:Applies to non-control, non-isochronous IN and OUT endpoints only.
		usb_drv_stall(0, 1, 1);     // IN
		usb_drv_stall(0, 1, 0);     // OUT
	} else { // stall:Applies to control endpoints only.
		usb_drv_stall(0, 0, 1);     // IN
		usb_drv_stall(0, 0, 0);     // OUT

		usb_drv_send_nonblocking(0, addr, size > req->length ? req->length : size);
	}
}

#if 0
static void dwc2_otg_epint(int idx, int dir_in)
{
	uint32_t epint_reg, epctl_reg, epsiz_reg;
	uint32_t ints, ctrl;

	VERBOSE("%s EP%d\n", (dir_in) ? "IN" : "OUT", idx);
	epint_reg = dir_in ? DIEPINT(idx) : DOEPINT(idx);
	epctl_reg = dir_in ? DIEPCTL(idx) : DOEPCTL(idx);
	epsiz_reg = dir_in ? DIEPTSIZ(idx) : DOEPTSIZ(idx);
	ints = mmio_read_32(DWC_OTG_BASE + epint_reg);
	ctrl = mmio_read_32(DWC_OTG_BASE + epctl_reg);
	/* clear endpoint interrupts */
	mmio_write_32(DWC_OTG_BASE, epint_reg, ints);
}
#endif

void dump_usb_reg(void)
{
#if 0
	int i;
	INFO("GDFIFOCFG:%x, GRXFSIZ:%x, GNPTXFSIZ:%x\n",
		mmio_read_32(GDFIFOCFG), mmio_read_32(GRXFSIZ), mmio_read_32(GNPTXFSIZ));
	for (i = 0; i < 3; i++) {
		INFO("DIEPTXF%d:%x, DTXFSTS%d:%x\n", i, mmio_read_32(DIEPTXF(i)), i, mmio_read_32(DTXFSTS(i)));
	}
	INFO("GAHBCFG:%x, GUSBCFG:%x, DCFG:%x, DCTL:%x\n",
			mmio_read_32(GAHBCFG), mmio_read_32(GUSBCFG), mmio_read_32(DCFG), mmio_read_32(DCTL));
	for (i = 0; i < 3; i++) {
		INFO("DIEPCTL%d:%x, DIEPINT%d:%x, DIEPTSIZ%d:%x, DIEPDMA%d:%x\n",
			i, mmio_read_32(DIEPCTL(i)), i, mmio_read_32(DIEPINT(i)), i, mmio_read_32(DIEPTSIZ(i)), i, mmio_read_32(DIEPDMA(i)));
	}
	for (i = 0; i < 3; i++) {
		INFO("DOEPCTL%d:%x, DOEPINT%d:%x, DOEPTSIZ%d:%x, DOEPDMA%d:%x\n",
			i, mmio_read_32(DOEPCTL(i)), i, mmio_read_32(DOEPINT(i)), i, mmio_read_32(DOEPTSIZ(i)), i, mmio_read_32(DOEPDMA(i)));
	}
#endif
}


/* IRQ handler */
static void usb_poll(void)
{
	uint32_t ints;
	uint32_t epints, data;

	ints = mmio_read_32(GINTSTS);		/* interrupt status */


	if ((ints & 0xc3010) == 0)
		return;
	INFO("%s, %d, GINTSTS:%x, DAINT:%x\n", __func__, __LINE__, ints, mmio_read_32(DAINT));
	dump_usb_reg();
	/*
	 * bus reset
	 * The core sets this bit to indicate that a reset is detected on the USB.
	 */
	if (ints & GINTSTS_USBRST) {
		VERBOSE("bus reset intr\n");
		/*set Non-Zero-Length status out handshake */
		/*
		 * DCFG:This register configures the core in Device mode after power-on
		 * or after certain control commands or enumeration. Do not make changes
		 * to this register after initial programming.
		 * Send a STALL handshake on a nonzero-length status OUT transaction and
		 * do not send the received OUT packet to the application.
		 */
		mmio_write_32(DCFG, 0x800004);
		reset_endpoints();
	}
	/*
	 * enumeration done, we now know the speed
	 * The core sets this bit to indicate that speed enumeration is complete. The
	 * application must read the Device Status (DSTS) register to obtain the
	 * enumerated speed.
	 */
	if (ints & GINTSTS_ENUMDONE) {
		/* Set up the maximum packet sizes accordingly */
		uint32_t maxpacket = usb_drv_port_speed() ? USB_BLOCK_HIGH_SPEED_SIZE : 64;  // high speed maxpacket=512
		VERBOSE("enum done intr. Maxpacket:%d\n", maxpacket);
		//Set Maximum In Packet Size (MPS)
		data = mmio_read_32(DIEPCTL1) & ~0x000003ff;
		mmio_write_32(DIEPCTL1, data | maxpacket);
		//Set Maximum Out Packet Size (MPS)
		data = mmio_read_32(DOEPCTL1) & ~0x000003ff;
		mmio_write_32(DOEPCTL1, data | maxpacket);
	}

#if 0
	dump_usb_reg();
	if (ints & (GINTSTS_IEPINT | GINTSTS_OEPINT)) {
		uint32_t	daint;

		VERBOSE("daint:%x daintmsk:%x, epints:%x\n", mmio_read_32(DAINT), mmio_read_32(DAINTMSK)), epints;
		daint = mmio_read_32(DAINT) & mmio_read_32(DAINTMSK);
		if (daint == 0)
			return;
	}
#endif
#if 1
	/*
	 * IN EP event
	 * The core sets this bit to indicate that an interrupt is pending on one of the IN
	 * endpoints of the core (in Device mode). The application must read the
	 * Device All Endpoints Interrupt (DAINT) register to determine the exact
	 * number of the IN endpoint on which the interrupt occurred, and then read
	 * the corresponding Device IN Endpoint-n Interrupt (DIEPINTn) register to
	 * determine the exact cause of the interrupt. The application must clear the
	 * appropriate status bit in the corresponding DIEPINTn register to clear this bit.
	 */
	if (ints & GINTSTS_IEPINT) {
		epints = mmio_read_32(DIEPINT0);
		mmio_write_32(DIEPINT0, epints);

		VERBOSE("IN EP event,ints:0x%x, DIEPINT0:%x, DAINT:%x, DAINTMSK:%x.\n",
			ints, epints, mmio_read_32(DAINT), mmio_read_32(DAINTMSK));
		if (epints & 0x1) { /* Transfer Completed Interrupt (XferCompl) */
			VERBOSE("TX completed.DIEPTSIZ(0) = 0x%x.\n", mmio_read_32(DIEPTSIZ0));
			/*FIXME,Maybe you can use bytes*/
			/*int bytes = endpoints[0].size - (DIEPTSIZ(0) & 0x3FFFF);*/ //actual transfer
			if (endpoints[0].busy) {
				endpoints[0].busy = 0;//false
				endpoints[0].rc = 0;
				endpoints[0].done = 1;//true
			}
		}
		if (epints & 0x4) { /* AHB error */
			WARN("AHB error on IN EP0.\n");
		}

		if (epints & 0x8) { /* Timeout */
			WARN("Timeout on IN EP0.\n");
			if (endpoints[0].busy) {
				endpoints[0].busy = 1;//false
				endpoints[0].rc = 1;
				endpoints[0].done = 1;//true
			}
		}
	}

	/*
	 * OUT EP event
	 * The core sets this bit to indicate that an interrupt is pending on one of the
	 * OUT endpoints of the core (in Device mode). The application must read the
	 * Device All Endpoints Interrupt (DAINT) register to determine the exact
	 * number of the OUT endpoint on which the interrupt occurred, and then read
	 * the corresponding Device OUT Endpoint-n Interrupt (DOEPINTn) register
	 * to determine the exact cause of the interrupt. The application must clear the
	 * appropriate status bit in the corresponding DOEPINTn register to clear this bit.
	 */
	if (ints & GINTSTS_OEPINT) {
		/* indicates the status of an endpoint
		 * with respect to USB- and AHB-related events. */
		epints = mmio_read_32(DOEPINT(0));
		VERBOSE("OUT EP event,ints:0x%x, DOEPINT0:%x, DAINT:%x, DAINTMSK:%x.\n",
			ints, epints, mmio_read_32(DAINT), mmio_read_32(DAINTMSK));
		if (epints) {
			mmio_write_32(DOEPINT(0), epints);
			/* Transfer completed */
			if (epints & DXEPINT_XFERCOMPL) {
				/*FIXME,need use bytes*/
				VERBOSE("EP0 RX completed. DOEPTSIZ(0) = 0x%x.\n",
					mmio_read_32(DOEPTSIZ(0)));
				if (endpoints[0].busy) {
					endpoints[0].busy = 0;
					endpoints[0].rc = 0;
					endpoints[0].done = 1;
				}
			}
			if (epints & DXEPINT_AHBERR) { /* AHB error */
				WARN("AHB error on OUT EP0.\n");
			}

			/*
			 * IN Token Received When TxFIFO is Empty (INTknTXFEmp)
			 * Indicates that an IN token was received when the associated TxFIFO (periodic/nonperiodic)
			 * was empty. This interrupt is asserted on the endpoint for which the IN token
			 * was received.
			 */
			if (epints & DXEPINT_SETUP) { /* SETUP phase done */
				VERBOSE("Setup phase \n");
				data = mmio_read_32(DIEPCTL(0)) | DXEPCTL_SNAK;
				mmio_write_32(DIEPCTL(0), data);
				data = mmio_read_32(DOEPCTL(0)) | DXEPCTL_SNAK;
				mmio_write_32(DOEPCTL(0), data);
				/*clear IN EP intr*/
				mmio_write_32(DIEPINT(0), ~0);
				usb_handle_control_request((setup_packet *)&ctrl_req);
			}

			/* Make sure EP0 OUT is set up to accept the next request */
			/* memset(p_ctrlreq, 0, NUM_ENDPOINTS*8); */
			mmio_write_32(DOEPTSIZ0, 0x60080040);
			/*
			 * IN Token Received When TxFIFO is Empty (INTknTXFEmp)
			 * Indicates that an IN token was received when the associated TxFIFO (periodic/nonperiodic)
			 * was empty. This interrupt is asserted on the endpoint for which the IN token
			 * was received.
			 */
			// notes that:the compulsive conversion is expectable.
			// Holds the start address of the external memory for storing or fetching endpoint data.
			dma_desc_ep0.status.b.bs = 0x3;
			dma_desc_ep0.status.b.mtrf = 0;
			dma_desc_ep0.status.b.sr = 0;
			dma_desc_ep0.status.b.l = 1;
			dma_desc_ep0.status.b.ioc = 1;
			dma_desc_ep0.status.b.sp = 0;
			dma_desc_ep0.status.b.bytes = 64;
			dma_desc_ep0.buf = (uintptr_t)&ctrl_req;
			dma_desc_ep0.status.b.sts = 0;
			dma_desc_ep0.status.b.bs = 0x0;
			mmio_write_32(DOEPDMA0, (uintptr_t)&dma_desc_ep0);
			// endpoint enable; clear NAK
			mmio_write_32(DOEPCTL0, 0x84000000);
		}

		epints = mmio_read_32(DOEPINT1);
		if(epints) {
			mmio_write_32(DOEPINT1, epints);
			VERBOSE("OUT EP1: epints :0x%x,DOEPTSIZ1 :0x%x.\n",epints, mmio_read_32(DOEPTSIZ1));
			/* Transfer Completed Interrupt (XferCompl);Transfer completed */
			if (epints & DXEPINT_XFERCOMPL) {
				/* ((readl(DOEPTSIZ(1))) & 0x7FFFF is Transfer Size (XferSize) */
				/*int bytes = (p_endpoints + 1)->size - ((readl(DOEPTSIZ(1))) & 0x7FFFF);*/
				int bytes = rx_desc_bytes - dma_desc.status.b.bytes;
				VERBOSE("OUT EP1: recv %d bytes \n",bytes);
				if (endpoints[1].busy) {
					endpoints[1].busy = 0;
					endpoints[1].rc = 0;
					endpoints[1].done = 1;
					rx_req.complete(bytes, 0);
				}
			}

			if (epints & DXEPINT_AHBERR) { /* AHB error */
				WARN("AHB error on OUT EP1.\n");
			}
			if (epints & DXEPINT_SETUP) { /* SETUP phase done */
				WARN("SETUP phase done  on OUT EP1.\n");
			}
		}
	}
#else
	if (ints & (GINTSTS_IEPINT | GINTSTS_OEPINT)) {
		uint32_t	daint, daint_out, daint_in;
		int		ep;

		VERBOSE("daint:%x daintmsk:%x, epints:%x\n", mmio_read_32(DAINT), mmio_read_32(DAINTMSK)), epints;
		daint = mmio_read_32(DAINT) & mmio_read_32(DAINTMSK);
		daint_out = daint >> DAINT_OUTEP_SHIFT;
		daint_in = daint & ~(daint_out << DAINT_OUTEP_SHIFT);
		for (ep = 0; ep < 15 && daint_out; ep++, daint_out >>= 1) {
			if (daint_out & 1)
				dwc2_otg_epint(ep, 0);
		}
		for (ep = 0; ep < 15 && daint_in; ep++, daint_in >>= 1) {
			if (daint_in & 1)
				dwc2_otg_epint(ep, 1);
		}
	}
#endif
	//INFO("%s, %d\n", __func__, __LINE__);
	dump_usb_reg();
	//write clear ints
	mmio_write_32(GINTSTS, ints);
}

#define EYE_PATTERN	0x70533483

/*
* pico phy exit siddq, nano phy enter siddq,
* and open the clock of pico phy and dvc,
*/
static void dvc_and_picophy_init_chip(void)
{
	unsigned int data;

	/* enable USB clock */
	mmio_write_32(PERI_SC_PERIPH_CLKEN0, PERI_CLK_USBOTG);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_CLKSTAT0);
	} while ((data & PERI_CLK_USBOTG) == 0);


	/* out of reset */
	mmio_write_32(PERI_SC_PERIPH_RSTDIS0,
		      PERI_RST_USBOTG_BUS | PERI_RST_PICOPHY |
		      PERI_RST_USBOTG | PERI_RST_USBOTG_32K);
	do {
		data = mmio_read_32(PERI_SC_PERIPH_RSTSTAT0);
		data &= PERI_RST_USBOTG_BUS | PERI_RST_PICOPHY |
			PERI_RST_USBOTG | PERI_RST_USBOTG_32K;
	} while (data);

	mmio_write_32(PERI_SC_PERIPH_CTRL8, EYE_PATTERN);

	/* configure USB PHY */
	data = mmio_read_32(PERI_SC_PERIPH_CTRL4);
	/* make PHY out of low power mode */
	data &= ~PERIPH_CTRL4_PICO_SIDDQ;
	/* detect VBUS by external circuit, switch D+ to 1.5KOhm pullup */
	data |= PERIPH_CTRL4_PICO_VBUSVLDEXTSEL | PERIPH_CTRL4_PICO_VBUSVLDEXT;
	data &= ~PERIPH_CTRL4_FPGA_EXT_PHY_SEL;
	/* select PHY */
	data &= ~PERIPH_CTRL4_OTG_PHY_SEL;
	mmio_write_32(PERI_SC_PERIPH_CTRL4, data);

	udelay(1000);

	data = mmio_read_32(PERI_SC_PERIPH_CTRL5);
	data &= ~PERIPH_CTRL5_PICOPHY_BC_MODE;
	mmio_write_32(PERI_SC_PERIPH_CTRL5, data);
    
	udelay(20000);
}

int init_usb(void)
{
	uint32_t	data;

	memset(&ctrl_req, 0, sizeof(setup_packet));
	memset(&ctrl_resp, 0, 2);
	memset(&dma_desc, 0, sizeof(struct dwc_otg_dev_dma_desc));
	memset(&dma_desc_ep0, 0, sizeof(struct dwc_otg_dev_dma_desc));
	memset(&dma_desc_in, 0, sizeof(struct dwc_otg_dev_dma_desc));

	VERBOSE("Pico PHY and DVC init start.\n");

	dvc_and_picophy_init_chip();
	VERBOSE("Pico PHY and DVC init done.\n");

	/* wait for OTG AHB master idle */
	do {
		data = mmio_read_32(GRSTCTL) & GRSTCTL_AHBIDLE;
	} while (data == 0);
	VERBOSE("Reset usb controller\n");

	/* OTG: Assert software reset */
	mmio_write_32(GRSTCTL, GRSTCTL_CSFTRST);

	/* wait for OTG to ack reset */
	while (mmio_read_32(GRSTCTL) & GRSTCTL_CSFTRST);

	/* wait for OTG AHB master idle */
	while ((mmio_read_32(GRSTCTL) & GRSTCTL_AHBIDLE) == 0);

	VERBOSE("Reset usb controller done\n");

	usb_config();
	VERBOSE("exit usb_init()\n");
	return 0;
}

#define LOCK_STATE_LOCKED		0
#define LOCK_STATE_UNLOCKED		1
#define LOCK_STATE_RELOCKED		2

#define FASTBOOT2
static void usb_rx_cmd_complete(unsigned actual, int stat)
{
#ifdef FASTBOOT2
#define TX_DATA_BUFFER_SIZE    2
#define MAX_RESPONSE_NUMBER	65
	unsigned int image_addr_offset = 0;
	int ret = 0;
#if 0
	unsigned int tx_data_buf[TX_DATA_BUFFER_SIZE];
	unsigned int dump_addr = 0;
	unsigned int dump_length = 0;
	unsigned int dump_id = 0;
	unsigned int image_dnld_offset = 0;
	char cbuf[11];
	int i;
	char *pcmd_buf;

	/* unsigned int self_downlod_flag; */
	char resp[MAX_RESPONSE_NUMBER] = {0};
#endif

	if(stat != 0) return;

	if(actual > 4095)
		actual = 4095;
	cmdbuf[actual] = 0;

	INFO("cmd :%s\n",cmdbuf);

#if 0
	if(LOCK_STATE_LOCKED == fastboot_cmd_lock_state_chk(cmdbuf))
	{
		fastboot_tx_status("FAILCommand not allowed");
		return;
	}

	if(0xFF != fastboot_unlock_execute_func(cmdbuf, resp, (char *)kernel_addr, kernel_size))
	{
		fastboot_tx_status(resp);
		return;
	}
#endif

	if(memcmp(cmdbuf, (void *)"reboot", 6) == 0) {
		tx_status("OKAY");
		INFO("It's reboot command\n");
		for (;;);
#if 0
		emmc_set_card_sleep();
		/* 0x10 means reboot boot defined in preboot.c*/
		ret = 0xF & readl(SECRAM_RESET_ADDR);
		writel(0x10 | ret, SECRAM_RESET_ADDR);

		for (;;) {
			board_reboot();
		}
#endif
	} else if(memcmp(cmdbuf, (void *)"erase:", 6) == 0) {
#if 0
		struct ptentry *ptn = (ptentry *)NULL;

		ptn = find_ptn(cmdbuf + 6);

		if(ptn == 0) {
			tx_status("FAILpartition does not exist");
			rx_cmd();
			return;
		}

		/* can't support erase fastboot image command */
		if(flash_erase(ptn)) {
			tx_status("FAILfailed to erase partition");

			rx_cmd();
			PRINT_INFO(" - FAIL\n");
			return;
		} else {
			PRINT_DEBUG("partition '%s' erased\n", ptn->name);
			PRINT_INFO(" - OKAY\n");
		}
		tx_status("OKAY");

		rx_cmd();
		return;
#endif
	} else if(memcmp(cmdbuf, (void *)"flash:", 6) == 0) {
		struct ptentry *ptn = NULL;

		INFO("recog updatefile\n");

#ifdef FLUSH_SPARSE_IMAGE
		/* 暂时规避B050升级system.img时USB的bug，将system.img分割成多块进行传输，
		   例如分成3块:sysp0、sysp1、sysp_e */
		if (0 == strncmp(cmdbuf + 6,"sysp", 4)) {
			if (0 == strncmp(cmdbuf + 6,"sysp_e", 6)) {
				ptn = find_ptn("system");
				sysp_flag = 0;
				kernel_size += (rx_addr - kernel_addr);
			}
			else {
				sysp_flag = 1;
				INFO(" - OKAY\n");
				tx_status("OKAY");
				rx_cmd();
				return;
			}            
		}
		/*Added by y65256 for V8 Hibench On 2014-6-3 End*/       
		else {
			ptn = find_ptn(cmdbuf + 6);
		}
#else
		ptn = find_ptn(cmdbuf + 6);
#endif

		if(ptn == 0) {
			tx_status("FAILpartition does not exist");
			rx_cmd();
			WARN("updatefile name is error\n");
			return;
		}
		INFO("updatefile name is ok\n");

#ifdef FLUSH_SPARSE_IMAGE
		/* handle sparce image */
		if (((sparse_header_t *)kernel_addr)->magic == SPARSE_HEADER_MAGIC) {


			if (do_unsparse(ptn, (unsigned char*)kernel_addr, kernel_size) ) {
				tx_status("sparse flash write failure");
				rx_cmd();
				INFO(" - FAIL\n");
				return;

			} else {
				INFO(" - OKAY\n");
				tx_status("OKAY");
				rx_cmd();
				return;
			}
		}
#endif


		/* 对于fastboot1，需要计算其VRL的偏移 */
		if (ptn->id == (int)ID_FASTBOOT1) {
			WARN("???ID_FASTBOOT1???\n");
			return;
		} /* 对于fastboot2，需要计算fastboot1以及fastboot2的VRL的偏移 */
		else if (ptn->id == (int)ID_FASTBOOT2) {
			/*DTS2013071301024 support fastboot2 in user data partition*/
			/* change to user data partion  */
			ptn->flags = 0;
		}

		/*只针对fastboot*/
		ptn->start += image_addr_offset;

		/* 处理PTable的流程 */
		if (ptn->id == (int)ID_PTABLE) {
			WARN("???ptable???\n");
			return;
		} else {
#ifdef FLUSH_FOR_USB
			ret = flash_write(ptn, (unsigned int)0,
					(void*)(kernel_addr + image_dnld_offset),
					kernel_size - image_dnld_offset);
#endif

		}

		if (0 != ret) {
			tx_status("FAILflash write failure");
			rx_cmd();
			INFO(" - FAIL\n");
			WARN("updatefile 0x%x is over\n", ptn->id);
		}
		else {
			tx_status("OKAY");
			rx_cmd();
			INFO(" - OKAY\n");
		}
		ptn->start -= image_addr_offset;
		return;
	} else if(memcmp(cmdbuf, (void *)"boot", 4) == 0) {
		INFO(" - OKAY\n");
#if 0
		boot_from_ram(kernel_addr);
		INFO(" - OKAY\n");
		tx_status("OKAY");
		if (execute_load_m3()) {
			fast_boot_exception();
		}
		start_m3();
		{
			enable_cpu123();
			pwrctrl_peripheral_init();
			boot_linux();
		}
#endif

		return;
	}


	tx_status("FAILinvalid command");
	rx_cmd();
#endif
}

static void usbloader_init(void)
{
	VERBOSE("enter usbloader_init\n");

#if 0
	kernel_addr = ANDROID_SYS_MEM_ADDR + 0x3000000; /*48M*/
#endif

	/*usb sw and hw init*/
	init_usb();

	/*alloc and init sth for transfer*/
	ep1in.num = BULK_IN_EP;
	ep1in.in = 1;
	ep1in.req = NULL;
	ep1in.maxpkt = MAX_PACKET_LEN;
	ep1in.next = &ep1in;
	ep1out.num = BULK_OUT_EP;
	ep1out.in = 0;
	ep1out.req = NULL;
	ep1out.maxpkt = MAX_PACKET_LEN;
	ep1out.next = &ep1out;
	cmdbuf = (char *)(rx_req.buf);

	VERBOSE("exit usbloader_init\n");
}

void usb_reinit()
{
    if (usb_need_reset)
    {
       usb_need_reset = 0;
       init_usb();
    }
}

void usb_download(void)
{
	usbloader_init();
	INFO("Enter downloading mode. Please run fastboot command on Host.\n");
	for (;;) {
		usb_poll();
		usb_reinit();
	}
}
