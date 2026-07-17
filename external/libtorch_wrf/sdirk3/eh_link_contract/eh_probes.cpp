// PR 9C.3 (P0-EH) commit 1: the mixed-toolchain exception-link probes.
//
// Seven behaviors a working C++ exception runtime must provide, each
// callable independently from the Fortran driver (one probe per process:
// under a BROKEN link a throwing probe terminates the process, so the
// runner selects probes by number and judges each run's exit/output).
//
// Scope of the finding these probes pin down (do not over-generalize):
// the CURRENT macOS Apple-clang/libc++ C++ objects + gfortran/mpif90
// final link fails to unwind; a clang++ final link with the Fortran/MPI
// runtimes made explicit restores it.
#include <cstdio>
#include <exception>
#include <stdexcept>

extern int eh_probe_cross_tu_throw();  // defined in eh_probe_tu2.cpp
extern int eh_probe_archive_throw();   // defined in eh_probe_arch.cpp (.a)

namespace {
struct DtorFlag {
    int* flag;
    explicit DtorFlag(int* f) : flag(f) {}
    ~DtorFlag() { *flag = 1; }
};
int throw_through_scope(int* dtor_ran) {
    DtorFlag guard(dtor_ran);
    throw std::runtime_error("EH_PROBE_DTOR");
}
}  // namespace

extern "C" int eh_probe_run(int which) {
    switch (which) {
        case 1: {  // same-TU throw/catch
            try {
                throw std::runtime_error("EH_PROBE_SAME_TU");
            } catch (const std::exception& e) {
                return 1;
            }
            return 0;
        }
        case 2: {  // cross-TU throw/catch
            try {
                return eh_probe_cross_tu_throw();
            } catch (const std::exception& e) {
                return 1;
            }
        }
        case 3: {  // static-archive throw/catch
            try {
                return eh_probe_archive_throw();
            } catch (const std::exception& e) {
                return 1;
            }
        }
        case 4: {  // stack destructor runs during unwind
            int dtor_ran = 0;
            try {
                throw_through_scope(&dtor_ran);
            } catch (const std::exception& e) {
                return dtor_ran == 1 ? 1 : 0;
            }
            return 0;
        }
        case 5: {  // std::exception_ptr capture + rethrow
            std::exception_ptr p;
            try {
                throw std::runtime_error("EH_PROBE_EPTR");
            } catch (...) {
                p = std::current_exception();
            }
            if (!p) return 0;
            try {
                std::rethrow_exception(p);
            } catch (const std::exception& e) {
                return 1;
            }
            return 0;
        }
        case 6:  // c10 / TORCH_CHECK class exception (built separately,
                 // weak-linked here so shapes without torch still link 1-5,7)
            return -1;  // handled by eh_probe_torch in the torch variant
        case 7: {  // catch INSIDE an extern "C" entry (the ABI-seal shape)
            try {
                throw std::runtime_error("EH_PROBE_EXTERN_C");
            } catch (...) {
                return 1;
            }
            return 0;
        }
    }
    return -2;
}
