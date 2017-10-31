/*
 * This file is part of the flashrom project.
 *
 * Copyright (C) 2009 Sean Nelson <audiohacked@gmail.com>
 * Copyright (C) 2011 Carl-Daniel Hailfinger
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include "flash.h"

int verbose_screen = MSG_INFO;
int verbose_logfile = MSG_DEBUG2;

#ifndef STANDALONE
static FILE *logfile = NULL;
static const char *programmer_param = NULL;


int programmer_init(const char *param)
{
	programmer_param = param;
	return 0;
}

/* This is a somewhat hacked function similar in some ways to strtok().
 * It will look for needle with a subsequent '=' in haystack, return a copy of
 * needle and remove everything from the first occurrence of needle to the next
 * delimiter from haystack.
 */
char *extract_param(const char *const *haystack, const char *needle, const char *delim)
{
	char *param_pos, *opt_pos, *rest;
	char *opt = NULL;
	int optlen;
	int needlelen;

	needlelen = strlen(needle);
	if (!needlelen) {
		msg_gerr("%s: empty needle! Please report a bug at "
			 "flashrom@flashrom.org\n", __func__);
		return NULL;
	}
	/* No programmer parameters given. */
	if (*haystack == NULL)
		return NULL;
	param_pos = strstr(*haystack, needle);
	do {
		if (!param_pos)
			return NULL;
		/* Needle followed by '='? */
		if (param_pos[needlelen] == '=') {
			
			/* Beginning of the string? */
			if (param_pos == *haystack)
				break;
			/* After a delimiter? */
			if (strchr(delim, *(param_pos - 1)))
				break;
		}
		/* Continue searching. */
		param_pos++;
		param_pos = strstr(param_pos, needle);
	} while (1);

	if (param_pos) {
		/* Get the string after needle and '='. */
		opt_pos = param_pos + needlelen + 1;
		optlen = strcspn(opt_pos, delim);
		/* Return an empty string if the parameter was empty. */
		opt = malloc(optlen + 1);
		if (!opt) {
			msg_gerr("Out of memory!\n");
			exit(1);
		}
		strncpy(opt, opt_pos, optlen);
		opt[optlen] = '\0';
		rest = opt_pos + optlen;
		/* Skip all delimiters after the current parameter. */
		rest += strspn(rest, delim);
		memmove(param_pos, rest, strlen(rest) + 1);
		/* We could shrink haystack, but the effort is not worth it. */
	}

	return opt;
}

char *extract_programmer_param(const char *param_name)
{
	return extract_param(&programmer_param, param_name, ",");
}

int close_logfile(void)
{
	if (!logfile)
		return 0;
	/* No need to call fflush() explicitly, fclose() already does that. */
	if (fclose(logfile)) {
		/* fclose returned an error. Stop writing to be safe. */
		logfile = NULL;
		msg_gerr("Closing the log file returned error %s\n", strerror(errno));
		return 1;
	}
	logfile = NULL;
	return 0;
}

int open_logfile(const char * const filename)
{
	if (!filename) {
		msg_gerr("No logfile name specified.\n");
		return 1;
	}
	if ((logfile = fopen(filename, "w")) == NULL) {
		msg_gerr("Error: opening log file \"%s\" failed: %s\n", filename, strerror(errno));
		return 1;
	}
	return 0;
}

void start_logging(void)
{
	enum msglevel oldverbose_screen = verbose_screen;

	/* Shut up the console. */
	verbose_screen = MSG_ERROR;
//	print_version();
	verbose_screen = oldverbose_screen;
}
#endif /* !STANDALONE */

/* Please note that level is the verbosity, not the importance of the message. */
int print(enum msglevel level, const char *fmt, ...)
{
	va_list ap;
	int ret = 0;
	FILE *output_type = stdout;

	if (level < MSG_INFO)
		output_type = stderr;

	if (level <= verbose_screen) {
		va_start(ap, fmt);
		ret = vfprintf(output_type, fmt, ap);
		va_end(ap);
		/* msg_*spew often happens inside chip accessors in possibly
		 * time-critical operations. Don't slow them down by flushing. */
		if (level != MSG_SPEW)
			fflush(output_type);
	}
#ifndef STANDALONE
	if ((level <= verbose_logfile) && logfile) {
		va_start(ap, fmt);
		ret = vfprintf(logfile, fmt, ap);
		va_end(ap);
		if (level != MSG_SPEW)
			fflush(logfile);
	}
#endif /* !STANDALONE */
	return ret;
}
