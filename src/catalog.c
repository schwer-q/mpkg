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

#if !defined(_WITH_GETLINE)
#define _WITH_GETLINE
#endif

#include <ctype.h>
#include <err.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "catalog.h"
#include "xalloc.h"

catalog_t *
catalog_new(void)
{
	catalog_t *catalog;

	catalog = xcalloc(1, sizeof(catalog_t));
	return (catalog);
}

void
catalog_free(catalog_t *catalog)
{
	catalog_t *obj;
	int idx;

	while (catalog) {
		obj = catalog;

		free(obj->package);
		if (obj->depends) {
			for (idx = 0; obj->depends[idx]; ++idx)
				free(obj->depends[idx]);
			free(obj->depends);
		}
		catalog = obj->next;
		free(obj);
	}
}

void
catalog_emit(catalog_t *catalog, const char *path)
{
	FILE *fp;
	char outfile[PATH_MAX];
	int idx;

	snprintf(outfile, PATH_MAX, "%s/catalog", path);
	if (!(fp = fopen(outfile, "w")))
		err(1, "fopen: %s", outfile);

	fprintf(fp,
		"#\n"
		"# Created by mpkg-repo\n"
		"# /!\\ DO NOT EDIT!!! /!\\\n"
		"#\n\n");

	while (catalog) {
		fprintf(fp, "%s|%d|", catalog->package, catalog->release);

		if (catalog->depends) {
			for (idx = 0; catalog->depends[idx]; ++idx) {
				if (idx == 0)
					fprintf(fp, "%s",
						catalog->depends[idx]);
				else
					fprintf(fp, ",%s",
						catalog->depends[idx]);
			}
		}
	        fprintf(fp, "\n");
		catalog = catalog->next;
	}

	fclose(fp);
}

catalog_t *
catalog_parse(const char *path)
{
	FILE *fp;
	catalog_t *catalog, *obj, *tmp;
	char *line, *myline, *myline1, *s;
	char infile[PATH_MAX];
	size_t idx, idx1, linecap, lineno;
	ssize_t linelen;

	snprintf(infile, PATH_MAX, "%s/catalog", path);
	if (!(fp = fopen(infile, "r")))
		err(1, "fopen: %s", infile);

	catalog = NULL; line = NULL;
	linecap = lineno = 0;
	while ((linelen = getline(&line, &linecap, fp)) > 0) {
		++lineno;
		myline = myline1 = xstrdup(line);
		while (isspace(*myline))
			++myline;
		if (*myline == '\0' || *myline == '#')
			goto next;

		obj = xcalloc(1, sizeof(catalog_t));
		for (idx = 0; (s = strsep(&myline, "|")); ++idx) {
			if (*s == '\0')
				errx(1, "%s:%d: empty field", infile, lineno);
			if (*s == '\n')
				continue;

			if (idx == 0)
				obj->package = xstrdup(s);
			if (idx == 1)
				obj->release =
					(int)strtol(s, (char **)NULL, 10);
			if (idx == 2) {
				obj->depends = xcalloc(1, sizeof(char *));
				idx1 = 1;
				while ((s = strsep(&s, ","))) {
					obj->depends =
						xrealloc(obj->depends,
							 (idx1 + 1) *
							 sizeof(char *));
					obj->depends[idx1] = NULL;
					obj->depends[idx1 - 1] = xstrdup(s);
					++idx1;
				}
			}
		}

		if (!catalog)
			catalog = obj;
		else {
			for (tmp = catalog; tmp->next; /* void */)
				tmp = tmp->next;
			tmp->next = obj;
		}

	next:
		free(myline1);
	}
	free(line);

	return (catalog);
}

catalog_t *
catalog_find(catalog_t *catalog, const char *package)
{
	catalog_t *obj;

	for (obj = catalog; obj; /* void */) {
		if (!strcmp(obj->package, package))
			return (obj);
		obj = obj->next;
	}
	return (NULL);
}
