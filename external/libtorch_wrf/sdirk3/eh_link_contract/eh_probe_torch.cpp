// Probe 6: a c10-class exception (what LibTorch throws via TORCH_CHECK)
// must be catchable in the final link. Linked only into the torch variant.
#include <c10/util/Exception.h>
extern "C" int eh_probe_torch_run() {
    try {
        TORCH_CHECK(false, "EH_PROBE_TORCH");
    } catch (const c10::Error& e) {
        return 1;
    } catch (const std::exception& e) {
        return 1;
    }
    return 0;
}
