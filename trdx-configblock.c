/*
 * Read/write the Toradex configuration block from eMMC
 *
 * Copyright (C) 2015 Tobias Klauser <tklauser@distanz.ch>
 *
 * Based on u-boot Toradex BSP which is:
 *
 * Copyright (c) 2011-2015 Toradex, Inc.
 *
 * License: GNU General Public License, version 2
 */

#define _LARGEFILE64_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>

#define __packed		__attribute__((packed))

#define ARRAY_SIZE(a)		(sizeof(a) / sizeof(a[0]))

#define err(fmt, args...)	fprintf(stderr, "Error: " fmt, ##args)
#define warn(fmt, args...)	fprintf(stderr, "Warning: " fmt, ##args)

#define TRDX_CFG_BLOCK_MAX_SIZE	512
/* Default offset of the 'ARG' partition (for pre v2.3 BSP releases) */
#define DEFAULT_ARG_PART_OFF	0x00000c00
#define DEFAULT_SECTOR_SIZE	4096
/* Config block offset inside the 1st eMMC boot area partition (>= BSP v2.3) */
#define DEFAULT_EMMC_BOOT_OFF	(-512)

struct toradex_tag {
	uint16_t	len:14;
	uint8_t		flags:2;
	uint16_t	id;
} __packed;

#define TAG_VALID	0xcf01
#define TAG_MAC		0x0000
#define TAG_HW		0x0008
#define TAG_FLAG_VALID	0x1

struct toradex_hw {
	uint16_t ver_major;
	uint16_t ver_minor;
	uint16_t ver_assembly;
	uint16_t prodid;
} __packed;

struct toradex_eth_addr {
	uint32_t oui:24;
	uint32_t nic:24;
} __packed;

static const char* const toradex_modules[] = {
	 [0] = "invalid",
	 [1] = "Colibri PXA270 312MHz",
	 [2] = "Colibri PXA270 520MHz",
	 [3] = "Colibri PXA320 806MHz",
	 [4] = "Colibri PXA300 208MHz",
	 [5] = "Colibri PXA310 624MHz",
	 [6] = "Colibri PXA320 806MHz IT",
	 [7] = "Colibri PXA300 208MHz XT",
	 [8] = "Colibri PXA270 312MHz",
	 [9] = "Colibri PXA270 520MHz",
	[10] = "Colibri VF50 128MB", /* not currently on sale */
	[11] = "Colibri VF61 256MB",
	[12] = "Colibri VF61 256MB IT",
	[13] = "Colibri VF50 128MB IT",
	[14] = "Colibri iMX6 Solo 256MB",
	[15] = "Colibri iMX6 DualLite 512MB",
	[16] = "Colibri iMX6 Solo 256MB IT",
	[17] = "Colibri iMX6 DualLite 512MB IT",
	[20] = "Colibri T20 256MB",
	[21] = "Colibri T20 512MB",
	[22] = "Colibri T20 512MB IT",
	[23] = "Colibri T30 1GB",
	[24] = "Colibri T20 256MB IT",
	[25] = "Apalis T30 2GB",
	[26] = "Apalis T30 1GB",
	[27] = "Apalis iMX6 Quad 1GB",
	[28] = "Apalis iMX6 Quad 2GB IT",
	[29] = "Apalis iMX6 Dual 512MB",
	[30] = "Colibri T30 1GB IT",
	[31] = "Apalis T30 1GB IT",
};

static const char *short_opts = "s:h";
static const struct option long_opts[] = {
	{ "skip",	required_argument,	NULL, 's' },
	{ "help",	no_argument,		NULL, 'h' },
	{ NULL, 	0,			NULL, 0 }
};

static void usage_and_exit(int ret)
{
	printf("Usage: trdx-configblock [OPTIONS...] [BLOCKDEV]\n"
	       "\n"
	       "Options:\n"
	       "  -s N[s|b], --skip N[s|b]  Set partition offset to N sectors/bytes\n"
	       "  -h, --help                Show this message and exit\n"
	       "\n"
	       "If BLOCKDEV is omitted, the default locations (according to the BSP release) are searched.\n");
	exit(ret);
}

static int read_config_block(const char *devfile, off_t skip)
{
	int fd, ret = -1;
	size_t read_size, len;
	off_t tag_off = 0, pos;
	uint8_t *config_block = NULL;
	uint32_t serial = 0;
	struct toradex_tag *tag;
	struct toradex_hw hw;
	struct toradex_eth_addr eth_addr;

	fd = open(devfile, O_RDONLY);
	if (fd < 0) {
		err("Failed to open file %s: %s\n", devfile, strerror(errno));
		return -1;
	}

	if ((pos = lseek64(fd, skip, skip < 0 ? SEEK_END : SEEK_SET)) == -1) {
		err("Failed to seek to offset %jd: %s\n", (intmax_t) skip, strerror(errno));
		goto out;
	}

	config_block = malloc(TRDX_CFG_BLOCK_MAX_SIZE);
	if (!config_block) {
		err("Failed to allocate memory\n");
		goto out;
	}

	/* TODO: NAND flash size is different, try to detect which one it is */
	read_size = TRDX_CFG_BLOCK_MAX_SIZE;
	len = read(fd, config_block, read_size);
	if (len < read_size) {
		err("Failed to read %u bytes from file: %s\n", read_size, strerror(errno));
		goto out;
	}

	ret = 0;

	tag = (struct toradex_tag *) config_block;
	if (tag->flags != TAG_FLAG_VALID && tag->id != TAG_VALID) {
		warn("No valid Toradex config block found on %s at 0x%08jx\n",
		     devfile, (intmax_t) pos);
		goto out;
	}
	tag_off = 4;

	printf("Toradex config block found on %s at 0x%08jx\n", devfile,
               (intmax_t) pos);


	memset(&hw, 0, sizeof(hw));
	memset(&eth_addr, 0, sizeof(eth_addr));

	while (true) {
		tag = (struct toradex_tag *)(config_block + tag_off);
		if (tag->flags != TAG_FLAG_VALID)
			break;

		tag_off += 4;
		switch (tag->id) {
		case TAG_MAC:
			memcpy(&eth_addr, config_block + tag_off, sizeof(eth_addr));
			/* NIC part of MAC address is serial number */
			serial = ntohl(eth_addr.nic) >> 8;
			break;
		case TAG_HW:
			memcpy(&hw, config_block + tag_off, sizeof(hw));
			break;
		default:
			warn("Unknown tag id 0x%04x found in Toradex config block\n", tag->id);
			break;
		}

		tag_off += tag->len * 4;
	}

	printf("Model:  Toradex %s V%d.%d%c\n", toradex_modules[hw.prodid],
	       hw.ver_major, hw.ver_minor, (char)hw.ver_assembly + 'A');
	printf("Serial: %08d\n", serial);
	printf("MAC:    %02x:%02x:%02x:%02x:%02x:%02x\n",
	       (uint8_t)((eth_addr.oui & 0x0000ff) >> 0),
	       (uint8_t)((eth_addr.oui & 0x00ff00) >> 8),
	       (uint8_t)((eth_addr.oui & 0xff0000) >> 16),
	       (uint8_t)((eth_addr.nic & 0x0000ff) >> 0),
	       (uint8_t)((eth_addr.nic & 0x00ff00) >> 8),
	       (uint8_t)((eth_addr.nic & 0xff0000) >> 16));

out:
	free(config_block);
	close(fd);
	return ret;
}

int main(int argc, char **argv)
{
	int c, ret;
	off_t skip = DEFAULT_ARG_PART_OFF;
	bool skip_set = false;
	char *devfile = NULL;
	enum {
		UNIT_SECTORS,
		UNIT_BYTES,
	} units = UNIT_SECTORS;

	if (argc == 1) {
	}

	/* If arguments are given, use the specified device/offset */
	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
		switch (c) {
		case 's':
			if (optarg[strlen(optarg) - 1] == 'b')
				units = UNIT_BYTES;
			else if (optarg[strlen(optarg) - 1] == 's')
				units = UNIT_SECTORS;
			skip = (off64_t) strtoll(optarg, NULL, 0);
			skip_set = true;
			break;
		case 'h':
			usage_and_exit(EXIT_SUCCESS);
		default:
			usage_and_exit(EXIT_FAILURE);
		}
	}

	if (optind < argc)
		devfile = argv[optind];

	if (units == UNIT_SECTORS)
		skip *= DEFAULT_SECTOR_SIZE;

	if (!devfile) {
		/* Toradex BSP >= 2.3 stores the config block in the last sector
		 * of the first boot partition */
		ret = read_config_block("/dev/mmcblk0boot0", skip_set ? skip : DEFAULT_EMMC_BOOT_OFF);
		if (ret != 0)
			ret = read_config_block("/dev/mmcblk0", skip_set ? skip : (DEFAULT_ARG_PART_OFF * DEFAULT_SECTOR_SIZE));
	} else
		ret = read_config_block(devfile, skip);

	return ret;
}
