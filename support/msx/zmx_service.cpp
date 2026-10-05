//============================================================================
//  ZMX Service for Main_MiSTer
//  Polls MSX FPGA core via EXT_BUS SPI and bridges transactions to zmxdrive
//============================================================================

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include "zmx_service.h"
#include "../../user_io.h"
#include "../../fpga_io.h"

// ZMXDrive C API symbols
extern "C" {
	void init(char path[]);
	unsigned char msxread(int cmd, unsigned short addr);
	void msxwrite(int cmd, unsigned short addr, unsigned char value);
	void reset();
}

// SPI command IDs matching rtl/peripheral/zmx_bridge.sv
#define CMD_ZMX_POLL 0x68
#define CMD_ZMX_ADDR 0x69
#define CMD_ZMX_RESP 0x6A

static int zmx_active = 0;

void zmx_service_init(const char *rom_path)
{
	char path[512] = ".";
	if (rom_path && strlen(rom_path)) {
		strncpy(path, rom_path, sizeof(path) - 1);
		path[sizeof(path) - 1] = '\0';
	}

	init(path);
	zmx_active = 1;
	printf("[ZMX] Service initialized with path: %s\n", path);
}

void zmx_service_poll()
{
	// Check OSD status (OIJ: ZMX Bridge, 0=Disabled, 1=Slot 1, 2=Slot 2)
	uint32_t enabled = user_io_status_get("OIJ");
	if (!enabled) {
		if (zmx_active) zmx_service_stop();
		return;
	}

	if (!zmx_active) {
		zmx_service_init("/media/fat/games/MSX");
	}

	// 1. Poll FPGA for pending transaction: returns [15:8] = cmd, [7:0] = flags (bit 0 = pending)
	spi_uio_cmd_cont(CMD_ZMX_POLL);
	uint16_t status = spi_w(0);
	DisableIO();

	uint8_t pending = status & 0x01;
	uint8_t cmd     = (status >> 8) & 0xFF;

	if (!pending) return;

	// 2. Read 16-bit address and write data from FPGA
	spi_uio_cmd_cont(CMD_ZMX_ADDR);
	uint16_t addr       = spi_w(0);
	uint16_t wdata_word = spi_w(0);
	DisableIO();

	uint8_t wdata = wdata_word & 0xFF;

	// 3. Process with zmxdrive engine
	uint8_t rdata = 0xFF;
	if (cmd == 0x01 || cmd == 0x11 || cmd == 0x03) {
		// Memory Write (WR_SLTSL1, WR_SLTSL2) or IO Write (WR_IO)
		msxwrite(cmd, addr, wdata);
	} else {
		// Memory Read (RD_SLTSL1, RD_SLTSL2) or IO Read (RD_IO)
		rdata = msxread(cmd, addr);
	}

	// 4. Send response to FPGA (clears pending bit and releases CPU WAIT)
	spi_uio_cmd_cont(CMD_ZMX_RESP);
	spi_w(rdata);
	DisableIO();
}

void zmx_service_stop()
{
	if (zmx_active) {
		zmx_active = 0;
		reset();
		printf("[ZMX] Service stopped\n");
	}
}
