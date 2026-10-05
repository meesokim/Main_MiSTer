//============================================================================
//  ZMX Service for Main_MiSTer
//  Polls MSX FPGA core via EXT_BUS SPI and bridges transactions to zmxdrive
//============================================================================

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

#include "zmx_service.h"
#include "../../user_io.h"
#include "../../fpga_io.h"

// ZMXDrive C API symbols
extern "C" {
	void init(char path[]);
	unsigned char msxread(int cmd, unsigned short addr);
	void msxwrite(int cmd, unsigned short addr, unsigned char value);
	void reset();
	int zmx_mount_rom(const char *filepath);
	void zmx_umount_rom();
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

static unsigned long total_polls = 0;
static unsigned long total_tx = 0;
static unsigned long total_slot_tx = 0;
static uint16_t last_status = 0;
static unsigned long total_io_tx = 0;
static unsigned long total_mem_rd = 0;
static unsigned long total_mem_wr = 0;
static uint8_t last_cmd = 0;
static uint16_t last_addr = 0;
static uint8_t last_wdata = 0;
static uint8_t last_rdata = 0;
static uint8_t last_slot_cmd = 0;
static uint16_t last_slot_addr = 0;
static uint8_t last_slot_wdata = 0;
static uint8_t last_slot_rdata = 0;
static time_t last_report_time = 0;

struct TxHistory {
	uint8_t cmd;
	uint16_t addr;
	uint8_t wdata;
	uint8_t rdata;
};
static TxHistory history[8] = {};
static int hist_idx = 0;
static char last_mounted_rom[256] = "None";

static void zmx_update_diagnostic_file(int enabled)
{
	time_t now = time(NULL);
	if (now == last_report_time) return;
	last_report_time = now;

	FILE *fp = fopen("/tmp/zmx_status.log", "w");
	if (fp) {
		uint8_t magic = (last_status >> 8) & 0xFF;
		fprintf(fp, "=== ZMX Bridge Diagnostic Status ===\n");
		fprintf(fp, "Bridge Enabled (OSD [19:18]): %d\n", enabled);
		fprintf(fp, "ZMX Service Active:          %d\n", zmx_active);
		fprintf(fp, "Mounted Slot 1 ROM:          %s\n", last_mounted_rom);
		fprintf(fp, "FPGA SPI Link:               %s (status=0x%04X, magic=0x%02X)\n",
			(magic == 0xA5) ? "OK (Magic 0xA5 confirmed)" : "WAITING / NO_REPLY (got 0x0000)",
			last_status, magic);
		fprintf(fp, "Total SPI Polls:             %lu\n", total_polls);
		fprintf(fp, "Total MSX Bus Transactions:  %lu (IO=%lu, MemRD=%lu, MemWR=%lu)\n",
			total_tx, total_io_tx, total_mem_rd, total_mem_wr);
		fprintf(fp, "Slot Memory Transactions:    %lu\n", total_slot_tx);
		fprintf(fp, "Last Any Access:             cmd=0x%02X, addr=0x%04X, wdata=0x%02X -> rdata=0x%02X\n",
			last_cmd, last_addr, last_wdata, last_rdata);
		fprintf(fp, "Last Slot Access:            cmd=0x%02X, addr=0x%04X, wdata=0x%02X -> rdata=0x%02X\n",
			last_slot_cmd, last_slot_addr, last_slot_wdata, last_slot_rdata);
		fprintf(fp, "Recent Transactions History:\n");
		for (int i = 0; i < 8; i++) {
			int idx = (hist_idx + i) % 8;
			if (history[idx].cmd != 0 || history[idx].addr != 0) {
				fprintf(fp, "  [%d] cmd=0x%02X, addr=0x%04X, wdata=0x%02X -> rdata=0x%02X\n",
					i, history[idx].cmd, history[idx].addr, history[idx].wdata, history[idx].rdata);
			}
		}
		fprintf(fp, "Timestamp:                   %s\n", ctime(&now));
		fclose(fp);
	}
}

void zmx_service_poll()
{
	static uint32_t poll_tick = 0;
	static uint32_t enabled = 1;

	// Check status periodically every ~512 polls (~20-50ms) to avoid overhead
	if ((++poll_tick & 0x1FF) == 0) {
		enabled = user_io_status_get("[19:18]");
		zmx_update_diagnostic_file(enabled);
	}

	if (!enabled) {
		if (zmx_active) zmx_service_stop();
		return;
	}

	if (!zmx_active) {
		zmx_service_init("/media/fat/games/MSX");
	}

	// High-performance adaptive spin-burst loop:
	// While MSX CPU is actively fetching instructions/data from cartridge,
	// stay in tight polling loop without relinquishing to main loop.
	// Max consecutive transactions per burst: 2048
	// Max idle spin polls before relinquishing: 48 (~40-60us)
	int tx_count = 0;
	int idle_count = 0;

	while (tx_count < 2048 && idle_count < 48) {
		total_polls++;
		// 1. Poll FPGA for pending transaction: returns [15:8] = 0xA5 (magic), [7:1] = cmd, bit 0 = pending
		spi_uio_cmd_cont(CMD_ZMX_POLL);
		uint16_t status = spi_w(0);
		DisableIO();

		last_status = status;
		uint8_t pending = status & 0x01;
		if (!pending) {
			idle_count++;
			continue;
		}

		// Reset idle count when a valid transaction is detected
		idle_count = 0;
		tx_count++;

		uint8_t cmd = (status >> 1) & 0x7F;

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

		total_tx++;
		if (cmd == 0x02 || cmd == 0x03) total_io_tx++;
		else if (cmd == 0x00 || cmd == 0x10) total_mem_rd++;
		else if (cmd == 0x01 || cmd == 0x11) total_mem_wr++;

		last_cmd = cmd;
		last_addr = addr;
		last_wdata = wdata;
		last_rdata = rdata;

		// Any slot memory transaction (RD_SLTSL1, RD_SLTSL2, WR_SLTSL1, WR_SLTSL2)
		if (cmd == 0x00 || cmd == 0x10 || cmd == 0x01 || cmd == 0x11) {
			total_slot_tx++;
			last_slot_cmd = cmd;
			last_slot_addr = addr;
			last_slot_wdata = wdata;
			last_slot_rdata = rdata;
		}
	}
}

void zmx_service_stop()
{
	if (zmx_active) {
		zmx_active = 0;
		reset();
		printf("[ZMX] Service stopped\n");
	}
}

int zmx_service_mount(const char *filepath)
{
	if (!filepath || !strlen(filepath)) return 0;

	// Ensure service is initialized
	if (!zmx_active) {
		char dir[512] = "/media/fat/games/MSX";
		zmx_service_init(dir);
	}

	// Auto-enable ZMX Bridge to Slot 1 if disabled
	uint32_t enabled = user_io_status_get("[19:18]");
	if (enabled == 0) {
		user_io_status_set("[19:18]", 1);
		printf("[ZMX] Auto-enabled ZMX Bridge to Slot 1\n");
	}

	// Reset slot transaction log for clean boot capture
	unlink("/tmp/zmx_slot_boot.log");
	total_slot_tx = 0;

	int ret = zmx_mount_rom(filepath);
	if (ret) {
		snprintf(last_mounted_rom, sizeof(last_mounted_rom), "%s (Mounted OK)", filepath);
		printf("[ZMX] Successfully mounted ROM to Slot 1: %s\n", filepath);
	} else {
		snprintf(last_mounted_rom, sizeof(last_mounted_rom), "%s (Load Failed)", filepath);
		printf("[ZMX] Failed to mount ROM: %s\n", filepath);
	}
	return ret;
}

void zmx_service_umount()
{
	zmx_umount_rom();
}

