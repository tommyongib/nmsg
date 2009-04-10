/*
 * Copyright (c) 2008 by Internet Systems Consortium, Inc. ("ISC")
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/* Import. */

#include <assert.h>
#include <stdlib.h>
#include <unistd.h>

#include "nmsg.h"
#include "private.h"

/* Export. */

struct nmsg_buf *
nmsg_buf_new(size_t sz) {
	struct nmsg_buf *buf;

	buf = calloc(1, sizeof(*buf));
	if (buf == NULL)
		return (NULL);
	buf->data = calloc(1, sz);
	if (buf->data == NULL) {
		free(buf);
		return (NULL);
	}
	return (buf);
}

ssize_t
nmsg_buf_used(struct nmsg_buf *buf) {
	assert(buf->pos >= buf->data);
	return (buf->pos - buf->data);
}

ssize_t
nmsg_buf_avail(struct nmsg_buf *buf) {
	assert(buf->pos <= buf->end);
	return (buf->end - buf->pos);
}

void
nmsg_buf_destroy(struct nmsg_buf **buf) {
	if ((*buf)->fd != 0)
		close((*buf)->fd);
	if ((*buf)->data != NULL)
		free((*buf)->data);
	free(*buf);
	*buf = NULL;
}

void
nmsg_buf_reset(struct nmsg_buf *buf) {
	buf->end = buf->pos = buf->data;
}
