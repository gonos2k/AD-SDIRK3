#ifndef WRF_SDIRK3_IMEX_ARK324_STEPPER_H
#define WRF_SDIRK3_IMEX_ARK324_STEPPER_H

#include <torch/torch.h>

#include <array>
#include <cmath>
#include <memory>

#include "wrf_sdirk3_imex_ark324_coeffs.h"
#include "wrf_sdirk3_imex_jfnk_stage_solver.h"
#include "wrf_sdirk3_imex_split_ops.h"
#include "wrf_sdirk3_imex_strong_acoustic.h"
#include "wrf_sdirk3_vertical_schur_theta_precond.h"

namespace wrf {
namespace sdirk3 {

struct IMEXARK324StageCache {
    std::array<torch::Tensor, ARK324L2SACoefficients::stages> stage_state;
    std::array<torch::Tensor, ARK324L2SACoefficients::stages> rhs;
    std::array<torch::Tensor, ARK324L2SACoefficients::stages> slow_tendency;
    std::array<torch::Tensor, ARK324L2SACoefficients::stages> fast_tendency;
    std::array<IMEXStageContext, ARK324L2SACoefficients::stages> context;
    std::array<AcousticLinearCoeffs, ARK324L2SACoefficients::stages> coeffs;
};

struct IMEXARK324StepperOptions {
    IMEXNewtonKrylovOptions newton_krylov;

    bool apply_filters_each_stage = true;
    bool apply_filters_end_step = true;

    double strong_acoustic_threshold = 0.8;
    double dx_min = 1000.0;  // meters
    double dz_min = 100.0;   // meters
};

class IMEXARK324Stepper {
public:
    IMEXARK324Stepper(std::shared_ptr<WrfIMEXSplitOps> split_ops,
                      std::shared_ptr<VerticalSchurThetaPreconditioner> preconditioner,
                      IMEXARK324StepperOptions options = IMEXARK324StepperOptions())
        : split_ops_(std::move(split_ops)),
          preconditioner_(std::move(preconditioner)),
          options_(std::move(options)) {}

    torch::Tensor step(const torch::Tensor& y_n,
                       double dt,
                       IMEXARK324StageCache* cache = nullptr) {
        torch::NoGradGuard no_grad;

        using Coeff = ARK324L2SACoefficients;
        constexpr int s = Coeff::stages;

        std::array<torch::Tensor, s> stage_state;
        std::array<torch::Tensor, s> rhs;
        std::array<torch::Tensor, s> slow_tendency;
        std::array<torch::Tensor, s> fast_tendency;

        for (int i = 0; i < s; ++i) {
            torch::Tensor rhs_i = y_n;
            for (int j = 0; j < i; ++j) {
                rhs_i = rhs_i + dt * Coeff::a_explicit[i][j] * slow_tendency[j] +
                        dt * Coeff::a_implicit[i][j] * fast_tendency[j];
            }
            rhs[i] = rhs_i;

            IMEXStageContext context = split_ops_->build_stage_context(rhs_i);
            AcousticLinearCoeffs coeffs = split_ops_->build_acoustic_coeffs(context);

            bool strong_acoustic = false;
            if (context.cs2_m_ref.defined() && context.cs2_m_ref.numel() > 0) {
                strong_acoustic = is_strong_acoustic_mode(
                    context.cs2_m_ref,
                    dt,
                    options_.dx_min,
                    options_.dz_min,
                    options_.strong_acoustic_threshold);
            }

            const double aii = Coeff::a_implicit[i][i];
            if (std::abs(aii) < 1e-14) {
                stage_state[i] = rhs_i;
            } else {
                preconditioner_->update_coeffs(coeffs);
                preconditioner_->set_alpha(dt * aii);

                auto fast_op = [&](const torch::Tensor& state) -> torch::Tensor {
                    return split_ops_->compute_fast_tendency(state, context, coeffs);
                };

                stage_state[i] = solve_imex_implicit_stage_jfnk(
                    rhs_i,
                    y_n,
                    dt * aii,
                    strong_acoustic,
                    fast_op,
                    *preconditioner_,
                    options_.newton_krylov);
            }

            if (options_.apply_filters_each_stage) {
                auto filtered = stage_state[i];
                split_ops_->apply_filters_inplace(filtered);
                stage_state[i] = filtered;
            }

            slow_tendency[i] = split_ops_->compute_slow_tendency(stage_state[i]);
            fast_tendency[i] = split_ops_->compute_fast_tendency(stage_state[i], context, coeffs);

            if (cache != nullptr) {
                cache->stage_state[i] = stage_state[i];
                cache->rhs[i] = rhs_i;
                cache->slow_tendency[i] = slow_tendency[i];
                cache->fast_tendency[i] = fast_tendency[i];
                cache->context[i] = std::move(context);
                cache->coeffs[i] = std::move(coeffs);
            }
        }

        torch::Tensor y_np1 = y_n;
        for (int i = 0; i < s; ++i) {
            y_np1 = y_np1 + dt * Coeff::b[i] * (slow_tendency[i] + fast_tendency[i]);
        }

        if (options_.apply_filters_end_step) {
            split_ops_->apply_filters_inplace(y_np1);
        }

        return y_np1;
    }

private:
    std::shared_ptr<WrfIMEXSplitOps> split_ops_;
    std::shared_ptr<VerticalSchurThetaPreconditioner> preconditioner_;
    IMEXARK324StepperOptions options_;
};

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_IMEX_ARK324_STEPPER_H
