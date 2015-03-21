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

#ifndef __ARCHIVE_H
#define __ARCHIVE_H

#define AR_READ		0x1
#define AR_WRITE	0x2
#define AR_RDWR		(AR_READ|AR_WRITE)

#define ARMAG	"!<arch>\n"	/* ar "magic number" */
#define SARMAG	8		/* strlen(ARMAG) */
#define ARFMAG	"`\n"		/* ar entry "magic number" */

typedef struct ar ar_t;
typedef struct ar_info ar_info_t;

struct ar_hdr {
	char ar_name[16];	/* name */
	char ar_date[12];	/* last modification time */
	char ar_uid[6];		/* user id */
	char ar_gid[6];		/* group id */
	char ar_mode[8];	/* octal file permissions */
	char ar_size[10];	/* size in bytes */
	char ar_fmag[2];	/* consistency check */
} __attribute__((packed));

struct ar_info {
	char	name[PATH_MAX];	/* name */
	char	path[PATH_MAX];	/* full path */
	time_t	date;		/* last modification time */
	uid_t	uid;		/* user id */
	gid_t	gid;		/* group id */
	mode_t	mode;		/* octal file permissions */
	off_t	size;		/* size in bytes */
};

ar_t	*ar_open(const char *filename, int flags);
void	ar_close(ar_t *ar);

void	ar_add(ar_t *ar, const char *filename);

#endif	/* __ARCHIVE_H */
