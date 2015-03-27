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
#include <sys/time.h>

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <strings.h>
#include <string.h>
#include <ctype.h>
#include <libgen.h>

#include "ar.h"
#include "xalloc.h"

struct ar {
	const char *filename;
	const char *wrkdir;

	int	fd;
	uint8_t	mode;
	int	offset;
	int     nchunk;

	char	**strtab;
};

static ar_t	*ar_open(const char *filename, int flags);
static int	ar_register_strtab(ar_t *ar, const char *filename);
static void	ar_read_strtab(ar_t *ar, ar_info_t *info);
static void	ar_write_data(ar_info_t *info, int ofd, const char *outfile);
static void	ar_write_finalize(ar_t *ar);
static void	ar_write_header(ar_info_t *info, int ofd, const char *outfile);
static void	ar_write_strtab(ar_t *ar);

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

	if (ar->mode)
		ar_write_finalize(ar);

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
	int ofd, offset;
	struct stat sb;

	info = &_info;
	bzero(info, sizeof(ar_info_t));
	bzero(outfile, PATH_MAX);

	if (strlen(filename) <= 15 && !strchr(filename, '/'))
		snprintf(info->name, PATH_MAX, "%s/", filename);
	else {
		offset = ar_register_strtab(ar, filename);
		snprintf(info->name, PATH_MAX, "/%d", offset);
	}
	snprintf(info->path, PATH_MAX, "%s/%s", ar->wrkdir, filename);

	if (lstat(info->path, &sb) == -1)
		err(1, "lstat: %s", info->path);
	info->date = sb.st_mtime;
	info->uid = sb.st_uid;
	info->gid = sb.st_gid;
	info->mode = sb.st_mode;
	if (S_ISLNK(info->mode) || S_ISREG(info->mode))
		info->size = sb.st_size;

	snprintf(outfile, PATH_MAX, "%s/%d.a",
		 dirname(ar->filename), ar->nchunk);
	if ((ofd = open(outfile, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1)
		err(1, "cannot open file: %s", outfile);

	ar_write_header(info, ofd, outfile);
	ar_write_data(info, ofd, outfile);

	close(ofd);
	++(ar->nchunk);
}

ar_info_t *
ar_next(ar_t *ar)
{
	ar_info_t *info;
	char *ptr;
	int i, idx, offset;
	ssize_t length;
	struct ar_hdr *hdr, _hdr;

	hdr = &_hdr;

	if (ar->offset) {
		if (lseek(ar->fd, SEEK_CUR, ar->offset) == -1)
			err(1, "lseek: %s", ar->filename);
		ar->offset = 0;
	}

	if ((length = read(ar->fd, hdr, sizeof(struct ar_hdr))) == -1)
		err(1, "read: %s", ar->filename);
	if (length == 0)
		return (NULL);
	if (length < sizeof(struct ar_hdr))
		errx(1, "read: %s: truncated entry header", ar->filename);

	if (strncmp(hdr->ar_fmag, ARFMAG, sizeof(ARFMAG)))
		errx(1, "%s: invalid archive entry", ar->filename);

	info = xcalloc(1, sizeof(ar_info_t));

	/*
	 * If the entry is a string table, read it then
	 * return the info for the next header.
	 */
	if (!strncmp(hdr->ar_name, "//", 2)) {
		(void)strcpy(info->name, "//");
		info->size = strtol(hdr->ar_size, (char **)NULL, 10);
		ar_read_strtab(ar, info);

		return (ar_next(ar));
	}
	snprintf(info->path, PATH_MAX, "%s/%s", ar->wrkdir, info->name);

	/*
	 * If hdr->ar_name starts with a '/' followed by a number, then we need
	 * to get the file name in the string table at the offset pointed in the
	 * hdr->ar_name field.
 	 */
	if (hdr->ar_name[0] == '/' && isdigit(hdr->ar_name[1])) {
		offset = (int)strtol(hdr->ar_name+1, (char **)NULL, 10);
		for (idx = 0, i = 0; ar->strtab[idx] && i < offset; ++idx)
			i += strlen(ar->strtab[idx]) + 2;
		(void)strcpy(info->name, ar->strtab[idx]);
	}
	else {
		if (!(ptr = strchr(hdr->ar_name, '/')))
			errx(1, "%s: invalid entry name", ar->filename);
		*ptr = '\0';
		(void)strcpy(info->name, hdr->ar_name);
	}

	info->date = (time_t)strtol(hdr->ar_date, (char **)NULL, 10);
	info->uid = (uid_t)strtol(hdr->ar_uid, (char **)NULL, 10);
	info->gid = (gid_t)strtol(hdr->ar_gid, (char **)NULL, 10);
	info->mode = (mode_t)strtol(hdr->ar_mode, (char **)NULL, 8);
	info->size = strtol(hdr->ar_size, (char **)NULL, 10);
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

static int
ar_register_strtab(ar_t *ar, const char *filename)
{
	int idx, offset;

	for (idx = 1, offset = 0; ar->strtab[idx]; ++idx)
		offset += strlen(ar->strtab[idx]) + 2;

	ar->strtab = xrealloc(ar->strtab, (idx+1) * sizeof(char *));
	ar->strtab[idx] = NULL;
	ar->strtab[idx-1] = xstrdup(filename);

	return (offset);
}

static void
ar_read_strtab(ar_t *ar, ar_info_t *info)
{
	char *buf, *ptr, *fname;
	int idx;
	ssize_t nbytes;

	buf = xcalloc(info->size, sizeof(char));
	if ((nbytes = read(ar->fd, buf, info->size)) == -1)
		err(1, "read: %s", ar->filename);
	if (nbytes < info->size)
		errx(1, "read: %s: truncated read", ar->filename);

	for (ptr = buf; (fname = strsep(&ptr, "\n")); /* void */) {
		if (*fname == '\0')
			continue;

		idx = strlen(fname);
		if (fname[idx-1] != '/')
			errx(1, "%s: invalid string table entry", ar->filename);
		fname[idx-1] = '\0';

		(void)ar_register_strtab(ar, fname);
	}
	free(buf);
}

static void
ar_write_finalize(ar_t *ar)
{
	char buf[512], chkname[PATH_MAX];
	int fd, nchunk;
	ssize_t nbytes, written;

	ar_write_strtab(ar);
	for (nchunk = 0; nchunk < ar->nchunk; ++nchunk) {
		snprintf(chkname, PATH_MAX, "%s/%d.a",
			 dirname(ar->filename), nchunk);
		if ((fd = open(chkname, O_RDONLY)) == -1)
			err(1, "cannot open file: %s", chkname);

		while ((nbytes = read(fd, buf, sizeof(buf))) > 0) {
			if ((written = write(ar->fd, buf, nbytes)) == -1)
				err(1, "write: %s", ar->filename);
			if (written < nbytes)
				errx(1, "write: %s: truncated write", ar->filename);
		}
		if (nbytes == -1)
			err(1, "read: %s", chkname);

		close(fd);
		if (unlink(chkname) == -1)
			err(1, "unlink: %s", chkname);
	}
}

static void
ar_write_header(ar_info_t *info, int ofd, const char *outfile)
{
	ssize_t written;
	struct ar_hdr *hdr, _hdr;

	hdr = &_hdr;
	hdr = memset(hdr, ' ', sizeof(struct ar_hdr));
	snprintf(hdr->ar_name, SAR_NAME+1, "%-16s", info->name);
	snprintf(hdr->ar_date, SAR_DATE+1, "%-12d", info->date);
	snprintf(hdr->ar_uid, SAR_UID+1, "%-6d", info->uid);
	snprintf(hdr->ar_gid, SAR_GID+1, "%-6d", info->gid);
	snprintf(hdr->ar_mode, SAR_MODE+1, "%-8d", info->mode);
	snprintf(hdr->ar_size, SAR_SIZE+1, "%-10lld", info->size);
	(void)memcpy(hdr->ar_fmag, ARFMAG, SAR_FMAG);

	if ((written = write(ofd, hdr, sizeof(struct ar_hdr))) == -1)
		err(1, "write: %s", outfile);
	if (written < sizeof(struct ar_hdr))
		errx(1, "write: %s: truncted write", outfile);
}

static void
ar_write_data(ar_info_t *info, int ofd, const char *outfile)
{
	char buf[512], target[PATH_MAX];
	int fd, nbytes;
	ssize_t written;

	if (S_ISLNK(info->mode)) {
		bzero(target, PATH_MAX);
		if (readlink(info->path, target, PATH_MAX-1) == -1)
			err(1, "readlink: %s", info->path);

		nbytes = strlen(target);
		if ((written = write(ofd, target, nbytes)) == -1)
			err(1, "write: %s", outfile);
		if (written < nbytes)
			errx(1, "write: %s: trucated write", outfile);
	}

	if (S_ISREG(info->mode)) {
		if ((fd = open(info->path, O_RDONLY|O_CLOEXEC)) == -1)
			err(1, "cannot open file: '%s'", info->path);

		while ((nbytes = read(fd, buf, 512)) > 0) {
			if ((written = write(ofd, buf, nbytes)) == -1)
				err(1, "write: %s", outfile);
			if (written < nbytes)
				errx(1, "written: %s: truncated write", outfile);
		}
		if (nbytes == -1)
			err(1, "read: %s", info->path);

		close(fd);
	}
}

static void
ar_write_strtab(ar_t *ar)
{
	char buf[PATH_MAX+2];
	int idx, nbytes;
	ssize_t written;
	struct ar_hdr *hdr, _hdr;

	hdr = &_hdr;
	(void)memset(hdr, ' ', sizeof(struct ar_hdr));

	for (idx = 0, nbytes = 0; ar->strtab[idx]; ++idx)
		nbytes += strlen(ar->strtab[idx]) + 2;

	(void)memcpy(hdr->ar_name, "//", 2);
	snprintf(hdr->ar_size, SAR_SIZE+1, "%-10d", nbytes);
	(void)memcpy(hdr->ar_fmag, ARFMAG, SAR_FMAG);

	/* is this check really needed? */
	if (lseek(ar->fd, SARMAG, SEEK_SET) == -1)
		err(1, "lseek: %s", ar->filename);

	if ((written = write(ar->fd, hdr, sizeof(struct ar_hdr))) == -1)
		err(1, "write: %s", ar->filename);
	if (written < sizeof(struct ar_hdr))
		errx(1, "write: %s: truncated write", ar->filename);

	for (idx = 0; ar->strtab[idx]; ++idx) {
		bzero(buf, sizeof(buf));
		(void)strcpy(buf, ar->strtab[idx]);
		(void)strcat(buf, "/\n");

		nbytes = strlen(buf);
		if ((written = write(ar->fd, buf, nbytes)) == -1)
			err(1, "write: %s", ar->filename);
		if (written < nbytes)
			errx(1, "write: %s: truncated write", ar->filename);
	}
}
