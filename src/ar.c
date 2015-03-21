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
#include <sys/stat.h>

#include <stdio.h>
#include <stdlib.h>
#include <err.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>

#include "ar.h"
#include "xalloc.h"

typedef struct {
	char	*filename;
	int	offset;
} ar_strtab;

struct ar {
	char		*filename; /* archive filename */
	char		*wrkdir;   /* archive work directory */

	FILE		*fp;	   /* file pointer */
	int		mode;	   /* true if open for writing */

	ar_strtab	**strtab; /* string table */
};

static void ar_write_header(ar_t *ar, ar_info_t *info);
static void ar_write_data(ar_t *ar, ar_info_t *info);
static void ar_write_strtab(ar_t *ar);

ar_t *
ar_open(const char *filename, int flags)
{
	ar_t *ar;

	ar = xcalloc(1, sizeof(ar_t));
	ar->filename = (char *)filename;
	ar->wrkdir = ".";
	ar->strtab = xcalloc(1, sizeof(ar_strtab *));

	if ((flags & AR_RDWR) == 0 || (flags & AR_RDWR) == AR_RDWR)
		errx(1, "ar_open(): invalid open flags");

	if (flags & AR_READ)
		ar->fp = fopen(ar->filename, "re");
	if (flags & AR_WRITE)
		ar->fp = fopen(ar->filename, "we");

	if (!ar->fp)
		err(1, "cannot open file: %s", ar->filename);

	if (flags & AR_WRITE) {
		ar->mode = 1;
		if (fwrite(ARMAG, sizeof(char), SARMAG, ar->fp) < SARMAG)
			err(1, "write: %s", ar->filename);
	}
	return (ar);
}

void
ar_close(ar_t *ar)
{
	int idx;

	if (ar->mode)
		ar_write_strtab(ar);

	fclose(ar->fp);
	for (idx = 0; ar->strtab[idx]; ++idx)
		free(ar->strtab[idx]);
	free(ar->strtab);
	free(ar);
}

void
ar_add(ar_t *ar, const char *filename)
{
	ar_info_t *info, _info;
	int idx, offset;
	struct stat sb;

	info = &_info;
	bzero(info, sizeof(ar_info_t));
	snprintf(info->path, PATH_MAX, "%s/%s", ar->wrkdir, filename);
	if (lstat(info->path, &sb) == -1)
		err(1, "cannot stat file: %s", info->path);

	if (strlen(filename) <= 15 && !strchr(filename, '/'))
		snprintf(info->name, PATH_MAX, "%s/", filename);
	else {
		for (idx = 0, offset = 0; ar->strtab[idx]; ++idx);
		ar->strtab = xrealloc(ar->strtab, idx+1);
		ar->strtab[idx+1] = NULL;
		ar->strtab[idx] = xcalloc(1, sizeof(ar_strtab));
		if (idx > 0) {
			offset = strlen(ar->strtab[idx-1]->filename);
			offset += ar->strtab[idx-1]->offset + 2;
		}
		ar->strtab[idx]->filename = xstrdup(filename);
		ar->strtab[idx]->offset = offset;
		snprintf(info->name, PATH_MAX, "/%d", ar->strtab[idx]->offset);
	}

	info->date = sb.st_mtime;
	info->uid = sb.st_uid;
	info->gid = sb.st_gid;
	info->mode = sb.st_mode;
	if ((info->mode & S_IFMT) == (S_IFLNK|S_IFREG))
		info->size = sb.st_size;
	else
		info->size = 0;

	ar_write_header(ar, info);
	if ((info->mode & S_IFMT) == (S_IFLNK|S_IFREG))
		ar_write_data(ar, info);
}

static void
ar_write_header(ar_t *ar, ar_info_t *info)
{
	struct ar_hdr *hdr, _hdr;
	size_t written;

	hdr = &_hdr;
	(void)memset(hdr, ' ', sizeof(struct ar_hdr));
	snprintf(hdr->ar_name, sizeof(hdr->ar_name)+1, "%-16s", info->name);
	snprintf(hdr->ar_date, sizeof(hdr->ar_date)+1, "%-12d", info->date);
	snprintf(hdr->ar_uid, sizeof(hdr->ar_uid)+1, "%-6d", info->uid);
	snprintf(hdr->ar_gid, sizeof(hdr->ar_gid)+1, "%-6d", info->gid);
	snprintf(hdr->ar_mode, sizeof(hdr->ar_mode)+1, "%-8d", info->mode);
	snprintf(hdr->ar_size, sizeof(hdr->ar_size)+1, "%-10lld", info->size);
	(void)memcpy(hdr->ar_fmag, ARFMAG, sizeof(hdr->ar_fmag));

	written = fwrite(hdr, 1, sizeof(struct ar_hdr), ar->fp);
	if (written < sizeof(struct ar_hdr))
		err(1, "write: %s", ar->filename);
}

static void
ar_write_data(ar_t *ar, ar_info_t *info)
{
	FILE *ifs;
	char buf[512], pathname[PATH_MAX];
	size_t length, written;

	if (S_ISLNK(info->mode)) {
		bzero(pathname, PATH_MAX);
		if (readlink(info->path, pathname, PATH_MAX-1) == -1)
			err(1, "readlink: %s", info->path);
		length = strlen(pathname);
		written = fwrite(pathname, 1, length, ar->fp);
		if (written < length)
			err(1, "write: %s", ar->filename);
	}

	if (S_ISREG(info->mode)) {
		if (!(ifs = fopen(info->path, "r")))
			err(1, "cannot open file: %s", info->path);

		while ((length = fread(buf, 1, 512, ifs))) {
			written = fwrite(buf, 1, length, ar->fp);
			if (written < length)
				err(1, "write: %s", ar->filename);
		}
		if (!feof(ifs) && ferror(ifs))
			err(1, "read: %s", info->path);
		fclose(ifs);
	}
}

static void
ar_write_strtab(ar_t *ar)
{
	ar_info_t *info, _info;
	int idx;
	size_t size, written;

	info = &_info;
	bzero(info, sizeof(ar_info_t));
	for (idx = 0, size = 0; ar->strtab[idx]; ++idx)
		size += strlen(ar->strtab[idx]->filename) + 2;
	(void)strcpy(info->name, "//");
	info->size = size;

	if (fseek(ar->fp, SARMAG, SEEK_SET) == -1)
		err(1, "seek: %s", ar->filename);

	ar_write_header(ar, info);
	for (idx = 0; ar->strtab[idx]; ++idx) {
		written = fprintf(ar->fp, "%s/\n", ar->strtab[idx]->filename);
		if (written < (strlen(ar->strtab[idx]->filename)+2))
			err(1, "write: %s", ar->filename);
	}
}
