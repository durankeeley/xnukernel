/* Minimal <uuid/uuid.h>: random (version 4) UUIDs from /dev/urandom. */
#pragma once
#include <stdio.h>
typedef unsigned char uuid_t[16];
static inline void uuid_generate(uuid_t u)
{
	FILE *f = fopen("/dev/urandom", "rb");
	if (!f || fread(u, 1, 16, f) != 16) { for (int i = 0; i < 16; i++) u[i] = (unsigned char)(i * 37 + 11); }
	if (f) fclose(f);
	u[6] = (u[6] & 0x0f) | 0x40;
	u[8] = (u[8] & 0x3f) | 0x80;
}
