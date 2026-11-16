/* Force-included into XNU's build-time tools on Linux. */
#pragma once
#define _GNU_SOURCE 1
#include <errno.h>
#define getprogname() program_invocation_short_name
#ifndef EX_UNAVAILABLE
#include <sysexits.h>
#endif
#include <sys/types.h>
#include <fcntl.h>
/* BSD mode parsing, from libbsd (linked with -lbsd) */
void *setmode(const char *mode_str);
mode_t getmode(const void *set, mode_t mode);
/* macOS: open the link itself; the SETUP tools only open regular files */
#ifndef O_SYMLINK
#define O_SYMLINK 0
#endif
