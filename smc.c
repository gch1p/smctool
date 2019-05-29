/*
 * This file is part of the coreboot project.
 *
 * Copyright (C) 2019 Evgeny Zinoviev <me@ch1p.io>
 *
 * SMC interacting code is based on applesmc linux driver by:
 * Copyright (C) 2007 Nicolas Boichat <nicolas@boichat.ch>
 * Copyright (C) 2010 Henrik Rydberg <rydberg@euromail.se>
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

#include <stdlib.h>
#include <sys/io.h>
#include <errno.h>
#include <stdio.h>
#include "smc.h"

/*
 * wait_read - Wait for a byte to appear on SMC port. Callers must
 * hold applesmc_lock.
 */
int wait_read(void)
{
	uint8_t status;
	int us;
	for (us = APPLESMC_MIN_WAIT; us < APPLESMC_MAX_WAIT; us <<= 1) {
		usleep(us);
		status = inb(APPLESMC_CMD_PORT);

		/* read: wait for smc to settle */
		if (status & 0x01)
			return 0;
	}

	fprintf(stderr, "%s() fail: 0x%02x\n", __func__, status);
	return -EIO;
}

/*
 * send_byte - Write to SMC port, retrying when necessary. Callers
 * must hold applesmc_lock.
 */
int send_byte(uint8_t cmd, uint16_t port)
{
	uint8_t status;
	int us;

	outb(cmd, port);
	for (us = APPLESMC_MIN_WAIT; us < APPLESMC_MAX_WAIT; us <<= 1) {
		usleep(us);
		status = inb(APPLESMC_CMD_PORT);
		/* write: wait for smc to settle */
		if (status & 0x02)
			continue;
		/* ready: cmd accepted, return */
		if (status & 0x04)
			return 0;
		/* timeout: give up */
		if (us << 1 == APPLESMC_MAX_WAIT)
			break;
		/* busy: long wait and resend */
		usleep(APPLESMC_RETRY_WAIT);
		outb(cmd, port);
	}

	fprintf(stderr, "%s(0x%02x, 0x%04x) fail: 0x%02x\n",
		__func__, cmd, port, status);
	return -EIO;
}

int send_command(uint8_t cmd)
{
	return send_byte(cmd, APPLESMC_CMD_PORT);
}

int send_argument(const char *key)
{
	int i;

	for (i = 0; i < 4; i++)
		if (send_byte(key[i], APPLESMC_DATA_PORT))
			return -EIO;
	return 0;
}

int read_smc(uint8_t cmd, const char *key, uint8_t *buffer, uint8_t len)
{
	uint8_t status, data = 0;
	int i;

	if (send_command(cmd) || send_argument(key)) {
		fprintf(stderr, "%.4s: read arg fail\n", key);
		return -EIO;
	}

	/* This has no effect on newer (2012) SMCs */
	if (send_byte(len, APPLESMC_DATA_PORT)) {
		fprintf(stderr, "%.4s: read len fail\n", key);
		return -EIO;
	}

	for (i = 0; i < len; i++) {
		if (wait_read()) {
			fprintf(stderr, "%.4s: read data[%d] fail\n", key, i);
			return -EIO;
		}
		buffer[i] = inb(APPLESMC_DATA_PORT);
	}

	/* Read the data port until bit0 is cleared */
	for (i = 0; i < 16; i++) {
		usleep(APPLESMC_MIN_WAIT);
		status = inb(APPLESMC_CMD_PORT);
		if (!(status & 0x01))
			break;
		data = inb(APPLESMC_DATA_PORT);
	}

	if (i)
		fprintf(stderr, "flushed %d bytes, last value is: %d\n",
			i, data);

	return 0;
}

int write_smc(uint8_t cmd, const char *key, const uint8_t *buffer, uint8_t len)
{
	int i;

	if (send_command(cmd) || send_argument(key)) {
		fprintf(stderr, "%s: write arg fail\n", key);
		return -EIO;
	}

	if (send_byte(len, APPLESMC_DATA_PORT)) {
		fprintf(stderr, "%.4s: write len fail\n", key);
		return -EIO;
	}

	for (i = 0; i < len; i++) {
		if (send_byte(buffer[i], APPLESMC_DATA_PORT)) {
			fprintf(stderr, "%s: write data fail\n", key);
			return -EIO;
		}
	}

	return 0;
}
