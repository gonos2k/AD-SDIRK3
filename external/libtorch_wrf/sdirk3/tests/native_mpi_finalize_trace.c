#include <mpi.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

/* Open MPI's F77 profiling symbol, preserving the original ierr argument. */
extern void pmpi_finalize_(MPI_Fint *ierr);

static void tracef(const char *format, ...) {
  const char *path = getenv("NATIVE_MPI_FINALIZE_TRACE");
  if (path == NULL || path[0] == '\0') return;
  FILE *stream = fopen(path, "a");
  if (stream == NULL) return;
  va_list args;
  va_start(args, format);
  fprintf(stream, "pid=%ld ", (long)getpid());
  vfprintf(stream, format, args);
  fputc('\n', stream);
  va_end(args);
  fflush(stream);
  fclose(stream);
}

static void trace_mpi_state(const char *phase) {
  int initialized = -1;
  int finalized = -1;
  int initialized_rc = PMPI_Initialized(&initialized);
  int finalized_rc = PMPI_Finalized(&finalized);
  tracef("%s initialized_rc=%d initialized=%d finalized_rc=%d finalized=%d",
         phase, initialized_rc, initialized, finalized_rc, finalized);
}

__attribute__((constructor)) static void trace_constructor(void) {
  trace_mpi_state("constructor");
}

__attribute__((destructor)) static void trace_destructor(void) {
  trace_mpi_state("destructor");
}

void mpi_finalize_(MPI_Fint *ierr) {
  trace_mpi_state("enter");
  tracef("forward ierr_ptr=%s", ierr == NULL ? "null" : "present");

  /* Forward exactly once. Do not synthesize ierr or call MPI_Finalize when skipped. */
  pmpi_finalize_(ierr);

  int actual_ierr = ierr == NULL ? -999 : (int)*ierr;
  int initialized = -1;
  int finalized = -1;
  int initialized_rc = PMPI_Initialized(&initialized);
  int finalized_rc = PMPI_Finalized(&finalized);
  tracef("return ierr=%d initialized_rc=%d initialized=%d finalized_rc=%d finalized=%d",
         actual_ierr, initialized_rc, initialized, finalized_rc, finalized);
}
