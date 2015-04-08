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

#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "manifest.h"
#include "xalloc.h"

static void	mf_config(manifest_t *mf, const char *arg1);
static void	mf_depend(manifest_t *mf, const char *arg1);
static void	mf_dir(manifest_t *mf, const char *arg1);
static void	mf_file(manifest_t *mf, const char *arg1);
static void	mf_package(manifest_t *mf, const char *arg1);
static void	mf_release(manifest_t *mf, const char *arg1);
static void	mf_script(manifest_t *mf, const char *arg1);

static struct {
	const char	*name;
	void		(*callback)(manifest_t *, const char *);
} commands[] = {
	{ "config",	mf_config },
	{ "depend",	mf_depend },
	{ "dir",	mf_dir },
	{ "file",	mf_file },
	{ "package",	mf_package },
	{ "release",	mf_release },
	{ "script",	mf_script },
	{ NULL,		NULL }
};

void
manifest_free(manifest_t *mf)
{
	manifest_depend_t *depend;
	manifest_node_t *node;

	free(mf->name);
	if (mf->depends) {
		while (mf->depends) {
			depend = mf->depends;
			mf->depends = mf->depends->next;
			free(depend->name);
			free(depend);
		}
	}
	if (mf->nodes) {
		while (mf->nodes) {
			node = mf->nodes;
			mf->nodes = mf->nodes->next;
			free(node->path);
			free(node);
		}
	}
	free(mf);
}

void
manifest_emit(manifest_t *mf, const char *filename)
{
	FILE *ofs;
	manifest_depend_t *depend;
	manifest_node_t *node;

	if (!(ofs = fopen(filename, "w")))
		err(1, "%s", filename);

	fprintf(ofs,
		"#\n"
		"# Automatically generated by mpkg-create\n"
		"# /!\\ DO NOT EDIT /!\\\n"
		"#\n\n");

	fprintf(ofs,
		"package\t%s\n"
		"release\t%d\n\n", mf->name, mf->release);

	for (depend = mf->depends; depend; /* void */) {
		fprintf(ofs, "depend\t%s\n", depend->name);
		depend = depend->next;
	}
	fprintf(ofs, "\n");

	for (node = mf->nodes; node; /* void */) {
		switch (node->kind) {
		case MF_NODE_CONFIG:
			fprintf(ofs, "config");
			break;
		case MF_NODE_DIR:
			fprintf(ofs, "dir");
			break;
		case MF_NODE_FILE:
			fprintf(ofs, "file");
			break;
		}
		fprintf(ofs, "\t%s\n", node->path);
		node = node->next;
	}

	fclose(ofs);
}

manifest_t *
manifest_parse(const char *filename)
{
	FILE *ifs;
	char **args;
	char *line = NULL, *myline, *myline1, *s;
	int idx, lineno = 0;
	manifest_t *mf;
	size_t linecap = 0;
	ssize_t linelen;

	if (!(ifs = fopen(filename, "r")))
		err(1, "%s", filename);
	mf = xcalloc(1, sizeof(manifest_t));

	while ((linelen = getline(&line, &linecap, ifs)) > 0) {
		++lineno;
		args = xcalloc(1, sizeof(char *));
		myline = myline1 = xstrdup(line);

		while (isspace(*myline))
			++myline;
		if (*myline == '\0' || *myline == '#')
			goto next;

		for (idx = 1; (s = strsep(&myline, WS)); /* void */) {
			if (*s == '\0')
				continue;

			args = xrealloc(args, (idx+1) * sizeof(char *));
			args[idx] = NULL;
			args[idx-1] = s;
			++idx;
		}

		--idx;
		if (idx < 2)
			errx(1, "%s:%d: not enough arguments", filename, lineno);
		if (idx > 2)
			errx(1, "%s:%d: too many arguments", filename, lineno);

		for (idx = 0; commands[idx].name; ++idx) {
			if (!strcmp(*args, commands[idx].name))
				break;
		}

		if (!commands[idx].name)
			errx(1, "%s:%d: %s: unknown command", filename, lineno, *args);

		commands[idx].callback(mf, *(args + 1));

	next:
		free(args);
		free(myline1);
	}

	fclose(ifs);
	return (mf);
}

static void
mf_config(manifest_t *mf, const char *arg1)
{
	manifest_node_t *node, *tmp;

	node = xcalloc(1, sizeof(manifest_node_t));
	node->path = xstrdup(arg1);
	node->kind = MF_NODE_CONFIG;

	if (!mf->nodes) {
		mf->nodes = node;
	}
	else {
		for (tmp = mf->nodes; tmp->next; /* void */)
			tmp = tmp->next;
		tmp->next = node;
	}
}

static void
mf_depend(manifest_t *mf, const char *arg1)
{
	manifest_depend_t *depend, *tmp;

	depend = xcalloc(1, sizeof(manifest_depend_t));
        depend->name = xstrdup(arg1);

	if (!mf->depends) {
		mf->depends = depend;
	}
	else {
		for (tmp = mf->depends; tmp->next; /* void */)
			tmp = tmp->next;
		tmp->next = depend;
	}
}

static void
mf_dir(manifest_t *mf, const char *arg1)
{
	manifest_node_t *node, *tmp;

	node = xcalloc(1, sizeof(manifest_node_t));
	node->path = xstrdup(arg1);
	node->kind = MF_NODE_DIR;

	if (!mf->nodes) {
		mf->nodes = node;
	}
	else {
		for (tmp = mf->nodes; tmp->next; /* void */)
			tmp = tmp->next;
		tmp->next = node;
	}
}

static void
mf_file(manifest_t *mf, const char *arg1)
{
	manifest_node_t *node, *tmp;

	node = xcalloc(1, sizeof(manifest_node_t));
	node->path = xstrdup(arg1);
	node->kind = MF_NODE_FILE;

	if (!mf->nodes) {
		mf->nodes = node;
	}
	else {
		for (tmp = mf->nodes; tmp->next; /* void */)
			tmp = tmp->next;
		tmp->next = node;
	}
}

static void
mf_package(manifest_t *mf, const char *arg1)
{
	mf->name = xstrdup(arg1);
}

static void
mf_release(manifest_t *mf, const char *arg1)
{
	mf->release = (int)strtol(arg1, (char **)NULL, 10);
}

static void
mf_script(manifest_t *mf, const char *arg1)
{
	mf->script = xstrdup(arg1);
}
