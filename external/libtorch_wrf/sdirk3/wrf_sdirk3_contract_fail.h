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

}  // namespace sdirk3
}  // namespace wrf
