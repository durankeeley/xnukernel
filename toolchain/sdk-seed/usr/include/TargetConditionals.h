/*
 * TargetConditionals.h for this cross toolchain: macOS on x86_64, as the
 * macOS 10.13 SDK defines it. (Apple's open-source CarbonHeaders-18.1 copy
 * predates TARGET_OS_OSX and friends.)
 */
#ifndef __TARGETCONDITIONALS__
#define __TARGETCONDITIONALS__

#define TARGET_OS_MAC               1
#define TARGET_OS_WIN32             0
#define TARGET_OS_UNIX              0
#define TARGET_OS_OSX               1
#define TARGET_OS_MAC_DESKTOP       1
#define TARGET_OS_IPHONE            0
#define TARGET_OS_IOS               0
#define TARGET_OS_WATCH             0
#define TARGET_OS_BRIDGE            0
#define TARGET_OS_TV                0
#define TARGET_OS_SIMULATOR         0
#define TARGET_OS_EMBEDDED          0
#define TARGET_OS_NANO              0
#define TARGET_IPHONE_SIMULATOR     0

#if defined(__x86_64__)
#define TARGET_CPU_X86              0
#define TARGET_CPU_X86_64           1
#elif defined(__i386__)
#define TARGET_CPU_X86              1
#define TARGET_CPU_X86_64           0
#else
#error "this TargetConditionals.h only knows x86"
#endif
#define TARGET_CPU_PPC              0
#define TARGET_CPU_PPC64            0
#define TARGET_CPU_68K              0
#define TARGET_CPU_ARM              0
#define TARGET_CPU_ARM64            0
#define TARGET_CPU_MIPS             0
#define TARGET_CPU_SPARC            0
#define TARGET_CPU_ALPHA            0

#define TARGET_RT_MAC_CFM           0
#define TARGET_RT_MAC_MACHO         1
#define TARGET_RT_LITTLE_ENDIAN     1
#define TARGET_RT_BIG_ENDIAN        0
#if defined(__LP64__)
#define TARGET_RT_64_BIT            1
#else
#define TARGET_RT_64_BIT            0
#endif

#endif
