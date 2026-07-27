// wrf_sdirk3_diag_io.h -- the one line-atomic diagnostic output primitive.
//
// 9F.D33 (review section 5). This existed already, as emit_sdirk3_diag_line() inside
// wrf_sdirk3_stage_history_diag.h. Reusing it from there meant pulling in that entire
// header -- stage-history records, evidence transactions, validators -- just to write
// a line, so a second diagnostic instead re-implemented raw std::cerr output and
// re-created the interleaving problem the original had already solved.
//
// The primitive lives here so it can be shared without dragging its former host
// along. Two functions, one mutex; not a logging framework.
//
// WHY LINE-ATOMICITY MATTERS HERE SPECIFICALLY: an atomic call counter stops two
// records claiming the same ordinal, but it does NOT stop their characters
// interleaving on the way to std::cerr. Under OpenMP tiling a multi-line record can
// come out as
//     [UTERMS] rhs=3 sites=[UTERMS] rhs=4 ...
// which is not merely ugly -- it is unparseable by the analysis scripts, and the
// resulting silent record loss looks exactly like a probe that did not fire.

#ifndef WRF_SDIRK3_DIAG_IO_H
#define WRF_SDIRK3_DIAG_IO_H

#include <iostream>
#include <mutex>
#include <string>

namespace wrf {
namespace sdirk3 {

// The single shared lock. Function-local static inside an inline function, so there
// is exactly ONE instance across every translation unit that includes this header.
inline std::mutex& diag_io_mutex() {
    static std::mutex mtx;
    return mtx;
}

// Write one already-formatted line under the shared lock. The caller composes into a
// local ostringstream first, so stream manipulators (std::scientific,
// std::setprecision) touch only that local stream and never the shared std::cerr
// formatting state.
inline void emit_diag_line(const std::string& line) {
    std::lock_guard<std::mutex> lock(diag_io_mutex());
    std::cerr << line;
}

// Write a multi-line record as ONE indivisible unit. A per-line lock would still let
// two records interleave line-by-line, which for a decomposition record (header, then
// its sites, then its closures) destroys the association between a header and the
// rows that belong to it.
inline void emit_diag_block(const std::string& block) {
    std::lock_guard<std::mutex> lock(diag_io_mutex());
    std::cerr << block;
    std::cerr.flush();
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_DIAG_IO_H
