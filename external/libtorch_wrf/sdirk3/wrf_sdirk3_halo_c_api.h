#ifndef WRF_SDIRK3_HALO_C_API_H
#define WRF_SDIRK3_HALO_C_API_H

/*
 * PR 7B: THE shared C ABI declaration for the checked halo lifecycle.
 * Production definitions and every C++ test include this header — a manual
 * re-declaration (the previous state) can silently diverge in arity or
 * exception specification (noexcept is part of the function type in C++17).
 *
 * sdirk3_halo_prepare_checked: the SOLE geometry authority. Returns 1 on
 * success (an identical re-prepare is a complete no-op), 0 on any
 * validation/lifecycle failure with a stable marker on stderr. No C++
 * exception crosses the ABI.
 *
 * sdirk3_halo_finalize: explicit teardown; ends the communicator
 * configuration lifetime (Policy A) and invalidates the prepare
 * fingerprint. Failures abort with a coordinated stop (void ABI).
 *
 * sdirk3_require_halo_fresh_checked: THE freshness consumption point
 * (PR 7B 3b-3 part 2). Returns 1 when the rank's halo data is fresh for
 * (global_timestep, logical_read_id) — a no-exchange configuration is
 * trivially fresh — and 0 with a stable marker on any failure:
 * SDIRK3_MPI_HALO_NOT_PREPARED (require before rank-level preparation;
 * required=false alone cannot distinguish a prepared serial state from
 * an unprepared one), invalid inputs, off-baseline caller, or
 * SDIRK3_MPI_HALO_STALE. Consume exactly once per SDIRK call
 * (rk_step == 1); re-checking the same logical read is idempotent.
 */

#ifdef __cplusplus
#include <cstdint>
#define SDIRK3_C_NOEXCEPT noexcept
extern "C" {
#else
#include <stdint.h>
#define SDIRK3_C_NOEXCEPT
#endif

int sdirk3_halo_prepare_checked(
    int ids, int ide, int jds, int jde, int kds, int kde,
    int ims, int ime, int jms, int jme, int kms, int kme,
    int ips, int ipe, int jps, int jpe, int kps, int kpe,
    int nprocx, int nprocy, int mypx, int mypy,
    int halo_width) SDIRK3_C_NOEXCEPT;

void sdirk3_halo_finalize(void) SDIRK3_C_NOEXCEPT;

int sdirk3_require_halo_fresh_checked(
    int64_t global_timestep, int logical_read_id) SDIRK3_C_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#undef SDIRK3_C_NOEXCEPT

#endif /* WRF_SDIRK3_HALO_C_API_H */
