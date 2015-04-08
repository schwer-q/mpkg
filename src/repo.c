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

#include <sys/types.h>

#include <dirent.h>
#include <err.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "catalog.h"
#include "manifest.h"
#include "xalloc.h"

static void	usage(char *fmt, ...);
static void	walk(catalog_t **head, const char *pathname);

int
main(int argc, char **argv)
{
	catalog_t *catalog;
	int ch, idx;

	while ((ch = getopt(argc, argv, "")) != -1) {
		switch (ch) {
		default:
			usage("%c -- unknown global option", (char)ch);
			break;
		}
	}
	if ((argc - optind) < 1)
		usage(NULL);

	for (idx = optind; idx < argc; ++idx) {
		catalog = NULL;

		walk(&catalog, argv[idx]);
		catalog_emit(catalog, argv[idx]);
		catalog_free(catalog);
	}

	return (0);
}

static void
walk(catalog_t **head, const char *pathname)
{
	DIR *dirp;
	catalog_t *obj, *tmp;
	char newpath[PATH_MAX];
	int idx;
	manifest_depend_t *depend;
	manifest_t *pkg;
	struct dirent *dirent;

	if (!(dirp = opendir(pathname))) {
		/* err(1, "opendir: %s", pathname); */
		warn("opendir: %s", pathname);
		return;
	}

	while ((dirent = readdir(dirp))) {
		if (!strcmp(dirent->d_name, ".") ||
		    !strcmp(dirent->d_name, ".."))
			continue;

		if (dirent->d_type == DT_DIR) {
			snprintf(newpath, PATH_MAX,
				 "%s/%s", pathname, dirent->d_name);
			walk(head, newpath);
		}

		if (!strcmp(dirent->d_name, "manifest")) {
			snprintf(newpath, PATH_MAX, "%s/manifest", pathname);

			pkg = manifest_parse(newpath);

			obj = xcalloc(1, sizeof(catalog_t));
			obj->package = xstrdup(pkg->name);
			obj->release = pkg->release;

			if (pkg->depends) {
				obj->depends = xcalloc(1, sizeof(char *));
				depend = pkg->depends;
				for (idx = 1; depend; ++idx) {
					obj->depends =
						xrealloc(obj->depends,
							 (idx + 1) *
							 sizeof(char *));
					obj->depends[idx] = NULL;
					obj->depends[idx - 1] =
						xstrdup(depend->name);
				}
			}

			manifest_free(pkg);
			if (!(*head))
				*head = obj;
			else {
				for (tmp = *head; tmp->next; /*  void */)
					tmp = tmp->next;
				tmp->next = obj;
			}
		}
	}

	closedir(dirp);

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
		"\t%s repodir ...\n",
		getprogname());

	exit(2);
}
