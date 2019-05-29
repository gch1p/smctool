/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2019 Evgeny Zinoviev <me@ch1p.io>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <getopt.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <endian.h>
#include <sys/io.h>
#include <stdio.h>
#include <errno.h>
#include "smc.h"
#include "smctool.h"

static bool fp_bits(char c, uint8_t *dest)
{
	if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) {
		*dest = strtol(&c, NULL, 16);
		return true;
	}
	return false;
}

static void print_bits(uint8_t len, uint8_t *ptr)
{
	for (int i = len - 1; i >= 0; i--) {
		for (int j = 7; j >= 0; j--)
			printf("%u", (ptr[i] >> j) & 1);

		if (i > 0)
			printf(" ");
	}
}

static void print_usage(const char *name)
{
	printf("usage: %s <options>\n", name);
	printf("\n"
		"Options:\n"
		"    -h, --help:        print this help\n"
		"    -k, --key <name>:  key name\n"
		"    -t, --type <type>: data type, see below\n"
		"    --output-hex\n"
		"    --output-bin\n"
		"\n"
		"Supported data types:\n"
		"    ui8, ui16, ui32, si8, si16, flag, fpXY, spXY\n"
		"\n"
		"    fp and sp are unsigned and signed fixed point\n"
		"    data types respectively.\n"
		"\n"
		"    The X in fp and sp data types is integer bits count\n"
		"    and Y is fraction bits count.\n"
		"\n"
		"    For example,\n"
		"    fpe2 means 14 integer bits, 2 fraction bits,\n"
		"    sp78 means 7 integer bits, 8 fraction bits\n"
		"    (and one sign bit).\n"
		"\n");
}


enum output_format {dec, hex, bin};

enum smctype {none, ui8, ui16, ui32, si8, si16, flag, fp, sp};
struct {
	enum smctype type;
	uint8_t len;
	char *str;
	char *decfmt;
	char *hexfmt;
} types[] = {
	{ui8,  1, "ui8",  "%u", "0x%02x"},
	{ui16, 2, "ui16", "%u", "0x%04x"},
	{ui32, 4, "ui32", "%u", "0x%08x"},
	{si8,  1, "si8",  "%d", "0x%02x"},
	{si16, 2, "si16", "%d", "0x%04x"},
	{flag, 1, "flag", "%u", "0x%01x"},
};

int main(int argc, char *argv[])
{
	bool show_help = false;
	int opt, option_index = 0;
	char name[KEYBUFSIZE+1] = {0};
	char typebuf[KEYBUFSIZE+1] = {0};
	uint8_t fp_int_bits = 0, fp_fraction_bits = 0;

	enum output_format of = dec;
	char *ofmt = NULL;
	char fmtbuf[32];

	enum smctype type = none;
	uint8_t len;

	struct option long_options[] = {
		{"help",       0, 0, 'h'},
		{"key",	       1, 0, 'k'},
		{"type",       1, 0, 't'},
		{"output-hex", 0, 0, 1000},
		{"output-bin", 0, 0, 1001},
		{0, 0, 0, 0}
	};

	if (argv[1] == NULL) {
		print_usage(argv[0]);
		exit(0);
	}

	while ((opt = getopt_long(argc, argv, "hk:t:",
				  long_options, &option_index)) != EOF) {
		switch (opt) {
		case 'h':
			show_help = true;
			break;

		case 'k':
			snprintf(name, KEYBUFSIZE, "%s", optarg);
			break;

		case 't':
			snprintf(typebuf, KEYBUFSIZE, "%s", optarg);
			break;

		case 1000: // output-hex
			of = hex;
			break;

		case 1001: // output-bin
			of = bin;
			break;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "Error: Extra parameter found.\n");
		print_usage(argv[0]);
		exit(1);
	}

	if (show_help) {
		print_usage(argv[0]);
		exit(0);
	}

	/* Validate key name */
	if (strlen(name) != 4) {
		fprintf(stderr, "Key name must be 4 characters long.\n");
		exit(1);
	}

	/* Validate key type */
	if ((typebuf[0] == 'f' || typebuf[0] == 's') && typebuf[1] == 'p') {
		if (!fp_bits(typebuf[2], &fp_int_bits)
			|| !fp_bits(typebuf[3], &fp_fraction_bits)) {
			fprintf(stderr, "Invalid fixed point data type.\n");
			exit(1);
		}
		if (typebuf[0] == 'f' && fp_int_bits + fp_fraction_bits != 16) {
			fprintf(stderr,
				"Invalid unsigned fixed point data type.\n");
			exit(1);
		}
		if (typebuf[0] == 's' && fp_int_bits + fp_fraction_bits != 15) {
			fprintf(stderr,
				"Invalid signed fixed point data type.\n");
			exit(1);
		}
		type = typebuf[0] == 'f' ? fp : sp;
		len = 2;
		ofmt = "%d.%u";
	}

	if (type == none) {
		for (unsigned int i = 0; i < ARRAY_SIZE(types); i++) {
			if (strcmp(typebuf, types[i].str))
				continue;

			type = types[i].type;
			len = types[i].len;
			if (of == dec)
				ofmt = types[i].decfmt;
			else if (of == hex)
				ofmt = types[i].hexfmt;
			break;
		}
	}

	if (type == none) {
		fprintf(stderr, "Key type \"%s\" is not known.\n", typebuf);
		exit(1);
	}

	/* Check permissions */
	if (geteuid() != 0) {
		fprintf(stderr, "You must be root.\n");
		exit(1);
	}

	/* Open SMC port */
	if (ioperm(APPLESMC_DATA_PORT, 0x10, 1)) {
		fprintf(stderr, "ioperm: %s\n", strerror(errno));
		exit(1);
	}

	/* Read key from SMC */
	int retval;
	uint32_t buf = 0;

	retval = read_smc(APPLESMC_READ_CMD, name, (uint8_t *)&buf, len);
	assert(retval == 0);

	/* Handle returned value according to the requested type */
	buf = be32toh(buf);

	int32_t fp_int = 0;
	uint16_t fp_fraction = 0;
	bool fp_sign = false;

	switch (type) {
	case flag:
		buf = (buf >> 24) & 1;
		break;

	case ui8:
		buf = (buf >> 24) & 0xff;
		break;

	case si8:
		buf = (buf >> 24) & 0xff;
		if (of == dec)
			buf = (int8_t)buf;
		break;

	case ui16:
		buf = (buf >> 16) & 0xffff;
		break;

	case si16:
		buf = (buf >> 16) & 0xffff;
		if (of == dec)
			buf = (int16_t)buf;
		break;

	case fp:
		buf = (buf >> 16) & 0xffff;
		fp_int = buf >> fp_fraction_bits;
		fp_fraction = buf & (0xffff >> fp_int_bits);
		break;

	case sp:
		buf = (buf >> 16) & 0xffff;
		fp_sign = ((buf >> 15) & 1) == 1;

		buf &= ~(1 << 15);

		fp_int = buf >> fp_fraction_bits;
		fp_fraction = buf & (0xffff >> fp_int_bits);

		if (fp_sign)
			fp_int *= -1;
		break;

	case ui32:
	case none: // to avoid gcc warning
		break;
	}

	/* Output the result */
	if (of != bin) {
		strcpy(fmtbuf, "%s = ");
		strcat(fmtbuf, ofmt);
		strcat(fmtbuf, "\n");

		if (type == fp || type == sp)
			printf(fmtbuf, name, fp_int, fp_fraction);
		else
			printf(fmtbuf, name, buf);
	} else {
		printf("%s = ", name);
		print_bits(len, (uint8_t *)&buf);
		printf("\n");
	}

	return 0;
}
