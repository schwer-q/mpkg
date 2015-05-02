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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "db.h"
#include "manifest.h"
#include "utils.h"
#include "xalloc.h"

db_t *
db_init(const char *path)
{
	db_t *db;

	db = xcalloc(1, sizeof(db_t));
	db->path = (char *)path;

	if (access(db->path, X_OK) == -1)
		mpkg_mkdirs(db->path);
	return (db);
}

void
db_free(db_t *db)
{
	dbnode_t *dbnode, *tmp;

	for (dbnode = db->nodes; dbnode; /* void */) {
		tmp = dbnode->next;
		manifest_free(dbnode->pkg);
		free(dbnode);
		dbnode = tmp;
	}
	free(db);
}

static dbnode_t *
db_import(const char *path)
{
	char mypath[PATH_MAX];
	dbnode_t *dbnode;

	dbnode = xcalloc(1, sizeof(dbnode_t));

	bzero(mypath, sizeof(char) * PATH_MAX);
	snprintf(mypath, PATH_MAX, "%s/manifest", path);
	if (access(mypath, R_OK) == -1) {
		free(dbnode);
		return (NULL);
	}
	dbnode->pkg = manifest_parse(mypath);

	bzero(mypath, sizeof(char) * PATH_MAX);
	snprintf(mypath, PATH_MAX, "%s/automatic", path);
	if (access(mypath, R_OK) == 0)
		dbnode->automatic = 1;

	return (dbnode);
}

void
db_load(db_t *db)
{
	DIR *dirp;
	struct dirent *dirent;
	dbnode_t *dbnode, *tmp;
	char path[PATH_MAX];

	if (!(dirp = opendir(db->path)))
		err(1, "opendir: %s", db->path);
	while ((dirent = readdir(dirp))) {
		if (!strcmp(dirent->d_name, ".") ||
		    !strcmp(dirent->d_name, ".."))
			continue;
		if (dirent->d_type != DT_DIR)
			continue;

		bzero(path, sizeof(char) * PATH_MAX);
		snprintf(path, PATH_MAX, "%s/%s", db->path, dirent->d_name);
		dbnode = db_import(path);

		if (!db->nodes)
			db->nodes = dbnode;
		else {
			for (tmp = db->nodes; tmp->next; /* void */)
				tmp = tmp->next;
			tmp->next = dbnode;
		}
	}
	(void)closedir(dirp);
}

void
db_reload(db_t *db)
{
	dbnode_t *dbnode, *tmp;

	for (dbnode = db->nodes; dbnode; /* void */) {
		tmp = dbnode->next;
		manifest_free(dbnode->pkg);
		free(dbnode);
		dbnode = tmp;
	}
	db_load(db);
}

dbnode_t *
db_find(db_t *db, const char *package)
{
	dbnode_t *node;

	for (node = db->nodes; node; /* void */) {
		if (!strcmp(node->pkg->name, package))
			return (node);
		node = node->next;
	}
	return (NULL);
}
