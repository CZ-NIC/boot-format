/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc.
 *
 * Author: Chen Gong
 * 	   Mingkai Hu <Mingiak.hu@freescale.com>
 *     Jimmy Zhao <jimmy.zhao@freescale.com>
 *  Rev: 1.0    Initial release
 *       1.1:   Bug fix for generating SPI image
 *              All the source code offset is byte mode instead of block mode
 *
 * This file is used for putting config words and U-Boot image to SDCard
 * when boot from SDCard, or to SPI image when boot from SPI flash. This
 * code puts the config words into the 1st partition, so please ensure
 * the size is enough on the 1st partition.
 *
 * NOTE: DON'T support FAT12 (it looks obsolete)
 *       DON'T support 64bit machine (it looks not necessary)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#define _LARGEFILE64_SOURCE
#define _GNU_SOURCE
#define DEBUG 1

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <getopt.h>
#include <errno.h>
#include <linux/hdreg.h>
#include <sys/ioctl.h>
#include "boot_format.h"

/*
 * Print data buffer in hex and ascii form to the terminal.
 *
 * parameters:
 *    data: pointer to data buffer
 *    len: data length
 *    width: data value width.  May be 1, 2, or 4.
 */
static void print_buf(void *data, uint len, uint width)
{
#ifdef DEBUG
	uint i;
	uint *uip = (uint *)data;
	ushort *usp = (ushort *)data;
	uchar *ucp = (uchar *)data;

	debug("\n");
	for (i = 0; i < len/width; i++) {
		if ((i % (16/width)) == 0)
			debug("0x%04x:", i);

		if (width == 4)
			debug(" %08x", uip[i]);
		else if (width == 2)
			debug(" %04x", usp[i]);
		else
			debug(" %02x", ucp[i]);

		if (((i+1) % (16/width)) == 0)
			debug("\n");
	}
	debug("\n");
#endif
}

static void print_mbr(struct mbr_disk_part_tbl *mbr_dpt)
{
#ifdef DEBUG
	debug("\n================== MBR ==================\n");
	debug("boot_ind         = 0x%x\n", mbr_dpt->boot_ind);
	debug("start_head       = 0x%x\n", mbr_dpt->start_head);
	debug("start_cylesec    = 0x%x\n", mbr_dpt->start_cylsec);
	debug("part_type        = 0x%x\n", mbr_dpt->part_type);
	debug("end_head         = 0x%x\n", mbr_dpt->end_head);
	debug("end_cylsec       = 0x%x\n", mbr_dpt->end_cylsec);
	debug("rel_sectors      = 0x%x\n", mbr_dpt->rel_sectors);
	debug("total_sectors    = 0x%x\n", mbr_dpt->total_sectors);
	debug("=========================================\n");
#endif
}

static void print_dbr(struct boot_sector *dbr)
{
#ifdef DEBUG
	debug("\n================== DBR ==================\n");
	debug("jmp_code[0]      = 0x%x\n", dbr->jmp_code[0]);

	debug("\n");
	debug("sector_size      = 0x%x\n", dbr->bpb.sector_size);
	debug("root_entries     = 0x%x\n", dbr->bpb.root_entries);
	debug("small_sector     = 0x%x\n", dbr->bpb.small_sectors);
	debug("sectors_p_fat    = 0x%x\n", dbr->bpb.sectors_p_fat);
	debug("=========================================\n");
#endif
}

static uint get_endian_mode(void)
{
	ushort us = 0x1122;
	uchar uc = *(uchar *)&us;
	if (uc == 0x11)
		return BIG_ENDIAN_MODE;
	else
		return LITTLE_ENDIAN_MODE;
}

static void swap_mbr(struct mbr_disk_part_tbl *mbr_dpt)
{
	int i;
	
	for (i = 0; i < 4; i++) {
		mbr_dpt->start_cylsec = SWAP16(mbr_dpt->start_cylsec);
		mbr_dpt->end_cylsec = SWAP16(mbr_dpt->end_cylsec);
		mbr_dpt->rel_sectors = SWAP32(mbr_dpt->rel_sectors);
		mbr_dpt->total_sectors = SWAP32(mbr_dpt->total_sectors);
	}
}

static void swap_dbr(struct boot_sector *dbr)
{
	dbr->bpb.sector_size = SWAP16(dbr->bpb.sector_size);
	dbr->bpb.reserved_sectors = SWAP16(dbr->bpb.reserved_sectors);
	dbr->bpb.root_entries = SWAP16(dbr->bpb.root_entries);
	dbr->bpb.small_sectors = SWAP16(dbr->bpb.small_sectors);
	dbr->bpb.sectors_p_fat = SWAP16(dbr->bpb.sectors_p_fat);
	dbr->bpb.sector_p_track = SWAP16(dbr->bpb.sector_p_track);
	dbr->bpb.heads = SWAP16(dbr->bpb.heads);
	dbr->bpb.hidden_sectors = SWAP32(dbr->bpb.hidden_sectors);
	dbr->bpb.large_sectors = SWAP32(dbr->bpb.large_sectors);

	dbr->fat16.ext_bpb.vol = SWAP32(dbr->fat16.ext_bpb.vol);

	dbr->fat32.ext.ext_flag = SWAP16(dbr->fat32.ext.ext_flag);
	dbr->fat32.ext.fs_version = SWAP16(dbr->fat32.ext.fs_version);
	dbr->fat32.ext.root_cluster = SWAP32(dbr->fat32.ext.root_cluster);
	dbr->fat32.ext.fs_info = SWAP16(dbr->fat32.ext.fs_info);
	dbr->fat32.ext.backup_boot_sector =
		SWAP16(dbr->fat32.ext.backup_boot_sector);
}

static inline ushort cylsec_to_sec(ushort cylsec)
{
	return cylsec & 0x3F;
}

static inline ushort cylsec_to_cyl(ushort cylsec)
{
	return ((cylsec & 0xC0) << 2) | ((cylsec & 0xFF00) >> 8);
}

static inline ushort back_to_cylsec(ushort sec, ushort cyl)
{
    return ((sec & 0x3F) || ((cyl & 0x300)>> 2) || ((cyl & 0xFF)<<8));
}


static bool get_disk_geometry(int fd, struct hd_geometry *geometry)
{
	bool success = true;

	if (ioctl(fd, HDIO_GETGEO, geometry)) {
		// Fall back to reasonable defaults for modern systems
		geometry->heads = 255;
		geometry->sectors = 63;
	}

	debug("Using disk geometry of %u heads and %u sectors/track for CHS translation\n", geometry->heads, geometry->sectors);

	return (success);
}

static bool chs2lba(int fd, const struct hd_geometry *geometry, uint c, uint h, uint s, uint *sector)
{
	bool success = false;

	// Finding the first sector of a partition is less easy than it looks.
	// Technically, we have to start with CHS and only use the LBA entry
	// if CHS doesn't work. This is a little bit annoying.
	if((h == 0xff) || (s == 0)) {
		// As this CHS value is effectively invalid, it is
		// our indication to keep using the LBA value that may have
		// been preset in the sectors variable by the caller
		success = false;
	} else {
		// Now we use CHS!
		*sector = (c * geometry->heads + h) * geometry->sectors + (s - 1);
		success = true;
	}


	return(success);
}

static void first_last_partition_on_disk(int fd, const struct hd_geometry *geometry, struct mbr_disk_part_tbl *dbr, struct mbr_disk_part_tbl **first_partition, struct mbr_disk_part_tbl **last_partition)
{
	uint first_s = 0xffffffff, last_s = 0, c, h, s;
	int i;

	*first_partition = NULL;
	*last_partition= NULL;
	
	// Find the physically first partition on disk, which might not be the
	// first entry!
	if(dbr) {
		for(i = 0; i < 4; ++i) {
			if ((dbr[i].part_type != 0) &&
			    ((dbr[i].boot_ind & 0x7f) == 0) &&
			    (dbr[i].total_sectors)) {
				uint lbasec = dbr->rel_sectors;

				// if CHS is valid, we have to override!
				c = cylsec_to_cyl(dbr->start_cylsec);
				h = dbr->start_head;
				s = cylsec_to_sec(dbr->start_cylsec);
				if(chs2lba(fd, geometry, c, h, s, &lbasec)) {
					// The following is a bit of a hack.
					// We fill in the LBA value to make
					// sure subsequent code can use it.
					if(dbr->rel_sectors != lbasec)
						printf("WARNING! CHS geometry for partition points to sector %u while LBA entry in MBR points to %u. Updated LBA value with CHS value!\n", lbasec, dbr->rel_sectors);
					dbr->rel_sectors = lbasec;

				}

				if(lbasec < first_s) {
					*first_partition = &dbr[i];
					first_s = lbasec;
				}

				// Now check the partition end
				lbasec = dbr->rel_sectors + dbr->total_sectors - 1;

				// if CHS is valid, we have to override!
				c = cylsec_to_cyl(dbr->end_cylsec);
				h = dbr->start_head;
				s = cylsec_to_sec(dbr->end_cylsec);
				chs2lba(fd, geometry, c, h, s, &lbasec);

				if(lbasec > last_s) {
					*last_partition = &dbr[i];
					last_s = lbasec;
				}
			}
		}
	}

}

/* NOTE: some formatted card hasn't MBR, only DBR */
static struct mbr_disk_part_tbl *parse_mbr(uchar *sd_data)
{
	struct mbr_disk_part_tbl *mbr_dpt = NULL;

	if ((sd_data[MBRDBR_BOOT_SIG_55] == 0x55) &&
	    (sd_data[MBRDBR_BOOT_SIG_AA] == 0xAA)) {
		int i;

		mbr_dpt = (struct mbr_disk_part_tbl *)(sd_data + MBR_DPT_OFF); //Partition Table

		/* Check the partition table. If it is obviously invalid,
		 * we don't have a good MBR */
		for (i = 0; i < 4; i++) {
			if ((mbr_dpt[i].part_type != 0) &&
			    ((mbr_dpt[i].boot_ind != 0 && mbr_dpt[i].boot_ind != 0x80) ||
			     (!mbr_dpt[i].total_sectors))) {
				mbr_dpt = NULL;
				break;
			}
		}
	}
		
	debug((mbr_dpt) ? "It is a valid MBR\n" : "It is not a valid MBR\n");

	return(mbr_dpt);

}

static bool is_fat_partition(struct mbr_disk_part_tbl *mbr_dpt)
{
	if (mbr_dpt) {
		/* We only know about FAT partitions, so we ignore all other types */
		if ((mbr_dpt->part_type == 0x01) ||
		    (mbr_dpt->part_type == 0x04) ||
		    (mbr_dpt->part_type == 0x06) ||
		    // Special LBA mapped Win 95 types
		    (mbr_dpt->part_type == 0x0B) ||
		    (mbr_dpt->part_type == 0x0C) ||
		    (mbr_dpt->part_type == 0x0E) ||
		    (mbr_dpt->part_type == 0x0F) ||
		    // Logical sectored FAT types, very unusual but technically valid
		    (mbr_dpt->part_type == 0x08) ||
		    (mbr_dpt->part_type == 0x11) ||
		    (mbr_dpt->part_type == 0x14) ||
		    (mbr_dpt->part_type == 0x24) ||
		    (mbr_dpt->part_type == 0x56) ||
		    (mbr_dpt->part_type == 0xe5) ||
		    (mbr_dpt->part_type == 0xf2))
			return(true);
	}

	return(false);
}

static struct boot_sector *parse_dbr(struct mbr_disk_part_tbl *mbr_dpt, uchar *boot_sect_buf)
{
	struct boot_sector *dbr = (struct boot_sector *)boot_sect_buf;

	print_dbr(dbr);

	if (mbr_dpt) {
		/* We only know about FAT partitions, so we ignore all other types */
		if(!is_fat_partition(mbr_dpt))
			dbr = NULL;
	}
	
	if (dbr) {
		if ((boot_sect_buf[MBRDBR_BOOT_SIG_55] != 0x55) ||
		    (boot_sect_buf[MBRDBR_BOOT_SIG_AA] != 0xAA) ||
		    (dbr->jmp_code[0] != 0xEB) ||
		    (dbr->bpb.sector_size != SECTOR_SIZE)) {
			dbr = NULL;
		}
	}

	debug((dbr) ? "It is a valid DBR\n" : "It is not a valid DBR\n");

	return dbr;
}

static int write_config_file(char *out, int num, struct config_word *pword[])
{
	struct stat sb;
	int h_config, i, n;
	char buf[20], *format;

	stat(out, &sb);
	if ((errno == ENOENT) || S_ISREG(sb.st_mode)) {
		h_config = open(out, O_WRONLY|O_CREAT|O_TRUNC, 0666);
		if (h_config < 0) {
			printf(MSG_OPEN_FILE_FAIL, out);
			return errno;
		}
		format = "%02x:%08x\n"; /* Backwards compatibility */
		for (i = 0; i < num; i++) {
			if (pword[i]->off >= 0x100) {
				format = "%04x:%08x\n";
				break;
			}
		}
		
		for (i = 0; i < num; i++) {
			n = sprintf(buf, format,
					pword[i]->off, pword[i]->val);
			if(write(h_config, buf, n) < 0)
				return errno;
		}
		close(h_config);
	}

	return 0;

}


static int parse_config_file(const char *p_configname, char *config_data, int config_len,
			struct config_word *word[])
{
	int i = 0, newline = 1;
	char *tmp = config_data;

	while (tmp < (config_data + config_len)) {
		if (isspace(*tmp) && !isblank(*tmp)) {
			*tmp++ = '\0';
			newline = 1;
			continue;
		}

		if (newline) {
			word[i++] = (struct config_word *)tmp;
			newline = 0;
		}
		tmp++;

		/* if overflow, just ignore left data */
		if (i >= BOOT_MAX_CONFIG_WORDS) {
			printf("WARNING! Too many configuration words!\n");
			break;
		}
	}

	config_len = i;
	for (i = 0; i < config_len; i++) {
		config_data = (char *)word[i];
		tmp = config_data;
		word[i] = malloc(sizeof(struct config_word));
		if(!word[i]) {
			printf(MSG_MEMALLOC_FAIL, p_configname);
			break;
		}

		while (*tmp++ != DELIMITER) ;
			*(tmp - 1) = '\0';

		word[i]->off = strtoul(config_data, (char **)NULL, 16);
		word[i]->val = strtoul(tmp, (char **)NULL, 16);
	}

	return config_len;
}

/*
 * NOTE: partition can't cross cylinder
 * sectors per cylinder = heads * (sectors per track)
 * though heads can be 256, but because of limits of address registers in ASM,
 * heads often is 255.
 * e.g. if heads = 255, sectors per track = 63
 * sectors per cylinder = 16065(255 * 63)
 *
 * to 1st partition, which sectors amount equals total sectors + hidden sectors
 *
 * if left space in the 1st partition is enough to save user code, that's
 * perfect, or related items in MBR/DBR tables must be fixed to exclude
 * the space that is used by user code
 * return: all ones -- error
 */
static uint adjust_partition_table(const struct hd_geometry *geometry, const uint sd_sectors,
				   struct mbr_disk_part_tbl *mbr, struct mbr_disk_part_tbl *mbr_last,
				   struct boot_sector *pdbr,
				   const uint n, const int sectors)
{
	uint cyl_secs, total_sec;
	int off, shrink_secs;

	// If this gets called without MBR and without partition info,
	// we just pretend to be unpartitioned
	if(!mbr && !pdbr)
		return n;
	
	// If our user code fits into the space prior to the first partition,
	// we are ok. Things can remain "as is" in this case
	if(mbr && (n + sectors <= mbr->rel_sectors))
		return n;

	// FAT supports other sizes than 512, but the code below does not!
	if(pdbr) {
		debug("Checking FAT boot record\n");
		if(pdbr->bpb.sector_size != 512)
			goto bad;

		// DOS 3 extension. We are not falling back to the HW reported geometry,
		// but try to stick with what the partitioning tool told us!
		if((pdbr->bpb.sector_p_track != geometry->sectors) ||
		(pdbr->bpb.heads != geometry->heads)) {
			printf("WARNING! CHS geometry in FAT boot block does not match reported disk geometry. Using disk geometry instead!\n");
		}

		total_sec = pdbr->bpb.small_sectors;
		if (total_sec == 0) /* partitions > 32M */
			total_sec = pdbr->bpb.large_sectors;

		if(!total_sec && mbr)
			total_sec = mbr->total_sectors;
	} else {
		debug("Reading partition size from MBR\n");
		total_sec = mbr->total_sectors;
	}

	debug("Calculating cylinder geometry for CHS rounding\n");
	cyl_secs = geometry->sectors * geometry->heads;
	if(!cyl_secs)
		goto bad;

	/* check whether 1st partition is enough big, maybe not needed */
	debug("SD sectors %u, 1st partition total_sec %u, needed sectors %u\n", sd_sectors, total_sec, sectors);

	// Let's see if we can put the code beyond the last partition
	// We check beyond the last sector of the last partition and round it up
	// to the next "cylinder" boundary just to keep partition tools happy
	// that might interfere with the interim space if partitions are modified
	// after placing the user code
	if(mbr) {
		off = mbr_last->rel_sectors + mbr_last->total_sectors;
	} else {
		off = total_sec;
	}
	if(sd_sectors < off)
		goto bad;
	off = ((off + cyl_secs - 1) / cyl_secs) * cyl_secs;
	if(sd_sectors <= off)
		goto bad;

	debug("Checking if we need to shrink the partition at all\n");
	if(sd_sectors - off >= sectors)
		goto end;

	debug("Shrinking seems necessary\n");
	// We are only shrinking the partition if it is the only
	// one, i.e., last == first. Reason is purely that this code
	// carries so much history that keeping the context to correctly
	// modify the last partition would be a hideous rewrite.
	if(mbr && (mbr != mbr_last))
		goto bad;

	// We again round up to the number of cylinders to make sure that
	// partitioning tools don't complain too much.
	shrink_secs = sectors - (sd_sectors - off);
	shrink_secs = ((shrink_secs + cyl_secs - 1) / cyl_secs) * cyl_secs;


	total_sec -= shrink_secs;
	off -= shrink_secs;

	if (pdbr) {
		if (pdbr->bpb.small_sectors == 0)
			pdbr->bpb.large_sectors = total_sec;
		else
			pdbr->bpb.small_sectors = total_sec;
	}

	if (mbr)
		mbr->total_sectors = total_sec;

	debug("Shrinked FAT partition by %u sectors to put user code image beyond the end\n", shrink_secs);
	printf("WARNING! First FAT partition has been adjusted in size! You MUST reformat that partition!\n");
end:
	return off;
bad:
	return (uint)-1;
}

static uint writebuffer(int h_dev, uchar *ptr, uint len)
{
	int n;

	while (len > 0) {
		n = (len >= 1<<30) ? 1<<30 : len;
		n = write(h_dev, ptr, n);
		if (n < 0) {
			break;
		}
		ptr += n;
		len -= n;
	}

	/* If this is not zero, we had an error! */
	return(len);
}

static bool read_sector(const int fd, const uint sector, uchar *buffer)
{
	bool success = false;

	if(lseek64(fd, SEC_TO_BYTE_OFFSET(sector), SEEK_SET) >= 0) {
		if (read(fd, buffer, SEC_TO_BYTE(1)) == SEC_TO_BYTE(1)) {
			success = true;
		}
	}

	return(success);
}

static bool write_sector(const int fd, const uint sector, uchar *buffer)
{
	bool success = false;

	if(lseek64(fd, SEC_TO_BYTE_OFFSET(sector), SEEK_SET) >= 0) {
		if (writebuffer(fd, buffer, SEC_TO_BYTE(1)) == 0) {
			success = true;
		}
	}

	return(success);
}

static int read_config_file(char *p_configname, struct config_word **pconfig_word, uint *p_config_num)
{
	char *cfg_data_buf;
	int exitcode = 0;
	int h_config;
	uint len;

	h_config = open(p_configname, O_RDONLY);
	if (h_config >= 0) {
		len = lseek(h_config, 0, SEEK_END);
		lseek(h_config, 0, SEEK_SET);
		cfg_data_buf = malloc(len + 1);
		if (cfg_data_buf) {
			cfg_data_buf[len] = 0; /* Ensure termination when parsing last line */
			if (read(h_config, cfg_data_buf, len) == len) {
				/* Parse config file */
				*p_config_num = parse_config_file(p_configname, cfg_data_buf, len, pconfig_word);
			} else {
				exitcode = errno;
				printf(MSG_READ_FILE_FAIL, p_configname);
			}

			free(cfg_data_buf);
		} else {
			exitcode = errno;
			printf(MSG_MEMALLOC_FAIL, p_configname);
		}
		
		close(h_config);
	} else {
		exitcode = errno;
		printf(MSG_OPEN_FILE_FAIL, p_configname);
	}

	return exitcode;
}

static int read_usercode_file(char *p_usercodename, char **p_usercode_buf, uint *p_len)
{
	int exitcode = 0;
	int h_usercode;
	int n;
	uint len;
	char *usercode_buf;
	
	h_usercode = open(p_usercodename, O_RDONLY);
	if (h_usercode >= 0) {
		n = lseek(h_usercode, 0, SEEK_END);
		lseek(h_usercode, 0, SEEK_SET);
		len = BYTE_ROUNDUP_TO_SEC(n);
		usercode_buf = malloc(len);
		if (usercode_buf) {
			memset(usercode_buf + n, 0, len - n);
			if (read(h_usercode, usercode_buf, n) == n) {
				*p_usercode_buf = usercode_buf;
				*p_len = len;
			} else {
				exitcode = errno;
				printf(MSG_READ_FILE_FAIL, p_usercodename);
				free(usercode_buf);
			}
		} else {
			exitcode = errno;
			printf(MSG_MEMALLOC_FAIL, p_usercodename);
		}
			
		close(h_usercode);
	} else {
		exitcode = errno;
		printf(MSG_OPEN_FILE_FAIL, p_usercodename);
	}

	return exitcode;
}

int main(int argc, char *argv[])
{
	int h_dev, h_spi_dev = 0, h_sd_dev = 0;
	char *usercode_buf = NULL;
	uchar *mbr_buf = NULL, *boot_sect_buf = NULL;
	char p_configname[256], p_usercodename[256], p_devname[256], p_outconfigname[256];
	struct mbr_disk_part_tbl *mbr_dpt_base, *mbr_dpt = NULL, *mbr_dpt_last = NULL;
	struct boot_sector *boot_sector = NULL;
	struct config_word *pconfig_word[BOOT_MAX_CONFIG_WORDS];
	int exitcode = 0, code_addr = -1, work_mode = 0;
	uint i, n, config_num, len, sec_user, rev_space = BOOT_REV_SPACE;
	uchar *ptr = NULL;
	struct stat sb;
	uint endian_mode;
	uint rel_sectors = 0;
	uint offmin = 0xffffffff, offmax = 0;
	off64_t sd_sectors = 0;
	bool pblboot;
	struct hd_geometry geometry;
	struct option longopts[] = {
		{"sd", required_argument, NULL, 'd'},
		{"spi", required_argument, NULL, 'p'},
		{"o", required_argument, NULL, 'o'},
		{"r", required_argument, NULL, 'r'},
		{0, 0, 0, 0},
	};

	memset(pconfig_word, 0, sizeof(struct config_word *) *
	       BOOT_MAX_CONFIG_WORDS);
	if (argc < 5) {
		printf("Usage: %s <config_file> <image> -sd <dev> [-o <out_config>]"
			" | -spi <spiimage>", argv[0]);
#if CONFIG_REV_SPACE
		printf(" [-r <size>]");
#endif
		printf("\n");
		printf("\n\tconfig_file : includes boot signature and config words");
		printf("\n\timage       : the U-Boot image for booting from eSDHC/eSPI");
		printf("\n\tdev         : SDCard's device node(e.g. /dev/sdb, /dev/mmcblk0)");
		printf("\n\tspiimage    : boot image for SPI mode");
		printf("\n\tout_config  : modified config file for SD mode");
#if CONFIG_REV_SPACE
		printf("\n\tsize        : reserved space in KiB (default 0KiB).");
#endif
		printf("\n");
		exitcode = EINVAL;
		goto end;
	}

	/* Endian mode */
	endian_mode = get_endian_mode();
	debug("This host is a %s endian machine.\n",
			endian_mode == BIG_ENDIAN_MODE ? "big" : "little");

	memset(p_devname, 0, sizeof(p_devname));
	strncpy(p_configname, argv[1], 255);
	strncpy(p_usercodename, argv[2], 255);
	p_outconfigname[0] = 0;
	while((n = getopt_long_only(argc, argv, "", longopts, NULL)) != -1) {
		int new_work_mode = 0;

		switch (n) {
			case 'd':
				new_work_mode = BOOT_WORK_MODE_SD;
				strncpy(p_devname, optarg, 255);
				break;
			case 'p':
				new_work_mode = BOOT_WORK_MODE_SPI;
				strncpy(p_devname, optarg, 255);
				break;
			case 'o':
				strncpy(p_outconfigname, optarg, 255);
				break;
#if CONFIG_REV_SPACE
			case 'r':
				rev_space = strtoul(optarg, (char**)NULL, 10) * 1024;
				break;
#endif
		}

		/*
		 * if "sd" and "spi" flags are used together,
		 * work_mode must be wrong !
	         * JZ: added print information for more than 1 option
		 */
	        if (new_work_mode && work_mode) {
        	    printf("\n Only one option is allowed. work_mode: %d \n", work_mode);
		    exitcode = EINVAL;
	            goto end;
        	}
		work_mode |= new_work_mode;

	}

	memset(&sb, 0, sizeof(struct stat));
	stat(p_devname, &sb);
	if ((work_mode == BOOT_WORK_MODE_SD) && !S_ISBLK(sb.st_mode)) {
		printf("Specified SDCard device node is not a block device!\n");
		exitcode = EINVAL;
		goto end;
	}

	if ((work_mode == BOOT_WORK_MODE_SPI) &&
			((errno != ENOENT) && !S_ISREG(sb.st_mode))) {
		printf("Specified SPI image name is not a regular file name!\n");
		exitcode = EINVAL;
		goto end;
	}

	if (work_mode == BOOT_WORK_MODE_SD) {
		/* Open SD device */
		h_sd_dev = open(p_devname, O_RDWR, 0666);
		if (h_sd_dev < 0) {
			exitcode = errno;
			printf(MSG_OPEN_FILE_FAIL, p_devname);
			goto end;
		}

		// Obtain disk geometry for CHS conversions
		if(!get_disk_geometry(h_sd_dev, &geometry)) {
			printf(MSG_GEOMETRY_FAIL, p_devname);
			exitcode = -EIO;
			goto end;
		}

		/* Read first sector from sd */
		mbr_buf = (uchar*)malloc(SEC_TO_BYTE(1));
		if (mbr_buf == NULL) {
			exitcode = errno;
			goto end;
		}

		/* Determine size of disk to understand SD vs. SDHC */
		sd_sectors = lseek64(h_sd_dev, 0, SEEK_END);
		if(sd_sectors < 0) {
			exitcode = errno;
			printf(MSG_READ_FILE_FAIL, p_devname);
			goto end;
		}
		sd_sectors = BYTE_TO_SEC(BYTE_ROUNDUP_TO_SEC(sd_sectors));
		debug("SDCard has %u sectors\n", (uint)sd_sectors);

		if(!read_sector(h_sd_dev, 0, mbr_buf)) {
			exitcode = errno;
			printf(MSG_READ_FILE_FAIL, p_devname);
			goto end;
		}
		debug("Read MBR from SDCard:\n");
		print_buf(mbr_buf, SEC_TO_BYTE(1), 1);

		if (get_endian_mode() == BIG_ENDIAN_MODE)
			swap_mbr((struct mbr_disk_part_tbl *)mbr_buf);

		mbr_dpt_base = parse_mbr(mbr_buf);
		mbr_dpt = NULL;
		mbr_dpt_last = NULL;
		if (mbr_dpt_base) {
			first_last_partition_on_disk(h_sd_dev, &geometry, mbr_dpt_base, &mbr_dpt, &mbr_dpt_last);
			debug((mbr_dpt) ? "Found first partition in MBR\n" : "No valid first partition set up in MBR\n");
			debug((mbr_dpt_last && mbr_dpt != mbr_dpt_last) ? "Found last partition in MBR\n" : "First partition seems to be also the last partition\n");
			if(mbr_dpt)
				print_mbr(mbr_dpt);
		}

		if (!mbr_dpt) {
			if (get_endian_mode() == BIG_ENDIAN_MODE)
				swap_mbr((struct mbr_disk_part_tbl *)mbr_buf);
		}

		/* Some formatted card doesn't have an MBR, only DBR */
		if (!mbr_dpt_base) {
			boot_sect_buf = mbr_buf;
			mbr_buf = NULL;
		} else {
			if(mbr_dpt) {
				boot_sect_buf = (uchar*)malloc(SEC_TO_BYTE(1));
				if (boot_sect_buf == NULL) {
					exitcode = errno;
					goto end;
				}

				rel_sectors = mbr_dpt->rel_sectors;
				if(!read_sector(h_sd_dev, rel_sectors, boot_sect_buf)) {
					exitcode = errno;
					printf(MSG_READ_FILE_FAIL, p_devname);
					goto end;
				}
				debug("Read DBR from SDCard at sector %u for partition size of %u sectors:\n", mbr_dpt->rel_sectors, mbr_dpt->total_sectors);
				print_buf(boot_sect_buf, SEC_TO_BYTE(1), 1);
			} else {
				debug("No partition or disk boot record found, treating disk as unformatted/raw\n");
			}
		}

		/* Parse first partition. NOTE: mbr_dpt maybe always be NULL */
		if(boot_sect_buf) {
			if (endian_mode == BIG_ENDIAN_MODE)
				swap_dbr((struct boot_sector *)boot_sect_buf);
			boot_sector = parse_dbr(mbr_dpt, boot_sect_buf);
			if(!boot_sector)
				if (endian_mode == BIG_ENDIAN_MODE)
					swap_dbr((struct boot_sector *)boot_sect_buf);
		}
	} else {
		h_spi_dev = open(p_devname, O_WRONLY|O_CREAT|O_TRUNC, 0666);
		if (h_spi_dev < 0) {
			exitcode = errno;
			printf(MSG_OPEN_FILE_FAIL, p_devname);
			goto end;
		}
	}

	/* Open config file */
	exitcode = read_config_file(p_configname, pconfig_word, &config_num);
	if (exitcode)
		goto end;

	/* Sanity check for PBL vs. non-PBL boot */
	pblboot = true;
	for (i = 0; i < config_num; i++) {
		offmin = min(pconfig_word[i]->off, offmin);
		offmax = max(pconfig_word[i]->off, offmax);
		if ((pconfig_word[i]->off == BOOT_SIGNATURE_OFF) &&
		    (pconfig_word[i]->val == BOOT_SIGNATURE)) {
			pblboot = false;
		}
	}

	debug((pblboot) ? "Configuration file is for a PBL boot\n" : "Configuration file is for a non-PBL boot\n");

	if (!pblboot) {
		if (offmin < BOOT_SIGNATURE_OFF) {
			printf("WARNING! First config word prior to boot signature!\n");
		}
		if (offmax > BOOT_SIGNATURE_OFF + 4*BOOT_MAX_CONFIG_WORDS) {
			printf("WARNING! Last config word past maximum permitted range!\n");
		}
		if (offmax >= 0x80 + 40 * 8) {
			if (mbr_dpt) 
				printf("WARNING! Config words conflict with FAT MBR usage!\n");
			if (!mbr_dpt && boot_sector) 
				printf("WARNING! We are overwriting a FAT style volume boot record!\n");
		}
	}


	/* Open user code file */
	exitcode = read_usercode_file(p_usercodename, &usercode_buf, &len);
	if (exitcode)
		goto end;

	
	/* User code + Reserved space */
	len = BYTE_ROUNDUP_TO_SEC(len);
	len += BYTE_ROUNDUP_TO_SEC(rev_space);

	debug("Updating boot image length to %u in configuration data\n", len);
	sec_user = BYTE_TO_SEC(len);
	for (i = 0; i < config_num; i++) {
		if (pconfig_word[i]->off == BOOT_IMAGE_LEN_OFF) {
			/* The length must be aligned to the sector size */
			pconfig_word[i]->val = len;
			//break; Changed to deal with complex copy/paste configs
		}
	}

	// User code has to start beyond the config words, and we always start at least
	// at block two to be backwards compatible
	n = BYTE_TO_SEC(offmax + 3) + 1;
	if(n < 2)
		n = 2;

	if (work_mode == BOOT_WORK_MODE_SD) {
		// We mess with partitions only if our code does not fit into the available space
		// prior to the first partition

		/* FIXME: don't support disk clean up */
		if(mbr_dpt || boot_sector) {
			debug("Checking if FAT partition needs to be shrinked\n");
			n = adjust_partition_table(&geometry, sd_sectors, mbr_dpt, mbr_dpt_last, boot_sector,
						   n, sec_user);
			rel_sectors = mbr_dpt->rel_sectors;
		}
	}

	if (n == (uint)-1) {
		printf(MSG_PARTLENGTH_FAIL);
		exitcode = EINVAL;
		goto end;
	}

	/* Change config word[0x50] to final user code position
	 * JZ: Source address should be always byte mode*/
	for (i = 0; i < config_num; i++) {
		if (pconfig_word[i]->off == BOOT_IMAGE_ADDR_OFF) {
			code_addr = i;
			pconfig_word[i]->val = SEC_TO_BYTE_OFFSET(n);
			break;
		}
	}

	if (code_addr < 0) {
		printf(MSG_PARTLENGTH_FAIL);
		exitcode = EINVAL;
		goto end;
	}

	/* Only in SD mode config file is supported to output */
	if (work_mode == BOOT_WORK_MODE_SD) {
		if (p_outconfigname[0]) {
			debug("Writing configuration file ...\n");
			if(write_config_file(p_outconfigname, config_num,
			                     pconfig_word)) {
				exitcode = errno;
				goto end;
			}
		}
	}

	/*
	 * Write back all data to sd card/spi image.
	 * NOTE: sequence is important, user code must be first because of
         * config word byte swap.
         */
	if (work_mode == BOOT_WORK_MODE_SD)
		h_dev = h_sd_dev;
	else
		h_dev = h_spi_dev;

	if (h_dev) {
		/* Write user code to sd card */
		debug("\nWriting image to %s...",
			(work_mode == BOOT_WORK_MODE_SD) ? "SDCard" : "SPI image");
		if(lseek(h_dev, pconfig_word[code_addr]->val, SEEK_SET) < 0) {
			goto writefail;
		}

		len = SEC_TO_BYTE(sec_user) - BYTE_ROUNDUP_TO_SEC(rev_space);
		ptr = (uchar *)usercode_buf;
		if (writebuffer(h_dev, ptr, len)) {
			goto writefail;
		}
			
		if ((work_mode == BOOT_WORK_MODE_SPI) && (rev_space != 0)) {
			/* Extend file size to accomodate reserved space */
			if(lseek(h_dev, rev_space - 1, SEEK_CUR) < 0) {
				goto writefail;
			}
			n = 0;
			if(write(h_dev, &n, 1) != 1) {
				goto writefail;
			}
		}

		debug("OK.\n");
	}

	if (work_mode == BOOT_WORK_MODE_SD) {
		/* Write MBR */
		if (mbr_buf) {
			debug("\nWriting MBR to SDCard...");
			if (endian_mode == BIG_ENDIAN_MODE)
				swap_mbr((struct mbr_disk_part_tbl *)ptr);

			ptr = (uchar *)mbr_buf;
			print_buf(ptr, SEC_TO_BYTE(1), 1);
			if (!write_sector(h_dev, 0, ptr)) {
				goto writefail;
			}
		}
		debug("OK.\n");

		/* Write DBR */
		if (boot_sect_buf) {
			debug("\nWriting DBR to SDCard...");
			if (endian_mode == BIG_ENDIAN_MODE)
				swap_dbr((struct boot_sector *)boot_sect_buf);
			print_dbr((struct boot_sector *)boot_sect_buf);

			ptr = (uchar *)boot_sect_buf;
			print_buf(ptr, SEC_TO_BYTE(1), 1);
			if (!write_sector(h_dev, rel_sectors, ptr)) {
				goto writefail;
			}
		}
		debug("OK.\n");
	}

	// Now it's time to finally update the image with our
	// config words. We read/update/write the sectors as needed
	{
		uchar sector[SECTOR_SIZE];
		uint secnum = 0xffffffff, newsecnum;

		debug("Updating configuration words in target image ...\n");
		for (i = 0; i < config_num; i++) {
			n = pconfig_word[i]->val;
			/* Config words are provided with big endian mode */
			if (endian_mode == LITTLE_ENDIAN_MODE)
				n = SWAP32(n);
			newsecnum = BYTE_TO_SEC(pconfig_word[i]->off);
			if(secnum != newsecnum) {
				if(secnum != 0xffffffff) {
					debug("Writing sector %u\n", secnum);
					if (!write_sector(h_dev, secnum, sector)) {
writefail:
						exitcode = errno;
						printf(MSG_WRITE_FILE_FAIL, p_devname);
						goto end;
					}
				}

				if (work_mode == BOOT_WORK_MODE_SD) {
					debug("Reading sector %u\n", newsecnum);
					if(!read_sector(h_dev, newsecnum, sector)) {
						exitcode = errno;
						printf(MSG_READ_FILE_FAIL, p_devname);
						goto end;
					}
				} else {
					memset(sector, 0, SECTOR_SIZE);
				}

				secnum = newsecnum;
			}
			*(uint *)(sector + pconfig_word[i]->off) = n;
		}
		if(secnum != 0xffffffff) {
			debug("Writing sector %u\n", secnum);
			if (!write_sector(h_dev, secnum, sector)) {
				goto writefail;
			}
		}
		debug("OK.\n");
	}


	sync();

end:
	for (i = 0; i < config_num; i++)
		free(pconfig_word[i]);
	free(boot_sect_buf);
	free(mbr_buf);
	free(usercode_buf);
	if (h_sd_dev != 0)
		close(h_sd_dev);
	if (h_spi_dev != 0)
		close(h_spi_dev);

	if (exitcode == 0) {
		printf("Congratulations! Completed successfully.\n");
		exitcode = EXIT_SUCCESS;
	} else {
		printf("Error happened while execution: %s\n", strerror(exitcode));
		exitcode = EXIT_FAILURE;
	}

	return exitcode;
}

