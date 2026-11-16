/* Host (Linux) stand-in for XNU's <sys/_types.h>. */
#pragma once
#include <i386/_types.h>
#include <sys/types.h>
typedef int32_t  __darwin_pid_t;
typedef uint32_t __darwin_uid_t;
typedef uint32_t __darwin_gid_t;
typedef uint32_t __darwin_mach_port_name_t;
typedef __darwin_mach_port_name_t __darwin_mach_port_t;
typedef uint16_t __darwin_mode_t;
typedef int64_t  __darwin_off_t;
typedef unsigned char __darwin_uuid_t[16];
typedef char __darwin_uuid_string_t[37];
