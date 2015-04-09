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

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "index.h"
#include "xalloc.h"

void
index_free(index_t *index)
{
	index_t *tmp;
	int i;

	while (index) {
		tmp = index;
		index = index->next;
		free(tmp->name);
		if (tmp->depends) {
			for (i = 0; tmp->depends[i]; ++i)
				free(tmp->depends[i]);
			free(tmp->depends);
		}
		free(tmp);
	}
}

void
index_emit(index_t *index, const char *filename)
{
	FILE *ofs;
	int idx;

	if (!(ofs = fopen(filename, "w")))
		err(1, "%s", filename);

	while (index) {
		fprintf(ofs, "%s|%d|", index->name, index->release);
		if (index->depends) {
			fprintf(ofs, "%s", *(index->depends));
			for (idx = 1; index->depends[idx]; ++idx)
				fprintf(ofs, ",%s", index->depends[idx]);
		}
		index = index->next;
	}

	fclose(ofs);
}

index_t *
index_parse(const char *filename)
{
	FILE *ifs;
	char *line = NULL, *myline, *myline1, *token;
	index_t *index = NULL, *tmp, *tmp1;
	int idx, lineno = 0;
	size_t linecap = 0;
	ssize_t linelen;

	if (!(ifs = fopen(filename, "r")))
		err(1, "%s", filename);

	while ((linelen = getline(&line, &linecap, ifs)) > 0) {
		++lineno;
		tmp = xcalloc(1, sizeof(index_t));
		myline = myline1 = xstrdup(line);

		for (idx = 0; (token = strsep(&myline, "|")); ++idx) {
			if (idx == 0)
				tmp->name = xstrdup(token);
			if (idx == 1)
				tmp->release = (int)strtol(token, (char **)NULL, 10);
			if (idx == 2) {
				myline = token;
				tmp->depends = xcalloc(1, sizeof(char *));

				for (idx = 0; (token = strsep(&myline, ",")); ++idx) {
					tmp->depends = xrealloc(tmp->depends, idx + 1);
					tmp->depends[idx+1] = NULL;
					tmp->depends[idx] = xstrdup(token);
				}
			}
		}
		free(myline1);

		if (index) {
			for (tmp1 = index; tmp->next; /* void */)
				tmp1 = tmp1->next;
			tmp1->next = tmp;
		}
		else
			index = tmp;
	}
	free(line);

	fclose(ifs);
	return (index);
}
