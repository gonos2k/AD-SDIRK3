#ifndef WRF_SDIRK3_IMEX_ARK324_COEFFS_H
#define WRF_SDIRK3_IMEX_ARK324_COEFFS_H

#include <array>

namespace wrf {
namespace sdirk3 {

// IMEX-ARK324L2SA (4-stage, 3rd-order) coefficients.
struct ARK324L2SACoefficients final {
    static constexpr int stages = 4;

    // Explicit tableau A^E
    inline static constexpr double a_explicit[stages][stages] = {
        {0.0, 0.0, 0.0, 0.0},
        {1767732205903.0 / 2027836641118.0, 0.0, 0.0, 0.0},
        {5535828885825.0 / 10492691773637.0, 788022342437.0 / 10882634858940.0, 0.0, 0.0},
        {6485989280629.0 / 16251701735622.0, -4246266847089.0 / 9704473918619.0,
         10755448449292.0 / 10357097424841.0, 0.0}};

    // Implicit tableau A^I (ESDIRK form: a_11 = 0)
    inline static constexpr double a_implicit[stages][stages] = {
        {0.0, 0.0, 0.0, 0.0},
        {1767732205903.0 / 4055673282236.0, 1767732205903.0 / 4055673282236.0, 0.0, 0.0},
        {2746238789719.0 / 10658868560708.0, -640167445237.0 / 6845629431997.0,
         1767732205903.0 / 4055673282236.0, 0.0},
        {1471266399579.0 / 7840856788654.0, -4482444167858.0 / 7529755066697.0,
         11266239266428.0 / 11593286722821.0, 1767732205903.0 / 4055673282236.0}};

    // Weights (b^E == b^I for ARK324L2SA)
    inline static constexpr std::array<double, stages> b = {
        1471266399579.0 / 7840856788654.0,
        -4482444167858.0 / 7529755066697.0,
        11266239266428.0 / 11593286722821.0,
        1767732205903.0 / 4055673282236.0};
};

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_ARK324_COEFFS_H
