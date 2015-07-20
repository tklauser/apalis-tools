/*
 * List Tegra partition information for a Toradex Apalis T30.
 *
 * Read the proprietary NVIDIA partition table and the GPT in the last sector
 * of the eMMC flash (as flashed when using Toradex Apalis image version 2.1).
 *
 * Based on information from the following sources:
 * - https://github.com/Stuw/ac100-tools/blob/master/nvtegrapart.c
 * - https://github.com/AndroidRoot/BlobTools/blob/master/shared/blob.h
 *
 * Copyright (C) 2015 Tobias Klauser <tklauser@distanz.ch>
 *
 * License: GNU General Public License, version 2
 */

#define _BSD_SOURCE
#define _LARGEFILE64_SOURCE
#include <byteswap.h>
#include <ctype.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uchar.h>
#include <unistd.h>
#include <wchar.h>

#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/types.h>

#define __packed	__attribute__((packed))

#define VERSION		0x00010000
#define MAX_NUM_PARTS	24	/* Could there be more in principle? In reality
				   this should be sufficient */
#define MAX_SIZE	4096	/* partition table repeats after 0x1000 */

#define PT_VERSION	0x00000100
#define BCT_ID		2

#define GPT_BLOCK_SIZE	512	/* GPT logical block size */

static const uint8_t PT_BCT_NAME[4] = { 'B', 'C', 'T', '\0' };
static const uint8_t PT_GPT_NAME[4] = { 'G', 'P', 'T', '\0' };

static const uint8_t GPT_SIGNATURE[8] = { 'E', 'F', 'I', ' ', 'P', 'A', 'R', 'T' };

#define err(fmt, args...)	fprintf(stderr, "Error: " fmt, ##args)

struct nvtegra_partinfo {
	uint32_t	id;
	char		name[4];
	uint32_t	allocation_policy;
	uint32_t	__unknown1;		/* 0x03000000 (some kind of version?) */
	uint32_t	__unknown2;		/* 0x00000000 */
	char		name2[4];
	uint32_t	fs_type;		/* filesystem type */
	uint32_t	__unknown3[3];		/* 0x00000000 */
	uint32_t 	virt_start_sector;	/* virtual start sector */
	uint32_t	__unknown4;
	uint32_t 	virt_size;		/* virtual size */
	uint32_t	__unknown5;
	uint32_t	start_sector;
	uint32_t	__unknown6;
	uint32_t	end_sector;
	uint32_t	__unknown7;
	uint32_t	 type;
	uint32_t 	__unknown8;
} __packed;

struct nvtegra_ptable {
	uint32_t	__unknown1;	/* 0x8b8d9e8 */
	uint32_t	__unknown2;	/* 0xfffffff */
	uint32_t	version;	/* always 0x00010000 */
	uint32_t	table_size;	/* actual size of the partition table in bytes */
	uint8_t		__unknown3[16];	/* looks like a signature or checksum */
	uint8_t		__unknown4[16];	/* always zero? */
	uint8_t		__unknown5[16];	/* copy (backup?) of the first 16 bytes */
	uint32_t	num_parts;	/* number of partitions (TODO: is this really 32 bit?) */
	uint8_t		__unknown6[4];	/* always zero? */
	struct nvtegra_partinfo partitions[MAX_NUM_PARTS];
} __packed;

typedef struct {
	uint32_t 	time_low;
	uint16_t 	time_mid;
	uint16_t 	time_hi_and_version;
	uint8_t		clock_seq_hi_and_reserved;
	uint8_t		clock_seq_low;
	uint8_t		node[6];
} __packed uuid_t;

struct gpt_header {
	uint8_t		signature[8];	/* EFI PART */
	uint32_t	version;
	uint32_t	size;
	uint32_t	crc_self;
	uint32_t	__reserved;
	uint64_t	lba_self;
	uint64_t	lba_alt;
	uint64_t	lba_start;
	uint64_t	lba_end;
	uuid_t		uuid;
	uint64_t	lba_table;
	uint32_t	num_entries;
	uint32_t	entry_size;
	uint32_t	crc_table;
} __packed;

struct gpt_entry {
	uuid_t		type;
	uuid_t		uuid;
	uint64_t	lba_start;
	uint64_t	lba_end;
	uint64_t	attr;
	uint16_t	name[36];
} __packed;

static const char *short_opts = "hv";
static const struct option long_opts[] = {
	{ "help",	no_argument,	NULL,	'h' },
	{ "verbose",	no_argument,	NULL,	'v' },
	{ NULL, 	0,		NULL, 	0 }
};

static void usage_and_exit(int ret)
{
	printf("Usage: nvtegraparts [OPTIONS...] [BOOTDEV [GPTDEV]]\n"
	       "\n"
	       "Options:\n"
	       "  -v, --verbose  Verbose mode (show hexdump of partition tables)\n"
	       "  -h, --help     Show this mesage and exit\n");
	exit(ret);
}

static void nvtegra_partition_print(unsigned int n, const struct nvtegra_partinfo *p)
{
	printf("  #%02u id=%02u [%-3.3s] policy=%u fs=%u virt=0x%08x+0x%08x sectors=0x%08x-0x%08x type=%u\n",
	       n, p->id, p->name, p->allocation_policy, p->fs_type,
	       p->virt_start_sector, p->virt_size,
	       p->start_sector, p->end_sector, p->type);
}

static void c16_to_string(char16_t *buf, char *str, size_t len)
{
	mbstate_t mbs;
	char mbbuf[MB_CUR_MAX];
	size_t pos = 0;

	mbrlen(NULL, 0, &mbs);

	while (buf) {
		size_t ret, i;

		ret = c16rtomb(mbbuf, *buf, &mbs);
		if (ret == 0 || ret > MB_CUR_MAX)
			break;
		for (i = 0; i < ret && pos < len - 1; i++, pos++) {
			str[pos] = mbbuf[i];
		}
		if (pos >= len - 1)
			break;
		buf++;
	}
	str[pos] = '\0';
}

static void le_uuid_dec(const void *buf, uuid_t *uuid)
{
	*((uint32_t *)uuid + 0) = bswap_32(*((const uint32_t *)buf + 0));
	*((uint16_t *)uuid + 2) = bswap_16(*((const uint16_t *)buf + 2));
	*((uint16_t *)uuid + 3) = bswap_16(*((const uint16_t *)buf + 3));
	*((uint64_t *)uuid + 1) = 	  (*((const uint64_t *)buf + 1));
}

#define UUID_FMT 	"%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x"
#define UUID_ARGS(u)	u.time_low, u.time_mid, u.time_hi_and_version, \
			u.clock_seq_hi_and_reserved, u.clock_seq_low, \
			u.node[0], u.node[1], u.node[2], u.node[3], \
			u.node[4], u.node[5]

static void gpt_partition_print(unsigned int n, const struct gpt_entry *e)
{
	char name[20];
	uuid_t type, uuid;
	uint64_t start = le64toh(e->lba_start);
	uint64_t size = le64toh(e->lba_end) - start + 1;

	c16_to_string((char16_t *)e->name, name, sizeof(name));
	le_uuid_dec(&e->type, &type);
	le_uuid_dec(&e->uuid, &uuid);

	printf("  #%02u name=%s type=" UUID_FMT " uuid=" UUID_FMT " attr=0x%" PRIx64 " start=0x%" PRIx64 " size=%" PRIu64 "\n",
	       n, name, UUID_ARGS(type), UUID_ARGS(uuid), le64toh(e->attr), start, size);
}

static void hexdump(const uint8_t *buf, size_t len)
{
	unsigned int i, j;

	for (i = 0, j = 0; i < len; i++) {
		if ((i & 0x0F) == 0)
			printf("%08x", i);
		if ((i & 0x07) == 0)
			printf(" ");
		printf(" %02x", buf[i]);

		if ((i & 0x0F) == 0x0F) {
			printf("  |");
			for ( ; j <= i; j++) {
				uint8_t c = buf[j];
				if (!isprint(c)) {
					c = '.';
				}
				printf("%c", c);
			}
			printf("|\n");
		}
	}
}

static const uint32_t crc32_tab[] = {
	0x00000000, 0x77073096, 0xee0e612c, 0x990951ba, 0x076dc419, 0x706af48f,
	0xe963a535, 0x9e6495a3,	0x0edb8832, 0x79dcb8a4, 0xe0d5e91e, 0x97d2d988,
	0x09b64c2b, 0x7eb17cbd, 0xe7b82d07, 0x90bf1d91, 0x1db71064, 0x6ab020f2,
	0xf3b97148, 0x84be41de,	0x1adad47d, 0x6ddde4eb, 0xf4d4b551, 0x83d385c7,
	0x136c9856, 0x646ba8c0, 0xfd62f97a, 0x8a65c9ec,	0x14015c4f, 0x63066cd9,
	0xfa0f3d63, 0x8d080df5,	0x3b6e20c8, 0x4c69105e, 0xd56041e4, 0xa2677172,
	0x3c03e4d1, 0x4b04d447, 0xd20d85fd, 0xa50ab56b,	0x35b5a8fa, 0x42b2986c,
	0xdbbbc9d6, 0xacbcf940,	0x32d86ce3, 0x45df5c75, 0xdcd60dcf, 0xabd13d59,
	0x26d930ac, 0x51de003a, 0xc8d75180, 0xbfd06116, 0x21b4f4b5, 0x56b3c423,
	0xcfba9599, 0xb8bda50f, 0x2802b89e, 0x5f058808, 0xc60cd9b2, 0xb10be924,
	0x2f6f7c87, 0x58684c11, 0xc1611dab, 0xb6662d3d,	0x76dc4190, 0x01db7106,
	0x98d220bc, 0xefd5102a, 0x71b18589, 0x06b6b51f, 0x9fbfe4a5, 0xe8b8d433,
	0x7807c9a2, 0x0f00f934, 0x9609a88e, 0xe10e9818, 0x7f6a0dbb, 0x086d3d2d,
	0x91646c97, 0xe6635c01, 0x6b6b51f4, 0x1c6c6162, 0x856530d8, 0xf262004e,
	0x6c0695ed, 0x1b01a57b, 0x8208f4c1, 0xf50fc457, 0x65b0d9c6, 0x12b7e950,
	0x8bbeb8ea, 0xfcb9887c, 0x62dd1ddf, 0x15da2d49, 0x8cd37cf3, 0xfbd44c65,
	0x4db26158, 0x3ab551ce, 0xa3bc0074, 0xd4bb30e2, 0x4adfa541, 0x3dd895d7,
	0xa4d1c46d, 0xd3d6f4fb, 0x4369e96a, 0x346ed9fc, 0xad678846, 0xda60b8d0,
	0x44042d73, 0x33031de5, 0xaa0a4c5f, 0xdd0d7cc9, 0x5005713c, 0x270241aa,
	0xbe0b1010, 0xc90c2086, 0x5768b525, 0x206f85b3, 0xb966d409, 0xce61e49f,
	0x5edef90e, 0x29d9c998, 0xb0d09822, 0xc7d7a8b4, 0x59b33d17, 0x2eb40d81,
	0xb7bd5c3b, 0xc0ba6cad, 0xedb88320, 0x9abfb3b6, 0x03b6e20c, 0x74b1d29a,
	0xead54739, 0x9dd277af, 0x04db2615, 0x73dc1683, 0xe3630b12, 0x94643b84,
	0x0d6d6a3e, 0x7a6a5aa8, 0xe40ecf0b, 0x9309ff9d, 0x0a00ae27, 0x7d079eb1,
	0xf00f9344, 0x8708a3d2, 0x1e01f268, 0x6906c2fe, 0xf762575d, 0x806567cb,
	0x196c3671, 0x6e6b06e7, 0xfed41b76, 0x89d32be0, 0x10da7a5a, 0x67dd4acc,
	0xf9b9df6f, 0x8ebeeff9, 0x17b7be43, 0x60b08ed5, 0xd6d6a3e8, 0xa1d1937e,
	0x38d8c2c4, 0x4fdff252, 0xd1bb67f1, 0xa6bc5767, 0x3fb506dd, 0x48b2364b,
	0xd80d2bda, 0xaf0a1b4c, 0x36034af6, 0x41047a60, 0xdf60efc3, 0xa867df55,
	0x316e8eef, 0x4669be79, 0xcb61b38c, 0xbc66831a, 0x256fd2a0, 0x5268e236,
	0xcc0c7795, 0xbb0b4703, 0x220216b9, 0x5505262f, 0xc5ba3bbe, 0xb2bd0b28,
	0x2bb45a92, 0x5cb36a04, 0xc2d7ffa7, 0xb5d0cf31, 0x2cd99e8b, 0x5bdeae1d,
	0x9b64c2b0, 0xec63f226, 0x756aa39c, 0x026d930a, 0x9c0906a9, 0xeb0e363f,
	0x72076785, 0x05005713, 0x95bf4a82, 0xe2b87a14, 0x7bb12bae, 0x0cb61b38,
	0x92d28e9b, 0xe5d5be0d, 0x7cdcefb7, 0x0bdbdf21, 0x86d3d2d4, 0xf1d4e242,
	0x68ddb3f8, 0x1fda836e, 0x81be16cd, 0xf6b9265b, 0x6fb077e1, 0x18b74777,
	0x88085ae6, 0xff0f6a70, 0x66063bca, 0x11010b5c, 0x8f659eff, 0xf862ae69,
	0x616bffd3, 0x166ccf45, 0xa00ae278, 0xd70dd2ee, 0x4e048354, 0x3903b3c2,
	0xa7672661, 0xd06016f7, 0x4969474d, 0x3e6e77db, 0xaed16a4a, 0xd9d65adc,
	0x40df0b66, 0x37d83bf0, 0xa9bcae53, 0xdebb9ec5, 0x47b2cf7f, 0x30b5ffe9,
	0xbdbdf21c, 0xcabac28a, 0x53b39330, 0x24b4a3a6, 0xbad03605, 0xcdd70693,
	0x54de5729, 0x23d967bf, 0xb3667a2e, 0xc4614ab8, 0x5d681b02, 0x2a6f2b94,
	0xb40bbe37, 0xc30c8ea1, 0x5a05df1b, 0x2d02ef8d
};

static uint32_t crc32(const void *buf, size_t len)
{
	const uint8_t *p = buf;
	uint32_t crc = ~0U;

	while (len--)
		crc = crc32_tab[(crc ^ *p++) & 0xFF] ^ (crc >> 8);

	return crc ^ ~0U;
}

int main(int argc, char **argv)
{
	int c, ret = -1;
	bool verbose = false;
	char *boot_dev = "/dev/mmcblk0boot1", *gpt_dev = "/dev/mmcblk0";
	FILE *fp;
	char *buf = NULL, *gpt_buf = NULL;
	ssize_t len;
	struct nvtegra_ptable *pt;
	struct nvtegra_partinfo *p, *gpt;
	unsigned int i;

	while ((c = getopt_long(argc, argv, short_opts, long_opts, NULL)) != -1) {
		switch (c) {
		case 'h':
			usage_and_exit(EXIT_SUCCESS);
		case 'v':
			verbose = true;
			break;
		default:
			usage_and_exit(EXIT_FAILURE);
		}
	}

	if (optind < argc)
		boot_dev = argv[optind];
	if (optind + 1 < argc)
		gpt_dev = argv[optind + 1];

	printf("Using boot device %s, GPT device %s\n", boot_dev, gpt_dev);

	buf = malloc(MAX_SIZE);
	if (!buf) {
		err("Failed to allocate memory\n");
		return -1;
	}

	fp = fopen(boot_dev, "r");
	if (!fp) {
		err("Failed to open file %s: %s\n", boot_dev, strerror(errno));
		goto err_free;
	}

	len = fread(buf, 1, MAX_SIZE, fp);
	fclose(fp);
	if (len != MAX_SIZE) {
		err("Failed to read %u bytes from file: %s\n", MAX_SIZE,
		    strerror(errno));
		goto err_free;
	}

	pt = (struct nvtegra_ptable *) buf;

	if (pt->version != PT_VERSION) {
		err("Invalid partition table version 0x%08x, expected 0x%08x\n",
		    pt->version, PT_VERSION);
		goto err_free;
	}

	printf("nvtegra partition table (%u partitions, size=%u)\n", pt->num_parts, pt->table_size);

	p = &pt->partitions[0];
	nvtegra_partition_print(0, p);

	/* Validate partitioning information (as far as possible) */
	if (p->id != BCT_ID) {
		err("Invalid partition id in BCT, expected %u\n", BCT_ID);
		goto err_free;
	}

	if ((memcmp(p->name, PT_BCT_NAME, sizeof(PT_BCT_NAME)) != 0) ||
	    (memcmp(p->name2, PT_BCT_NAME, sizeof(PT_BCT_NAME)) != 0)) {
		err("Invalid name for BCT, expected %s\n", PT_BCT_NAME);
		goto err_free;
	}

	if (p->start_sector != 0) {
		err("Invalid start sector, expected 0\n");
		goto err_free;
	}

	gpt = NULL;
	for (i = 1; (i < MAX_NUM_PARTS) && (i < pt->num_parts); i++) {
		p = &pt->partitions[i];
		if (p->id >= 128) {
			err("Invalid id %u\n", p->id);
			break;
		}
		nvtegra_partition_print(i, p);
		if ((memcmp(p->name, PT_GPT_NAME, sizeof(PT_GPT_NAME) - 1) == 0) &&
		    (memcmp(p->name2, PT_GPT_NAME, sizeof(PT_GPT_NAME) - 1) == 0))
			gpt = p;
	}

	if (gpt) {
		int fd;
		uint8_t gpt_block[GPT_BLOCK_SIZE];
		struct gpt_header *gpt_h;
		uint32_t crc, crc_self;
		int sector_size;
		unsigned int num_entries;
		size_t gpt_size, blocks, count;
		off64_t ofs;

		fd = open(gpt_dev, O_RDONLY);
		if (fd < 0) {
			err("Failed to open file %s: %s\n", gpt_dev, strerror(errno));
			goto err_free;
		}

		ofs = lseek64(fd, -GPT_BLOCK_SIZE, SEEK_END);
		if (ofs == (off_t)-1) {
			err("Failed to seek to GPT header block: %s\n", strerror(errno));
			close(fd);
			goto err_free;
		}

		len = read(fd, gpt_block, GPT_BLOCK_SIZE);
		if (len != GPT_BLOCK_SIZE) {
			err("Failed to read %u bytes of GPT header: %s\n", GPT_BLOCK_SIZE,
			    strerror(errno));
			close(fd);
			goto err_free;
		}

		gpt_h = (struct gpt_header *) gpt_block;

		/* Validate GPT header */
		if (memcmp(gpt_h->signature, GPT_SIGNATURE, sizeof(GPT_SIGNATURE)) != 0) {
			err("Invalid GPT signature\n");
			close(fd);
			goto err_free;
		}

		crc_self = gpt_h->crc_self;
		gpt_h->crc_self = 0;
		crc = crc32(gpt_h, le32toh(gpt_h->size));
		if (crc != crc_self) {
			err("Invalid GPT header CRC 0x%04x, calculated 0x%04x\n", crc_self, crc);
			close(fd);
			goto err_free;
		}

		if (ioctl(fd, BLKSSZGET, &sector_size) != 0) {
			printf("Failed to get block size, assuming default value 512\n");
			sector_size = 512;
		}

		num_entries = le32toh(gpt_h->num_entries);
		gpt_size = num_entries * le32toh(gpt_h->entry_size);
		blocks = gpt_size / sector_size + ((gpt_size % sector_size) ? 1 : 0);
		count = blocks * sector_size;

		gpt_buf = malloc(count);
		if (!gpt_buf) {
			err("Failed to allocate memory\n");
			close(fd);
			goto err_free;
		}

		ofs = le64toh(gpt_h->lba_table) * sector_size;
		if (lseek64(fd, ofs, SEEK_SET) != ofs) {
			err("Failed to seek to GPT table: %s\n", strerror(errno));
			close(fd);
			goto err_free;
		}

		if (read(fd, gpt_buf, count) != (ssize_t)count) {
			err("Failed to to read GPT table: %s\n", strerror(errno));
			close(fd);
			goto err_free;
		}

		close(fd);

		if (verbose) {
			printf("\nGPT header dump:\n");
			hexdump(gpt_block, len);
		}

		printf("\nGUID partition table (%u partitions, size=%zu, sector 0x%" PRIx64 ", offset 0x%" PRIx64 ")\n",
		       num_entries, gpt_size, le64toh(gpt_h->lba_table), ofs);

		for (i = 0; i < num_entries; i++) {
			struct gpt_entry *gpt_e = (void *)(gpt_buf + i * le32toh(gpt_h->entry_size));
			if (verbose) {
				printf("\nGPT block %u dump:\n", i);
				hexdump((uint8_t *)gpt_e, sizeof(*gpt_e));
			}
			gpt_partition_print(i, gpt_e);
		}
	} else {
		printf("No GPT found or no block device file specified\n");
	}

	ret = 0;
err_free:
	free(buf);
	free(gpt_buf);
	return ret;
}
