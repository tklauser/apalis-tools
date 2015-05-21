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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define __packed	__attribute__((packed))

#define VERSION		0x00010000
#define MAX_NUM_PARTS	24	/* Could there be more in principle? In reality
				   this should be sufficient */
#define MAX_SIZE	4096	/* partition table repeats after 0x1000 */

#define PT_VERSION	0x00000100
#define BCT_ID		2
#define BCT_NAME	"BCT\0"

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
	uint32_t	__unknown2;	/* fffffffff */
	uint32_t	version;	/* always 0x00010000 */
	uint32_t	table_size;	/* actual size of the partition table in bytes */
	uint8_t		__unknown3[16];	/* looks like a signature or checksum */
	uint8_t		__unknown4[16];	/* always zero? */
	uint8_t		__unknown5[16];	/* copy (backup?) of the first 16 bytes */
	uint32_t	num_parts;	/* number of partitions (TODO: is this really 32 bit?) */
	uint8_t		__unknown6[4];	/* always zero? */
	struct nvtegra_partinfo partitions[MAX_NUM_PARTS];
} __packed;

static void nvtegra_partition_print(unsigned int n, const struct nvtegra_partinfo *p)
{
	printf("  #%02u id=%02u [%-3.3s] policy=%u fs=%u virt=%08x+%08x sectors=%08x-%08x type=%u\n",
	       n, p->id, p->name, p->allocation_policy, p->fs_type,
	       p->virt_start_sector, p->virt_size,
	       p->start_sector, p->end_sector, p->type);
}

int main(int argc, char **argv)
{
	int ret = -1;
	FILE *fp;
	char *buf;
	struct nvtegra_ptable *pt;
	struct nvtegra_partinfo *p;
	unsigned int i;

	if (argc < 2) {
		printf("Usage: nvtegraparts IMAGE\n");
		return -1;
	}

	fp = fopen(argv[1], "r");
	if (!fp) {
		err("Failed to open file %s: %s\n", argv[1], strerror(errno));
		return -1;
	}

	buf = malloc(MAX_SIZE);
	if (!buf) {
		err("Failed to allocate memory\n");
		goto err_fclose;
	}

	if (fread(buf, 1, MAX_SIZE, fp) != MAX_SIZE) {
		err("Failed to read %u bytes from image: %s\n", MAX_SIZE,
		    strerror(errno));
		goto err_free;
	}

	pt = (struct nvtegra_ptable *) buf;

	if (pt->version != PT_VERSION) {
		err("Invalid partition table version %08x, expected %08x\n",
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

	if ((memcmp(p->name, BCT_NAME, sizeof(BCT_NAME) - 1) != 0) ||
	    (memcmp(p->name2, BCT_NAME, sizeof(BCT_NAME) - 1) != 0)) {
		err("Invalid name for BCT, expected %s\n", BCT_NAME);
		goto err_free;
	}

	if (p->start_sector != 0) {
		err("Invalid start sector, expected 0\n");
		goto err_free;
	}

	for (i = 1; (i < MAX_NUM_PARTS) && (i < pt->num_parts); i++) {
		p = &pt->partitions[i];
		if (p->id >= 128) {
			err("Invalid id %u\n", p->id);
			break;
		}
		nvtegra_partition_print(i, p);
	}

	ret = 0;
err_free:
	free(buf);
err_fclose:
	fclose(fp);
	return ret;
}
