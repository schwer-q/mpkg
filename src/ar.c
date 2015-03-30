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
#include <sys/time.h>
#include <sys/types.h>

#include <ctype.h>
#include <err.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#include "ar.h"
#include "xalloc.h"

struct ar {
	const char *filename;
	const char *wrkdir;

	int	fd;
	uint8_t	mode;
	int	offset;

	char	**strtab;
};

static ar_t	*ar_open(const char *filename, int flags);
static void	ar_write_data(ar_t *ar, ar_info_t *info);
static void	ar_write_header(ar_t *ar, ar_info_t *info);

ar_t *
ar_open_read(const char *filename)
{
	ar_t *ar;
	char buf[SARMAG+1];

	bzero(buf, sizeof(buf));
	ar = ar_open(filename, O_RDONLY|O_CLOEXEC);

	if (read(ar->fd, buf, SARMAG) < SARMAG)
		errx(1, "%s: invalid magic", ar->filename);
	if (strcmp(buf, ARMAG))
		errx(1, "%s: invalid magic", ar->filename);

	return (ar);
}

ar_t *
ar_open_write(const char *filename)
{
	ar_t *ar;
	ssize_t written;

	ar = ar_open(filename, O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC);
	ar->mode = 1;

	if ((written = write(ar->fd, ARMAG, SARMAG)) == -1)
		err(1, "write: %s", ar->filename);
	if (written < SARMAG)
		errx(1, "write: %s: truncated write", ar->filename);

	return (ar);
}

void
ar_close(ar_t *ar)
{
	int idx;

	for (idx = 0; ar->strtab[idx]; ++idx)
		free(ar->strtab[idx]);
	free(ar->strtab);

	close(ar->fd);
}

void
ar_append(ar_t *ar, const char *filename)
{
	ar_info_t *info, _info;
	char outfile[PATH_MAX];
	struct stat sb;

	info = &_info;
	bzero(info, sizeof(ar_info_t));
	bzero(outfile, PATH_MAX);

	snprintf(info->name, PATH_MAX, "%s", filename);
	snprintf(info->path, PATH_MAX, "%s/%s", ar->wrkdir, info->name);

	if (lstat(info->path, &sb) == -1)
		err(1, "lstat: %s", info->path);
	info->date = sb.st_mtime;
	info->uid = sb.st_uid;
	info->gid = sb.st_gid;
	info->mode = sb.st_mode;
	if (S_ISLNK(info->mode) || S_ISREG(info->mode))
		info->size = sb.st_size;

	ar_write_header(ar, info);
	ar_write_data(ar, info);
}

ar_info_t *
ar_next(ar_t *ar)
{
	ar_info_t *info;
	char buf[PATH_MAX+1];
	ssize_t nsize, nbytes;
	struct ar_hdr *hdr, _hdr;

	hdr = &_hdr;

	if (ar->offset) {
		if (lseek(ar->fd, SEEK_CUR, ar->offset) == -1)
			err(1, "lseek: %s", ar->filename);
		ar->offset = 0;
	}

	if ((nbytes = read(ar->fd, hdr, sizeof(struct ar_hdr))) == -1)
		err(1, "read: %s", ar->filename);
	if (nbytes == 0)
		return (NULL);
	if (nbytes < (ssize_t)sizeof(struct ar_hdr))
		errx(1, "read: %s: truncated entry header", ar->filename);

	if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG)))
		errx(1, "%s: invalid archive entry", ar->filename);

	info = xcalloc(1, sizeof(ar_info_t));

	nsize = (size_t)strtol(hdr->ar_name, (char **)NULL, 10);
	info->date = (time_t)strtol(hdr->ar_date, (char **)NULL, 10);
	info->uid = (uid_t)strtol(hdr->ar_uid, (char **)NULL, 10);
	info->gid = (gid_t)strtol(hdr->ar_gid, (char **)NULL, 10);
	info->mode = (mode_t)strtol(hdr->ar_mode, (char **)NULL, 8);
	info->size = strtol(hdr->ar_size, (char **)NULL, 10);

	bzero(buf, sizeof(buf));
	if ((nbytes = read(ar->fd, buf, nsize)) == -1)
		err(1, "read: %s", ar->filename);
	if (nbytes < nsize)
		errx(1, "read: %s: tuncated read", ar->filename);

	(void)strncpy(info->name, buf, nsize-1);
	snprintf(info->path, PATH_MAX, "%s/%s", ar->wrkdir, info->name);
	ar->offset = (int)info->size;

	return (info);
}

void
ar_extract(ar_t *ar, ar_info_t *info)
{
	char buf[512], target[PATH_MAX];
	int fd;
	size_t ebytes, nbytes;
	ssize_t length, written;
	struct timeval times;

	ar->offset = 0;

	switch (info->mode & S_IFMT) {
	case S_IFIFO:
		if (mkfifo(info->path, info->mode & 0007777) == -1)
			err(1, "mkfifo: '%s'", info->path);
		break;

	case S_IFDIR:
		if (mkdir(info->path, info->mode & 0007777) == -1)
			err(1, "mkdir: '%s'", info->path);
		break;

	case S_IFREG:
		if ((fd = open(info->path, O_WRONLY|O_CREAT|O_TRUNC)) == -1)
			err(1, "cannot open file: '%s'", info->path);

		for (ebytes = 0; ebytes < info->size; /* void */) {
			nbytes = (info->size - ebytes) % 512;
			if ((length = read(ar->fd, buf, nbytes+1)) == -1)
				err(1, "read: %s", ar->filename);
			if ((written = write(fd, buf, length)) == -1)
				err(1, "write: %s", info->path);
			if (written < length)
				errx(1, "write: %s: truncated write", info->path);
			ebytes += written;
		}

		close(fd);
		break;

	case S_IFLNK:
		bzero(target, PATH_MAX);
		if (read(ar->fd, target, info->size) == -1)
			err(1, "read: %s", ar->filename);
		if (symlink(target, info->path) == -1)
			err(1, "symlink: %s", info->path);
		break;

	case S_IFSOCK:		/* not supported */
	case S_IFCHR:		/* not supported */
	case S_IFBLK:		/* not supported */
	case S_IFWHT:		/* not supported */
		break;
	}

	bzero(&times, sizeof(struct timeval));
	times.tv_sec = info->date;
	if (lutimes(info->path, &times) == -1)
		err(1, "lutimes: %s", info->path);
}

void
ar_extract_all(ar_t *ar)
{
	ar_info_t *info, **dirs;
	int idx;
	struct timeval times;

	dirs = xcalloc(1, sizeof(ar_info_t *));
	while ((info = ar_next(ar))) {
		ar_extract(ar, info);

		if (S_ISDIR(info->mode)) {
			for (idx = 0; dirs[idx]; ++idx);
			dirs = xrealloc(dirs, idx+1);
			dirs[idx+1] = NULL;
			dirs[idx] = info;;

			continue;
		}

		free(info);
	}

	for (idx = 0; dirs[idx]; ++idx);
	while (idx > 0) {
		--idx;

		bzero(&times, sizeof(struct timeval));
		if (lutimes(dirs[idx]->path, &times) == -1)
			err(1, "lutimes: %s", dirs[idx]->path);

		free(dirs[idx]);
	}
	free(dirs);
}

void
ar_set_wrkdir(ar_t *ar, const char *wrkdir)
{
	ar->wrkdir = wrkdir;
}

static ar_t *
ar_open(const char *filename, int flags)
{
	ar_t *ar;

	ar = xcalloc(1, sizeof(ar_t));

	ar->filename = filename;
	ar->wrkdir = ".";

	ar->strtab = xcalloc(1, sizeof(char *));

	if ((ar->fd = open(filename, flags, 0644)) == -1)
		err(1, "open: %s", ar->filename);

	return (ar);
}

static void
ar_write_data(ar_t *ar, ar_info_t *info)
{
	char buf[512], target[PATH_MAX];
	int fd, nbytes;
	ssize_t written;

	if (S_ISLNK(info->mode)) {
		bzero(target, PATH_MAX);
		if (readlink(info->path, target, PATH_MAX-1) == -1)
			err(1, "readlink: %s", info->path);

		nbytes = strlen(target);
		if ((written = write(ar->fd, target, nbytes)) == -1)
			err(1, "write: %s", ar->filename);
		if (written < nbytes)
			errx(1, "write: %s: trucated write", ar->filename);
	}

	if (S_ISREG(info->mode)) {
		if ((fd = open(info->path, O_RDONLY|O_CLOEXEC)) == -1)
			err(1, "cannot open file: '%s'", info->path);

		while ((nbytes = read(fd, buf, 512)) > 0) {
			if ((written = write(ar->fd, buf, nbytes)) == -1)
				err(1, "write: %s", ar->filename);
			if (written < nbytes)
				errx(1, "written: %s: truncated write", ar->filename);
		}
		if (nbytes == -1)
			err(1, "read: %s", info->path);

		close(fd);
	}
}

static void
ar_write_header(ar_t *ar, ar_info_t *info)
{
	char buf[PATH_MAX+1];
	ssize_t nsize, written;
	struct ar_hdr *hdr, _hdr;

	hdr = &_hdr;
	hdr = memset(hdr, ' ', sizeof(struct ar_hdr));
	nsize = strlen(info->name) + 1;
	snprintf(hdr->ar_name, 6+1, "%-6d", nsize);
	snprintf(hdr->ar_date, 12+1, "%-12d", info->date);
	snprintf(hdr->ar_uid, 6+1, "%-6d", info->uid);
	snprintf(hdr->ar_gid, 6+1, "%-6d", info->gid);
	snprintf(hdr->ar_mode, 8+1, "%-8d", info->mode);
	snprintf(hdr->ar_size, 10+1, "%-10lld", info->size);
	(void)memcpy(hdr->ar_fmag, ARFMAG, 2);

	if ((written = write(ar->fd, hdr, sizeof(struct ar_hdr))) == -1)
		err(1, "write: %s", ar->filename);
	if (written < (ssize_t)sizeof(struct ar_hdr))
		errx(1, "write: %s: truncted write", ar->filename);

	snprintf(buf, sizeof(buf), "%s\n", info->name);
	if ((written = write(ar->fd, buf, nsize)) == -1)
		err(1, "write: %s", ar->filename);
	if (written < nsize)
		errx(1, "write: %s: truncated write", ar->filename);
}
