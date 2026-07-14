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
 */

#ifdef __cplusplus
#define SDIRK3_C_NOEXCEPT noexcept
extern "C" {
#else
#define SDIRK3_C_NOEXCEPT
#endif

int sdirk3_halo_prepare_checked(
    int ids, int ide, int jds, int jde, int kds, int kde,
    int ims, int ime, int jms, int jme, int kms, int kme,
    int ips, int ipe, int jps, int jpe, int kps, int kpe,
    int nprocx, int nprocy, int mypx, int mypy,
    int halo_width) SDIRK3_C_NOEXCEPT;

void sdirk3_halo_finalize(void) SDIRK3_C_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#undef SDIRK3_C_NOEXCEPT

#endif /* WRF_SDIRK3_HALO_C_API_H */
