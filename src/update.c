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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "catalog.h"
#include "db.h"
#include "mpkg.h"
#include "worker.h"

static void usage(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void
update_func(config_t *config, int argc, char **argv)
{
	catalog_t *catalog;
	char pathname[PATH_MAX];
	db_t *db;
	dbnode_t *node;
	int ch;
	worker_t *worker;

	optreset = 1; optind = 1; opterr = 0;
	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {

		default:
			usage("%c -- unknown option", ch);
			break;
		}
	}


	catalog = catalog_new();
	catalog_parse(catalog, config->repodir);

	snprintf(pathname, PATH_MAX, "%s/var/db/mpkg", config->rootdir);
	db = db_init(pathname);
	db_load(db);

	for (node = db->nodes; node; /* void */) {
		worker = worker_new(config, node->pkg->name,
				    WORKER_ACTION_UPDATE, true);
		worker_set_catalog(worker, catalog);
		worker_set_db(worker, db);

		worker_exec(worker);

		worker_free(worker);

	}

	catalog_free(catalog);
	db_free(db);
}

static void
usage(const char *fmt, ...)
{
	const char *progname;
	va_list ap;

	if (fmt) {
		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	progname = getprogname();
	fprintf(stdout,
		"usage:\n"
		"\t%s update [package [...]]\n",
		progname);

	exit(2);
}
