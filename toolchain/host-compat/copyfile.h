/* Minimal <copyfile.h>: fcopyfile(..., COPYFILE_DATA) via read/write. */
#pragma once
#include <unistd.h>
#include <errno.h>
typedef void *copyfile_state_t;
#define COPYFILE_DATA (1 << 3)
static inline int fcopyfile(int from, int to, copyfile_state_t s, unsigned int flags)
{
	char buf[65536];
	ssize_t n;

	(void)s; (void)flags;
	while ((n = read(from, buf, sizeof(buf))) > 0) {
		char *p = buf;
		while (n > 0) {
			ssize_t w = write(to, p, n);
			if (w < 0) { if (errno == EINTR) continue; return -1; }
			p += w; n -= w;
		}
	}
	return n < 0 ? -1 : 0;
}
