#pragma once
// PR 9C.2: the production fail route for W-damping contract violations.
//
// MEASURED (2026-07-17, 12-line reproduction): the mpif90(gfortran)-linked
// wrf.exe CANNOT unwind C++ exceptions at all — a try/catch in the SAME
// FUNCTION as the throw still reaches std::terminate (Apple-clang TU +
// mpif90 driver link -> libgcc's unwinder cannot find handlers in
// Apple-clang frames; exit 134), while the identical objects linked by
// clang++ catch normally. Every C++ catch in the production executable —
// including the v2 ABI seal — is therefore unreachable in this link.
//
// Consequence: the production path must NEVER throw. The ABI layer installs
// a [[noreturn]]-behaving handler (abort_c_abi_exception: stable marker to
// stderr, flush, coordinated MPI_Abort, abort) and these routers invoke it
// BEFORE any throw would occur. With no handler installed — the offline
// contract binaries, which are clang++-linked and CAN unwind — they throw,
// keeping the standing contracts' marker/exception semantics unchanged.
#include <cstdlib>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

using ContractFailHandler = void (*)(const char* what);

inline ContractFailHandler& wdamp_contract_fail_handler() {
    static ContractFailHandler h = nullptr;
    return h;
}

[[noreturn]] inline void wdamp_geometry_fail(const std::string& msg) {
    if (auto h = wdamp_contract_fail_handler()) {
        h(msg.c_str());
        std::abort();  // unreachable when the handler is noreturn
    }
    throw std::runtime_error(msg);
}

[[noreturn]] inline void wdamp_input_fail(const std::string& msg) {
    if (auto h = wdamp_contract_fail_handler()) {
        h(msg.c_str());
        std::abort();  // unreachable when the handler is noreturn
    }
    throw std::invalid_argument(msg);
}

// 9F.D130 (review 3.2): a fail-close that NEVER throws, for C ABI boundaries.
//
// wdamp_input_fail/wdamp_geometry_fail throw when no fatal handler is installed, so their ABI
// safety depends on installation ORDER. Config load runs BEFORE the solver installs its
// handler, so the D130 extern "C" wrapper caught a parse error, called wdamp_input_fail, and
// that threw again FROM INSIDE the catch -- std::terminate, and the user got a bare SIGABRT
// with no message at all. Measured: exit=134, nothing on stdout or stderr.
//
// This one takes no such dependency: write the marker straight to the C stderr, flush it, then
// abort. No allocation on the failure path beyond the caller's string, no exception, no handler
// lookup that can change behaviour with link order.
[[noreturn]] inline void abort_c_abi_fail(const std::string& msg) noexcept {
    // Emit FIRST, then delegate. The installed handler is typically noreturn (it aborts or
    // calls wrf_error_fatal), so anything printed after the call never runs -- measured: the
    // first version of this function printed nothing at all on either stream because h(msg)
    // never came back. A fail-close nobody can read is only marginally better than a hang.
    std::fputs("\n[SDIRK3 FATAL] ", stderr);
    std::fputs(msg.c_str(), stderr);
    std::fputs("\n", stderr);
    std::fflush(stderr);
    std::fflush(stdout);
    if (auto h = wdamp_contract_fail_handler()) {
        h(msg.c_str());   // may be noreturn; the marker is already out
    }
    std::abort();
}

}  // namespace sdirk3
}  // namespace wrf
