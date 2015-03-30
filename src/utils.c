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
#include <sys/types.h>

#include <err.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "utils.h"
#include "xalloc.h"

void
mpkg_copy(const char *src, const char *dst)
{
	char buf[512];
	ssize_t nbytes, written;
	int ifd, ofd;

	if ((ifd = open(src, O_RDONLY)) == -1)
		err(1, "cannot open file: %s", src);
	if ((ofd = open(dst, O_WRONLY|O_CREAT|O_TRUNC, 0644)) == -1)
		err(1, "cannot open file: %s", dst);

	while ((nbytes = read(ifd, buf, sizeof(buf))) > 0) {
		if ((written = write(ofd, buf, nbytes)) == -1)
			err(1, "write: %s", dst);
		if (written < nbytes)
			errx(1, "write: %s: truncated write", dst);
	}
	if (nbytes == -1)
		err(1, "read: %s", src);

	close(ifd);
	close(ofd);
}

void
mpkg_mkdirs(const char *path)
{
	char *p, *p1, *s;

	p = p1 = xstrdup(path);
	while ((s = strsep(&p, "/"))) {
		if (access(s, F_OK) == -1)
			if (mkdir(s, 0755) == -1)
				err(1, "mkdir: %s", s);
	}
	free(p1);
}
