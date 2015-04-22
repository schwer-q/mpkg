/*
 * Copyright (c) 2015, Quentin Schwerkolt
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice, this
 *   list of conditions and the following disclaimer.
 *
 * * Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#if defined(HAVE_CONFIG_H)
#include "config.h"
#endif	/* HAVE_CONFIG_H */

#include <err.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>

#include "mpkg.h"

static struct {
	const char *name;
	void (*callback)(config_t *, int, char **);
        const char *help;
} commands[] = {
	{ "info",	info_func, "get information about installed packages" },
	{ "install",	install_func, "install package" },
	{ "list",	list_func, "list installed package" },
	{ "remove",	NULL, "remove installed package" },
	{ "update",	NULL, "update installed package" },
	{ NULL,		NULL, NULL }
};

int
main(int argc, char **argv)
{
	int ch, idx;
        config_t _config, *config;

	config = &_config;
	bzero(config, sizeof(config_t));
	config->rootdir = "/";

	while ((ch = getopt(argc, argv, "R:nvy")) != -1) {
		switch (ch) {
		case 'R':
			config->rootdir = optarg;
			break;

		case 'n':
			config->dryrun = 1;
			break;

		case 'v':
			config->verbose = 1;
			break;

		case 'y':
			config->ansyes = 1;
			break;

		default:
			usage("%c -- unknown global option", ch);
			break;
		}
	}
	if ((argc - optind) < 1)
		usage(NULL);

	for (idx = 0; commands[idx].name; ++idx) {
		if (!strcmp(argv[optind], commands[idx].name))
			break;
	}
	if (!commands[idx].name)
		usage("%s -- unknown command", argv[optind]);

	if (!commands[idx].callback)
		errx(1, "%s -- not yet implemented", commands[idx].name);
	commands[idx].callback(config, argc - optind, argv + optind);

	return (0);
}

static void
usage(const char *fmt, ...)
{
	int idx;
	va_list ap;

	if (fmt) {
		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	fprintf(stdout,
		"usage:\n"
		"\t%s [-R root] [-nvy] command ...\n\n"
		"commands:\n",
		getprogname());

	for (idx = 0; commands[idx].name; ++idx) {
		fprintf(stdout,
			"\t%s\t-- %s\n",
			commands[idx].name, commands[idx].help);
	}

	exit(2);
}
