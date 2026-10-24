#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif
#include <mpi.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

/* Optional GNU Fortran/Open MPI diagnostic object, not a production dependency.
 * The old PR #316 observer/executable remains pinned in its archived evidence. */
extern void pmpi_init_(MPI_Fint *ierr);
extern void pmpi_finalize_(MPI_Fint *ierr);
_Static_assert(sizeof(int) == 4 && sizeof(MPI_Fint) == 4,
               "diagnostic adapter requires the registered 32-bit Fortran INTEGER ABI");

static void tracef(const char *format, ...) {
  int saved_errno = errno;
  const char *path = getenv("NATIVE_MPI_FINALIZE_TRACE");
  if (path == NULL || path[0] == '\0') { errno = saved_errno; return; }
  FILE *stream = fopen(path, "a");
  if (stream == NULL) { errno = saved_errno; return; }
  va_list args;
  va_start(args, format);
  struct timespec now;
  int64_t monotonic_ns = -1;
  if (clock_gettime(CLOCK_MONOTONIC, &now) == 0)
    monotonic_ns = (int64_t)now.tv_sec * INT64_C(1000000000) + now.tv_nsec;
  fprintf(stream, "pid=%ld pthread_token=%" PRIuPTR " monotonic_ns=%" PRId64 " ",
          (long)getpid(), (uintptr_t)pthread_self(), monotonic_ns);
  vfprintf(stream, format, args);
  fputc('\n', stream);
  va_end(args);
  fflush(stream);
  fclose(stream);
  errno = saved_errno;
}

/* Default INTEGER is 32-bit in the registered GNU Fortran build. */
void native_exit_phase_(const int *phase) {
  static const char *names[] = {
    "unknown", "model_run_return", "io_cleanup_enter", "io_cleanup_return",
    "framework_finalize_enter", "framework_finalize_return",
    "wrf_shutdown_enter", "wrf_shutdown_return", "wrf_finalize_return"
  };
  int code = phase == NULL ? 0 : *phase;
  tracef("phase=%s code=%d", code >= 1 && code <= 8 ? names[code] : names[0], code);
}

__attribute__((constructor)) static void trace_constructor(void) {
  tracef("phase=observer_loaded");
}

__attribute__((destructor)) static void trace_destructor(void) {
  /* An observer destructor is not proof that the process/launcher exited 0. */
  tracef("phase=observer_destructor");
}

void mpi_init_(MPI_Fint *ierr) {
  tracef("phase=mpi_init_enter ierr_ptr=%s", ierr == NULL ? "null" : "present");
  pmpi_init_(ierr);
  tracef("phase=mpi_init_return ierr=%d", ierr == NULL ? -999 : (int)*ierr);
}

void mpi_finalize_(MPI_Fint *ierr) {
  tracef("phase=mpi_finalize_enter ierr_ptr=%s", ierr == NULL ? "null" : "present");
  /* Forward exactly once. Do not synthesize ierr or call MPI_Finalize when skipped. */
  pmpi_finalize_(ierr);
  tracef("phase=mpi_finalize_return ierr=%d", ierr == NULL ? -999 : (int)*ierr);
}
