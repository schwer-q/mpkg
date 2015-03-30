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

#include <sys/stat.h>
#include <sys/types.h>

#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "ar.h"
#include "manifest.h"
#include "utils.h"

static void	usage(char *fmt, ...);

int
main(int argc, char **argv)
{
	ar_t *ar;
	char *protodir, *repodir;
	char path[PATH_MAX];
	int ch, idx;
	manifest_node_t *node;
	manifest_t *pkg;

	protodir = repodir = NULL;
	while ((ch = getopt(argc, argv, "p:r:")) != -1) {
		switch (ch) {
		case 'p':
			protodir = optarg;
			break;

		case 'r':
			repodir = optarg;
			break;

		default:
			usage("%c -- unknown global option", (char)ch);
			break;
		}
	}

	if (!protodir)
		usage("-p is required");
	if (!repodir)
		usage("-r is required");

	for (idx = optind; idx < argc; ++idx) {
		pkg = manifest_parse(argv[idx]);

		snprintf(path, PATH_MAX, "%s/%s", repodir, pkg->name);
		if (access(path, X_OK) == -1)
			if (mkdir(path, 0755) == -1)
				err(1, "mkdir: %s", path);

		snprintf(path, PATH_MAX, "%s/%s/data.a", repodir, pkg->name);
		ar = ar_open_write(path);
		ar_set_wrkdir(ar, protodir);
		for (node = pkg->nodes; node; /* void */) {
			ar_append(ar, node->path);
			node = node->next;
		}
		ar_close(ar);

		snprintf(path, PATH_MAX, "%s/%s/manifest", repodir, pkg->name);
		manifest_emit(pkg, path);

		manifest_free(pkg);
	}

	return (0);
}

static void
usage(char *fmt, ...)
{
	va_list ap;

	if (fmt) {
		va_start(ap, fmt);
		vwarnx(fmt, ap);
		va_end(ap);
	}

	fprintf(stdout,
		"usage:\n"
		"\t%s -p protodir -r repodir manifest ...\n",
		getprogname());

	exit(2);
}
