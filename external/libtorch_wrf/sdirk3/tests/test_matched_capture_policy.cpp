#include "../wrf_sdirk3_linear_delay_audit.h"
#include <iostream>

using namespace wrf::sdirk3::linear_delay_audit;

int main() {
    TORCH_CHECK(current == nullptr && a5_capture_allowed(), "no frame must permit capture");
    Frame f;
    {
        IterationScope scope(f);
        f.selected = true; f.base_call = true; f.stage = 4;
        TORCH_CHECK(!a5_capture_allowed(), "selected stage 4 base must remain suppressed");
        f.stage = 2;
        TORCH_CHECK(a5_capture_allowed(), "selected stage 2 base must permit capture");
        f.selected = false; f.stage = 4;
        TORCH_CHECK(a5_capture_allowed(), "unselected base must permit capture");
        f.selected = true; f.base_call = false;
        TORCH_CHECK(a5_capture_allowed(), "non-base call must permit capture");
        f.stage = 2; f.selected = false;
        TORCH_CHECK(a5_capture_allowed(), "unselected non-base call must permit capture");
    }
    TORCH_CHECK(current == nullptr && a5_capture_allowed(), "frame scope must restore null");
    std::cout << "matched capture policy passed\n";
}
