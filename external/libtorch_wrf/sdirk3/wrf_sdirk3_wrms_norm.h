#ifndef WRF_SDIRK3_WRMS_NORM_H
#define WRF_SDIRK3_WRMS_NORM_H

#include <torch/torch.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace wrf {
namespace sdirk3 {

struct PackedBlockSizes {
    int64_t u = 0;
    int64_t v = 0;
    int64_t w = 0;
    int64_t ph = 0;
    int64_t t = 0;
    int64_t mu = 0;

    int64_t total() const {
        return u + v + w + ph + t + mu;
    }
};

struct WRMSNormConfig {
    float rtol = 1.0e-3f;
    float atol_u = 1.0e-8f;
    float atol_v = 1.0e-8f;
    float atol_w = 1.0e-8f;
    float atol_ph = 1.0e-8f;
    float atol_t = 1.0e-8f;
    float atol_mu = 1.0e-8f;
    float floor = 1.0e-12f;
};

namespace detail {

inline torch::Tensor wrms_block_sum(const torch::Tensor& v,
                                    const torch::Tensor& y_ref,
                                    int64_t offset,
                                    int64_t size,
                                    float atol,
                                    const WRMSNormConfig& cfg) {
    if (size <= 0) {
        return torch::zeros({}, v.options());
    }
    const auto v_block = v.slice(0, offset, offset + size);
    const auto y_block = y_ref.slice(0, offset, offset + size);
    const float rtol = std::max(cfg.rtol, 0.0f);
    const float floor = std::max(cfg.floor, 0.0f);
    const float atol_eff = std::max(atol, 0.0f);
    auto ewt = rtol * torch::abs(y_block) + atol_eff;
    if (floor > 0.0f) {
        ewt = torch::clamp_min(ewt, floor);
    }
    const auto scaled = v_block / ewt;
    return (scaled * scaled).sum();
}

}  // namespace detail

inline torch::Tensor wrms_norm_packed(const torch::Tensor& v,
                                      const torch::Tensor& y_ref,
                                      const PackedBlockSizes& blocks,
                                      const WRMSNormConfig& cfg) {
    if (!v.defined() || !y_ref.defined()) {
        throw std::invalid_argument("wrms_norm_packed: tensors must be defined");
    }
    if (v.numel() != y_ref.numel()) {
        throw std::invalid_argument("wrms_norm_packed: v and y_ref sizes differ");
    }
    if (v.dim() != 1 || y_ref.dim() != 1) {
        throw std::invalid_argument("wrms_norm_packed: expected 1D packed tensors");
    }
    if (blocks.total() != v.numel()) {
        throw std::invalid_argument("wrms_norm_packed: block sizes do not match tensor size");
    }

    int64_t offset = 0;
    auto sum_sq = torch::zeros({}, v.options());
    sum_sq = sum_sq + detail::wrms_block_sum(v, y_ref, offset, blocks.u, cfg.atol_u, cfg);
    offset += blocks.u;
    sum_sq = sum_sq + detail::wrms_block_sum(v, y_ref, offset, blocks.v, cfg.atol_v, cfg);
    offset += blocks.v;
    sum_sq = sum_sq + detail::wrms_block_sum(v, y_ref, offset, blocks.w, cfg.atol_w, cfg);
    offset += blocks.w;
    sum_sq = sum_sq + detail::wrms_block_sum(v, y_ref, offset, blocks.ph, cfg.atol_ph, cfg);
    offset += blocks.ph;
    sum_sq = sum_sq + detail::wrms_block_sum(v, y_ref, offset, blocks.t, cfg.atol_t, cfg);
    offset += blocks.t;
    sum_sq = sum_sq + detail::wrms_block_sum(v, y_ref, offset, blocks.mu, cfg.atol_mu, cfg);

    const auto denom = torch::full({}, static_cast<double>(std::max<int64_t>(v.numel(), 1)), v.options());
    return torch::sqrt(sum_sq / denom);
}

inline torch::Tensor wrms_growth_packed(const torch::Tensor& r_now,
                                        const torch::Tensor& r0,
                                        const torch::Tensor& y_ref,
                                        const PackedBlockSizes& blocks,
                                        const WRMSNormConfig& cfg) {
    const auto now = wrms_norm_packed(r_now, y_ref, blocks, cfg);
    const auto initial = wrms_norm_packed(r0, y_ref, blocks, cfg);
    const auto eps = torch::full({}, static_cast<double>(std::max(cfg.floor, 1.0e-20f)), now.options());
    return now / torch::maximum(initial, eps);
}

}  // namespace sdirk3
}  // namespace wrf

#endif  // WRF_SDIRK3_WRMS_NORM_H
