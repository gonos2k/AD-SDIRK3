// 9F.D39: the shared diagnostic output lock, verified rather than asserted.
//
// WHY THIS EXISTS. Review section 2 asked for ONE diagnostic mutex across the
// repository. D36 delivered it: wrf_sdirk3_stage_history_diag.h dropped its private
// mutex and now forwards to emit_diag_line(). But that fix was verified by reading
// the code -- no fixture would have failed if a future edit reintroduced a second
// lock, or if someone wrote a record with a bare `std::cerr <<` chain.
//
// That is the same claimed-but-unchecked shape the u-slow closure fixtures exist to
// reject, so the lock gets the same treatment: a test built to FAIL against the
// unlocked implementation.
//
// WHAT LINE-ATOMICITY ACTUALLY BUYS. std::cerr is unbuffered but not atomic. Each
// operator<< is its own write, so two threads emitting a multi-line record interleave
// at CHARACTER granularity:
//     [UTERMS] rhs=3 sites=[UTERMS] rhs=4 ...
// An atomic call counter stops two records claiming the same ordinal; it does nothing
// about this. And the damage is quiet -- interleaved output still looks like output.
// It surfaces only as a parser silently dropping records, which is indistinguishable
// from a probe that never fired: the exact confusion this campaign keeps hitting.
//
// NEGATIVE CONTROL, AND AN HONEST CAVEAT ABOUT IT. Verified by removing the lock from
// emit_diag_block() and re-running: the fixture fails. But it fails by ABORTING
// (SIGABRT), not by reporting interleaved records. The reason is that this test
// redirects std::cerr to an std::ostringstream, and an ostringstream buffer is not
// thread-safe -- unsynchronized concurrent writes are a data race, not merely
// interleaving. In production the real cerr streambuf interleaves instead of
// crashing. So this gate proves the lock is load-bearing via a MORE violent failure
// mode than the one it guards against in the field. That is acceptable for a gate,
// and it is written down here so nobody later reads the abort as an unrelated bug.

#include "../wrf_sdirk3_diag_io.h"
#include "../wrf_sdirk3_stage_history_diag.h"

#include <atomic>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace {

int failures = 0;
int check_count = 0;

void check(bool ok, const char* what) {
    ++check_count;
    std::cout << (ok ? "  ok   " : "  FAIL ") << what << std::endl;
    if (!ok) ++failures;
}

class CerrRedirect {
 public:
    explicit CerrRedirect(std::streambuf* to) : old_(std::cerr.rdbuf(to)) {}
    ~CerrRedirect() { std::cerr.rdbuf(old_); }
    CerrRedirect(const CerrRedirect&) = delete;
    CerrRedirect& operator=(const CerrRedirect&) = delete;
 private:
    std::streambuf* old_;
};

constexpr int kThreads = 8;
constexpr int kRecordsPerThread = 40;

// Each record is deliberately MULTI-LINE and padded. A one-line record would pass
// even under a per-line lock, and a short one can slip through a race window by
// luck -- the point is to give the scheduler somewhere to interleave.
std::string make_record(int thread_id, int seq) {
    std::ostringstream o;
    o << "[REC] begin t=" << thread_id << " seq=" << seq << "\n";
    for (int i = 0; i < 6; ++i) {
        o << "[REC]   payload t=" << thread_id << " seq=" << seq
          << " row=" << i << " ................................\n";
    }
    o << "[REC] end t=" << thread_id << " seq=" << seq << "\n";
    return o.str();
}

// A record survived iff its exact text appears intact in the stream.
int count_intact(const std::string& haystack, const std::string& needle) {
    int n = 0;
    for (std::size_t p = haystack.find(needle); p != std::string::npos;
         p = haystack.find(needle, p + 1)) {
        ++n;
    }
    return n;
}

}  // namespace

int main() {
    // --- emit_diag_block() keeps a multi-line record indivisible ---
    std::string captured;
    {
        std::ostringstream buf;
        {
            CerrRedirect guard(buf.rdbuf());
            std::vector<std::thread> pool;
            for (int t = 0; t < kThreads; ++t) {
                pool.emplace_back([t] {
                    for (int s = 0; s < kRecordsPerThread; ++s) {
                        wrf::sdirk3::emit_diag_block(make_record(t, s));
                    }
                });
            }
            for (auto& th : pool) th.join();
        }
        captured = buf.str();
    }

    int intact = 0;
    for (int t = 0; t < kThreads; ++t) {
        for (int s = 0; s < kRecordsPerThread; ++s) {
            intact += count_intact(captured, make_record(t, s));
        }
    }
    const int expected = kThreads * kRecordsPerThread;
    check(intact == expected, "every multi-line block survives concurrent emission intact");

    // Total byte count must also match: a record could in principle appear intact
    // while a DUPLICATE fragment of another record was also written.
    std::size_t expected_bytes = 0;
    for (int t = 0; t < kThreads; ++t) {
        for (int s = 0; s < kRecordsPerThread; ++s) expected_bytes += make_record(t, s).size();
    }
    check(captured.size() == expected_bytes, "no bytes lost or duplicated under contention");

    // --- the stage-history entry point must share THAT lock, not its own ---
    // This is the section-2 property specifically. If emit_sdirk3_diag_line() ever
    // reacquires a private mutex, lines from the two entry points can interleave
    // with each other even though each is internally consistent -- so the check is
    // that records from BOTH APIs coexist without corruption.
    {
        std::ostringstream buf;
        {
            CerrRedirect guard(buf.rdbuf());
            std::vector<std::thread> pool;
            for (int t = 0; t < kThreads; ++t) {
                pool.emplace_back([t] {
                    for (int s = 0; s < kRecordsPerThread; ++s) {
                        if (t % 2 == 0) {
                            wrf::sdirk3::emit_diag_block(make_record(t, s));
                        } else {
                            wrf::sdirk3::emit_sdirk3_diag_line(make_record(t, s));
                        }
                    }
                });
            }
            for (auto& th : pool) th.join();
        }
        const std::string mixed = buf.str();
        int mixed_intact = 0;
        for (int t = 0; t < kThreads; ++t) {
            for (int s = 0; s < kRecordsPerThread; ++s) {
                mixed_intact += count_intact(mixed, make_record(t, s));
            }
        }
        check(mixed_intact == expected,
              "emit_diag_block and emit_sdirk3_diag_line share ONE lock (section 2)");
    }

    // Case-count ratchet, held in the test rather than in CI YAML -- counters in the
    // workflow have rotted repeatedly in this repo.
    constexpr int expected_checks = 3;
    const bool count_ok = (check_count == expected_checks);
    std::cout << (count_ok ? "  ok   " : "  FAIL ")
              << "case-count ratchet (" << check_count << "/" << expected_checks << ")"
              << std::endl;
    if (!count_ok) ++failures;

    if (failures == 0) {
        std::cout << "DIAG_IO_CONCURRENCY: PASS" << std::endl;
        return 0;
    }
    std::cout << "DIAG_IO_CONCURRENCY: FAIL (" << failures << ")" << std::endl;
    return 1;
}
