// The operator contract is header-only, and until now nothing in the production library compiled
// it -- only the CTest target did, with its own flags. That left two gaps:
//
//   1. The header was never compiled with the model's production flags, so a construct that
//      breaks there would only surface at the next production include.
//   2. compile's staleness gate (compile:529) forbids any tracked sdirk3 source from being newer
//      than libwrf_sdirk3_libtorch.a -- but with no archive object depending on this header,
//      editing it could never make the archive newer. The contract had NO reachable success
//      state: one edit bricked the build until the archive was rebuilt by unrelated means.
//
// Touching the archive to silence the gate would have turned a real stale-artifact detector into
// a rubber stamp. Compiling the header into the library is the honest direction: the object now
// genuinely depends on it, so the archive genuinely goes stale when it changes.
#include "wrf_sdirk3_operator_contract.h"
