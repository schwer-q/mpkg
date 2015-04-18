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
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include "db.h"
#include "manifest.h"
#include "mpkg.h"

void
list_func(config_t *config, int argc, char **argv)
{
	char dbpath[PATH_MAX];
	db_t *db;
	dblist_t *dbnode;
	int ch;
	int automatic, manual;

	automatic = 0; manual = 0;
	optreset = 1; optind = 1; opterr = 0;
	while ((ch = getopt(argc, argv, "am")) != -1) {
		switch (ch) {
		case 'a':
			automatic = 1;
			break;

		case 'm':
		        manual = 1;
			break;

		default:
			usage("%c -- unknown option", ch);
			break;
		}
	}
	if (automatic && manual)
		usage("-a and -m are mutually exclusive");

	if (!automatic && !manual) {
		automatic = 1;
		manual = 1;
	}


	bzero(dbpath, sizeof(char) * PATH_MAX);
	snprintf(dbpath, PATH_MAX, "%s/var/db/mpkg", config->rootdir);
	db = db_init(dbpath);
	db_load(db);

	for (dbnode = db->nodes; dbnode; /* void */) {
		if (automatic && dbnode->automatic)
			printf("%s-%d\n",
			       dbnode->pkg->name,
			       dbnode->pkg->release);

		if (manual && !dbnode->automatic)
			printf("%s-%d\n",
			       dbnode->pkg->name,
			       dbnode->pkg->release);

		dbnode = dbnode->next;
	}
	db_free(db);
}

static void
usage(const char *fmt, ...)
{
	const char *progname;
	int idx;
	va_list ap;

	if (fmt) {
		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	progname = getprogname();
	fprintf(stdout,
		"usage:\n"
		"\t%s list [-a|-m]\n",
		progname);

	exit(2);
}
