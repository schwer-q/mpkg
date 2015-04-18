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
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "db.h"
#include "mpkg.h"

static void	info_show(db_t *db, char **list, int show_deps, int show_files);
static void	usage(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

void
info_func(config_t *config, int argc, char **argv)
{
	char dbpath[PATH_MAX];
	db_t *db;
	int all_pkgs, show_deps, show_files;
	int ch;

	optreset = 1; optind = 1; opterr = 0;
	all_pkgs = 0; show_deps = 0; show_files = 0;
	while ((ch = getopt(argc, argv, "adl")) != -1) {
		switch (ch) {
		case 'a':
			all_pkgs = 1;
			break;

		case 'd':
			show_deps = 1;
			break;

		case 'l':
			show_files = 1;
			break;

		default:
			usage("%c -- unknown option", ch);
			break;
		}
	}
	if (all_pkgs && (argc - optind) > 0)
		usage("-a and a package has been specifed");
	if (!all_pkgs && (argc - optind) < 1)
		usage("no package specified");

	bzero(dbpath, sizeof(char) * PATH_MAX);
	snprintf(dbpath, PATH_MAX, "%s/var/db/mpkg", config->rootdir);
	db = db_init(dbpath);
	db_load(db);

	if (all_pkgs)
		info_show(db, NULL, show_deps, show_files);
	else
		info_show(db, argv + optind, show_deps, show_files);

	db_free(db);
}

static void
info_show(db_t *db, char **list, int show_deps, int show_files)
{
	dblist_t *dbnode;
	int idx;
	manifest_depend_t *depend;
	manifest_node_t *node;

	for (dbnode = db->nodes; dbnode; /* void */) {
		if (list) {
			for (idx = 0; list[idx]; ++idx)
				if (!strcmp(dbnode->pkg->name, list[idx]))
					break;
			if (!list[idx])
				continue;
		}
		printf("%s-%d\n", dbnode->pkg->name, dbnode->pkg->release);

		if (show_deps) {
			printf("depends:\n");
			for (depend =
				     dbnode->pkg->depends; depend; /* void */) {
				printf("\t%s\n", depend->name);
				depend = depend->next;
			}
		}

		if (show_files) {
			printf("content:\n");
			for (node = dbnode->pkg->nodes; node; /* void */) {
				printf("\t%s\n", node->path);
				node = node->next;
			}
		}

		dbnode = dbnode->next;
	}
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
		"\t%s info [-dl] package [...]\n"
		"\t%s info -a [-dl]\n",
		progname, progname);

	exit(2);
}
