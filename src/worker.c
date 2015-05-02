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
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "ar.h"
#include "catalog.h"
#include "db.h"
#include "manifest.h"
#include "mpkg.h"
#include "utils.h"
#include "worker.h"
#include "xalloc.h"

static inline void worker_install(worker_t *worker);
static inline void worker_uninstall(worker_t *worker);
static void worker_script(worker_t *worker, const char *arg);

worker_t *
worker_new(config_t *config, const char *package, int action, bool automatic)
{
	worker_t *worker;

	worker = xcalloc(1, sizeof(worker_t));
	worker->config = config;

	worker->package = xstrdup(package);
	worker->action = action;
	worker->automatic = automatic;
	return (worker);
}

void
worker_free(worker_t *worker)
{
	free(worker->package);
	free(worker);
}

void
worker_set_catalog(worker_t *worker, catalog_t *catalog)
{
	worker->catalog = catalog;
}

void
worker_set_db(worker_t *worker, db_t *db)
{
	worker->db = db;
}

static void
worker_depends(worker_t *worker, char **depends)
{
	catalog_t *obj;
	dbnode_t *node;
	int idx;
	worker_t *depw;

	for (idx = 0; depends[idx]; ++idx) {
		if (!(obj = catalog_find(worker->catalog, depends[idx])))
			errx(1, "%s: not found in catalog", worker->package);
		node = db_find(worker->db, depends[idx]);

		printf("%s depends on: %s - ", worker->package, depends[idx]);
		if (node && node->pkg->release >= obj->release) {
			printf("found\n");
		}
		else {
			printf("not found\n");

			depw = worker_new(worker->config,
					  depends[idx],
					  worker->action, true);
			worker_exec(depw);
			worker_free(depw);

			db_reload(worker->db);
			idx = -1;
		}
	}
}

static bool
worker_has_rdepends(worker_t *worker)
{
	dbnode_t *node;
	manifest_depend_t *depend;

	for (node = worker->db->nodes; node; /* void */) {
		if (!strcmp(node->pkg->name, worker->package)) {
			node = node->next;
			continue;
		}

		for (depend = node->pkg->depends; depend; /* void */) {
			if (!strcmp(depend->name, worker->package))
				return (true);
			depend = depend->next;
		}
	}
	return (false);
}

void
worker_exec(worker_t *worker)
{
	catalog_t *obj;
	dbnode_t *node;

	if (worker->action & (WORKER_ACTION_INSTALL|WORKER_ACTION_UPDATE)) {
		if (!(obj = catalog_find(worker->catalog, worker->package)))
			errx(1, "%s: not found in catalog", worker->package);

		if (obj->depends && *(obj->depends))
			worker_depends(worker, obj->depends);

		if (!(node = db_find(worker->db, worker->package)))
			worker->action = WORKER_ACTION_INSTALL;
		else if (node->pkg->release < obj->release)
			worker->action = WORKER_ACTION_UPDATE;
		else
			worker->action = WORKER_ACTION_NONE;
	}
	else if (worker->action == WORKER_ACTION_UNINSTALL) {
		if (worker_has_rdepends(worker))
			worker->action = WORKER_ACTION_NONE;
	}

	switch (worker->action) {
	case WORKER_ACTION_INSTALL:
		worker_script(worker, "preinstall");
		worker_install(worker);
		worker_script(worker, "postinstall");
		break;

	case WORKER_ACTION_UPDATE:
		worker_script(worker, "preupdate");
		worker_uninstall(worker);
		worker_install(worker);
		worker_script(worker, "postupdate");
		break;

	case WORKER_ACTION_UNINSTALL:
		worker_script(worker, "preuninstall");
		worker_uninstall(worker);
		worker_script(worker, "postuninstall");
		break;

	default:
		break;
	}
	return;
}

static void
worker_script(worker_t *worker, const char *arg)
{
	char *cmdline;
	char dst[PATH_MAX], src[PATH_MAX];

	if (worker->config->rootdir[0] == '/' &&
	    worker->config->rootdir[1] == '\0') {
		asprintf(&cmdline, "/bin/sh %s/%s/script %s",
			 worker->config->repodir, worker->package, arg);
		switch (system(cmdline)) {
		case -1:
			warn("system");
			break;
		case 127:
			warnx("system");
			break;

		default:
			break;
		}
		free(cmdline);
	}
	else {
		snprintf(src, PATH_MAX, "%s/%s/script",
			 worker->config->repodir, worker->package);
		snprintf(dst, PATH_MAX, "%s/tmp/script.XXXXXX",
			 worker->config->rootdir);

		if (access(src, R_OK) == 0)
			mpkg_copy_tmp(dst, src);

		asprintf(&cmdline, "/usr/sbin/chroot /bin/sh /tmp/%s %s",
			 basename(dst), arg);
		switch (system(cmdline)) {
		case -1:
			warn("system");
			break;
		case 127:
			warnx("system");
			break;

		default:
			break;
		}
		free(cmdline);

		unlink(dst);
	}
}

static inline void
worker_install(worker_t *worker)
{
	ar_t *ar;
	char arfile[PATH_MAX];

	snprintf(arfile, PATH_MAX, "%s/%s/data.a",
		 worker->config->repodir, worker->package);

	ar = ar_open_read(arfile);
	ar_set_wrkdir(ar, worker->config->rootdir);
	ar_extract_all(ar);
	ar_close(ar);
}

static inline void
worker_uninstall(worker_t *worker)
{
	DIR *dirp;
	bool empty;
	char path[PATH_MAX];
	dbnode_t *dnode;
	manifest_node_t *node;
	struct dirent *dirent;

	dnode = db_find(worker->db, worker->package);
	for (node = dnode->pkg->nodes; node; /* void */) {
		if (node->kind == MF_NODE_FILE) {
			snprintf(path, PATH_MAX, "%s/%s",
				 worker->config->rootdir, node->path);
			if (unlink(path) == -1)
				warn("unlink: %s", path);
		}
		node = node->next;
	}

	for (node = dnode->pkg->nodes; node; /* void */) {
		if (node->kind == MF_NODE_DIR) {
			snprintf(path, PATH_MAX, "%s/%s",
				 worker->config->rootdir, node->path);

			empty = true;
			if (!(dirp = opendir(path)))
				err(1, "opendir: %s", path);
			while ((dirent = readdir(dirp))) {
				if (!strcmp(dirent->d_name, ".") ||
				    !strcmp(dirent->d_name, ".."))
					continue;
				empty = false;
			}
			closedir(dirp);

			if (empty) {
				if (rmdir(path) == -1)
					err(1, "rmdir: %s", path);
			}
		}
		node = node->next;
	}
}
