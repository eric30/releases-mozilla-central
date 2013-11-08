/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "linux_seccomp.h"
#include "linux_syscalls.h"

/* This is the actual seccomp whitelist.
 * This is used for B2G content-processes.
 */

/* Architecture-specific frequently used syscalls */
#if defined(__arm__)
#define SECCOMP_WHITELIST_ARCH_HIGH \
  ALLOW_SYSCALL(msgget), \
  ALLOW_SYSCALL(recv), \
  ALLOW_SYSCALL(mmap2),
#elif defined(__i386__)
#define SECCOMP_WHITELIST_ARCH_HIGH \
  ALLOW_SYSCALL(ipc), \
  ALLOW_SYSCALL(mmap2),
#elif defined(__x86_64__)
#define SECCOMP_WHITELIST_ARCH_HIGH \
  ALLOW_SYSCALL(msgget),
#else
#define SECCOMP_WHITELIST_ARCH_HIGH
#endif

/* Architecture-specific infrequently used syscalls */
#if defined(__arm__)
#define SECCOMP_WHITELIST_ARCH_LOW \
  ALLOW_SYSCALL(_llseek), \
  ALLOW_SYSCALL(getuid32), \
  ALLOW_SYSCALL(geteuid32), \
  ALLOW_SYSCALL(sigreturn), \
  ALLOW_SYSCALL(fcntl64),
#elif defined(__i386__)
#define SECCOMP_WHITELIST_ARCH_LOW \
  ALLOW_SYSCALL(_llseek), \
  ALLOW_SYSCALL(getuid32), \
  ALLOW_SYSCALL(geteuid32), \
  ALLOW_SYSCALL(sigreturn), \
  ALLOW_SYSCALL(fcntl64),
#else
#define SECCOMP_WHITELIST_ARCH_LOW
#endif

/* Architecture-specific very infrequently used syscalls */
#if defined(__arm__)
#define SECCOMP_WHITELIST_ARCH_LAST \
  ALLOW_ARM_SYSCALL(breakpoint), \
  ALLOW_ARM_SYSCALL(cacheflush), \
  ALLOW_ARM_SYSCALL(usr26), \
  ALLOW_ARM_SYSCALL(usr32), \
  ALLOW_ARM_SYSCALL(set_tls),
#else
#define SECCOMP_WHITELIST_ARCH_LAST
#endif

/* System calls used by the profiler */
#ifdef MOZ_PROFILING
# ifdef __NR_sigaction
#  define SECCOMP_WHITELIST_PROFILING \
  ALLOW_SYSCALL(sigaction), \
  ALLOW_SYSCALL(rt_sigaction), \
  ALLOW_SYSCALL(tgkill),
# else
#  define SECCOMP_WHITELIST_PROFILING \
  ALLOW_SYSCALL(rt_sigaction), \
  ALLOW_SYSCALL(tgkill),
# endif
#else
# define SECCOMP_WHITELIST_PROFILING
#endif

/* Architecture-specific syscalls that should eventually be removed */
#if defined(__arm__)
#define SECCOMP_WHITELIST_ARCH_TOREMOVE \
  ALLOW_SYSCALL(fstat64), \
  ALLOW_SYSCALL(stat64), \
  ALLOW_SYSCALL(lstat64), \
  ALLOW_SYSCALL(sigprocmask),
#elif defined(__i386__)
#define SECCOMP_WHITELIST_ARCH_TOREMOVE \
  ALLOW_SYSCALL(fstat64), \
  ALLOW_SYSCALL(stat64), \
  ALLOW_SYSCALL(lstat64), \
  ALLOW_SYSCALL(sigprocmask),
#else
#define SECCOMP_WHITELIST_ARCH_TOREMOVE
#endif

/* Most used system calls should be at the top of the whitelist
 * for performance reasons. The whitelist BPF filter exits after
 * processing any ALLOW_SYSCALL macro.
 *
 * How are those syscalls found?
 * 1) via strace -p <child pid> or/and
 * 2) with MOZ_CONTENT_SANDBOX_REPORTER set, the child will report which system call
 *    has been denied by seccomp-bpf, just before exiting, via NSPR.
 * System call number to name mapping is found in:
 * bionic/libc/kernel/arch-arm/asm/unistd.h
 * or your libc's unistd.h/kernel headers.
 *
 * Current list order has been optimized through manual guess-work.
 * It could be further optimized by analyzing the output of:
 * 'strace -c -p <child pid>' for most used web apps.
 */
#define SECCOMP_WHITELIST \
  /* These are calls we're ok to allow */ \
  SECCOMP_WHITELIST_ARCH_HIGH \
  ALLOW_SYSCALL(gettimeofday), \
  ALLOW_SYSCALL(read), \
  ALLOW_SYSCALL(write), \
  ALLOW_SYSCALL(lseek), \
  /* ioctl() is for GL. Remove when GL proxy is implemented.
   * Additionally ioctl() might be a place where we want to have
   * argument filtering */ \
  ALLOW_SYSCALL(ioctl), \
  ALLOW_SYSCALL(close), \
  ALLOW_SYSCALL(munmap), \
  ALLOW_SYSCALL(mprotect), \
  ALLOW_SYSCALL(writev), \
  ALLOW_SYSCALL(clone), \
  ALLOW_SYSCALL(brk), \
  ALLOW_SYSCALL(clock_gettime), \
  ALLOW_SYSCALL(getpid), \
  ALLOW_SYSCALL(gettid), \
  ALLOW_SYSCALL(getrusage), \
  ALLOW_SYSCALL(madvise), \
  ALLOW_SYSCALL(rt_sigreturn), \
  ALLOW_SYSCALL(epoll_wait), \
  ALLOW_SYSCALL(futex), \
  ALLOW_SYSCALL(dup), \
  ALLOW_SYSCALL(nanosleep), \
  SECCOMP_WHITELIST_ARCH_LOW \
  /* Must remove all of the following in the future, when no longer used */ \
  /* open() is for some legacy APIs such as font loading. */ \
  /* See bug 906996 for removing unlink(). */ \
  SECCOMP_WHITELIST_ARCH_TOREMOVE \
  ALLOW_SYSCALL(open), \
  ALLOW_SYSCALL(prctl), \
  ALLOW_SYSCALL(access), \
  ALLOW_SYSCALL(getdents64), \
  ALLOW_SYSCALL(unlink), \
  ALLOW_SYSCALL(fsync), \
  ALLOW_SYSCALL(socketpair), \
  ALLOW_SYSCALL(sendmsg), \
  /* Should remove all of the following in the future, if possible */ \
  ALLOW_SYSCALL(getpriority), \
  ALLOW_SYSCALL(setpriority), \
  ALLOW_SYSCALL(sched_setscheduler), \
  SECCOMP_WHITELIST_PROFILING \
  /* Always last and always OK calls */ \
  SECCOMP_WHITELIST_ARCH_LAST \
  /* restart_syscall is called internally, generally when debugging */ \
  ALLOW_SYSCALL(restart_syscall), \
  ALLOW_SYSCALL(exit_group), \
  ALLOW_SYSCALL(exit)

