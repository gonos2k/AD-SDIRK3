#pragma once

// One-shot, opt-in evidence for the first evaluated rejected Stage-2 Newton
// trial. This records tensors already produced by the solve; it never calls
// the RHS, JVP, or preconditioner.
#include <torch/torch.h>
#include <torch/serialize.h>
#include "wrf_sdirk3_rw_term_capture.h"

#include <cstdio>
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace wrf {
namespace sdirk3 {

struct Stage2RwTermLedger {
    bool valid = false;
    RwTermCaptureIdentity identity;
    int stage = -1;
    int newton_iter = -1;
    std::uint64_t solve_generation = 0;
    std::uint64_t input_digest = 0;
    int rhs_mode = -1;
    bool expect_wdamp = false;
    int64_t w_start = -1;
    int64_t w_size = 0;
    std::string failure;
    std::vector<std::pair<std::string, torch::Tensor>> terms;
    torch::Tensor final_w_packed;
    RwWConversionCapture w_conversion;
};

struct Stage2RejectionSnapshot {
    torch::Tensor U_n;
    torch::Tensor U_stage;
    torch::Tensor K;
    torch::Tensor U_eval;
    torch::Tensor F;
    torch::Tensor R;
    torch::Tensor dK;
    torch::Tensor dK_trial;
    torch::Tensor K_trial;
    torch::Tensor U_trial;
    torch::Tensor F_trial;
    torch::Tensor R_trial;
    torch::Tensor S_diag;
    torch::Tensor S_inv_diag;
    torch::Tensor halo_mask;
    torch::Tensor gmres_r_true;
    Stage2RwTermLedger rw_term_ledger;

    int64_t timestep = -1;
    std::uint64_t solver_id = 0;
    std::uint64_t solve_generation = 0;
    std::uint64_t rhs_input_digest = 0;
    int stage = -1;
    int newton_iter = -1;
    int trust_attempt = -1;
    double dt = 0.0;
    double gamma = 0.0;
    double step_fraction = 0.0;
    double trust_radius = 0.0;
    double effective_limit = 0.0;
    double residual_old_scaled_l2 = 0.0;
    double residual_trial_scaled_l2 = 0.0;
    double actual_reduction = 0.0;
    double predicted_reduction = 0.0;
    double rho = 0.0;
    double rho_accept_threshold = 0.0;
    double gmres_relative_error = 0.0;
    std::string rejection_reason;
};

inline torch::Tensor snapshot_cpu_tensor(const torch::Tensor& tensor) {
    if (!tensor.defined()) {
        return torch::empty({0}, torch::TensorOptions().dtype(torch::kFloat32)
                                      .device(torch::kCPU));
    }
    return tensor.detach().to(torch::kCPU).contiguous().clone();
}

inline Stage2RwTermLedger clone_stage2_rw_term_ledger(
    const Stage2RwTermLedger& source) {
    Stage2RwTermLedger frozen = source;
    const auto clone_detached = [](const torch::Tensor& tensor) {
        return tensor.defined() ? tensor.detach().clone() : torch::Tensor();
    };
    for (auto& term : frozen.terms) term.second = clone_detached(term.second);
    frozen.final_w_packed = clone_detached(source.final_w_packed);
    frozen.w_conversion.w_input = clone_detached(source.w_conversion.w_input);
    frozen.w_conversion.velocity_mass_w = clone_detached(source.w_conversion.velocity_mass_w);
    frozen.w_conversion.mu_tend_at_w = clone_detached(source.w_conversion.mu_tend_at_w);
    frozen.w_conversion.conversion_rate = clone_detached(source.w_conversion.conversion_rate);
    frozen.w_conversion.w_tend_after_conversion =
        clone_detached(source.w_conversion.w_tend_after_conversion);
    return frozen;
}

inline Stage2RejectionSnapshot clone_stage2_rejection_snapshot(
    const Stage2RejectionSnapshot& source, bool clone_rw_ledger = true) {
    Stage2RejectionSnapshot frozen = source;
    const auto clone_detached = [](const torch::Tensor& tensor) {
        return tensor.defined() ? tensor.detach().clone() : torch::Tensor();
    };
    frozen.U_n = clone_detached(source.U_n);
    frozen.U_stage = clone_detached(source.U_stage);
    frozen.K = clone_detached(source.K);
    frozen.U_eval = clone_detached(source.U_eval);
    frozen.F = clone_detached(source.F);
    frozen.R = clone_detached(source.R);
    frozen.dK = clone_detached(source.dK);
    frozen.dK_trial = clone_detached(source.dK_trial);
    frozen.K_trial = clone_detached(source.K_trial);
    frozen.U_trial = clone_detached(source.U_trial);
    frozen.F_trial = clone_detached(source.F_trial);
    frozen.R_trial = clone_detached(source.R_trial);
    frozen.S_diag = clone_detached(source.S_diag);
    frozen.S_inv_diag = clone_detached(source.S_inv_diag);
    frozen.halo_mask = clone_detached(source.halo_mask);
    frozen.gmres_r_true = clone_detached(source.gmres_r_true);
    frozen.rw_term_ledger = clone_rw_ledger
        ? clone_stage2_rw_term_ledger(source.rw_term_ledger)
        : Stage2RwTermLedger{};
    return frozen;
}

inline std::uint64_t stage2_rw_term_ledger_nbytes(
    const Stage2RwTermLedger& ledger) {
    std::uint64_t bytes = 0;
    const auto add = [&](const torch::Tensor& tensor, std::uint64_t& total) {
        if (!tensor.defined()) return true;
        const auto count = static_cast<std::uint64_t>(tensor.numel());
        const auto width = static_cast<std::uint64_t>(tensor.element_size());
        if (width != 0 && count >
            (std::numeric_limits<std::uint64_t>::max() - total) / width) return false;
        total += count * width;
        return true;
    };
    for (const auto& term : ledger.terms)
        if (!add(term.second, bytes)) return std::numeric_limits<std::uint64_t>::max();
    if (!add(ledger.final_w_packed, bytes)) return std::numeric_limits<std::uint64_t>::max();
    for (const auto* tensor : {&ledger.w_conversion.w_input,
                               &ledger.w_conversion.velocity_mass_w,
                               &ledger.w_conversion.mu_tend_at_w,
                               &ledger.w_conversion.conversion_rate,
                               &ledger.w_conversion.w_tend_after_conversion})
        if (!add(*tensor, bytes)) return std::numeric_limits<std::uint64_t>::max();
    return bytes;
}

inline std::string validate_stage2_rw_terminal_ledger(
    const Stage2RejectionSnapshot& snapshot) {
    const auto& ledger = snapshot.rw_term_ledger;
    if (!ledger.failure.empty()) return ledger.failure;
    if (!ledger.valid || !ledger.identity.valid)
        return "missing_rw_term_capture";
    if (ledger.identity.timestep != snapshot.timestep ||
        ledger.identity.solver_id != snapshot.solver_id ||
        ledger.identity.stage != snapshot.stage ||
        ledger.identity.newton_iter != snapshot.newton_iter ||
        ledger.identity.solve_generation != snapshot.solve_generation ||
        ledger.identity.input_digest != snapshot.rhs_input_digest)
        return "rw_term_capture_identity_mismatch";
    if (ledger.stage != snapshot.stage || ledger.newton_iter != snapshot.newton_iter ||
        ledger.solve_generation != snapshot.solve_generation ||
        ledger.input_digest != snapshot.rhs_input_digest)
        return "rw_term_capture_candidate_mismatch";
    if (ledger.rhs_mode != 1)
        return "unsupported_rw_rhs_mode";
    constexpr std::uint64_t max_bytes = 32ULL * 1024ULL * 1024ULL;
    if (stage2_rw_term_ledger_nbytes(ledger) > max_bytes)
        return "rw_term_ledger_exceeds_32_mib_cap";
    const auto inventory_error = validate_rw_term_inventory(
        ledger.terms, ledger.expect_wdamp);
    if (!inventory_error.empty()) return "rw_term_inventory:" + inventory_error;
    if (!ledger.final_w_packed.defined() || ledger.final_w_packed.dim() != 1 ||
        ledger.w_start < 0 || ledger.w_size <= 0 ||
        ledger.final_w_packed.numel() != ledger.w_size)
        return "missing_final_packed_w_observation";
    const auto& conversion = ledger.w_conversion;
    if (!conversion.observed || conversion.coupled_slow_export ||
        !conversion.w_input.defined() || !conversion.velocity_mass_w.defined() ||
        !conversion.mu_tend_at_w.defined() ||
        (conversion.canonical_horizontal && !conversion.conversion_rate.defined()) ||
        !conversion.w_tend_after_conversion.defined())
        return "missing_or_unsupported_w_conversion_bridge";
    for (const auto* tensor : {&conversion.w_input, &conversion.velocity_mass_w,
                               &conversion.mu_tend_at_w,
                               &conversion.w_tend_after_conversion}) {
        if (tensor->scalar_type() != torch::kFloat32 || tensor->numel() != ledger.w_size)
            return "unsupported_w_conversion_operand_layout";
    }
    if (conversion.canonical_horizontal &&
        (conversion.conversion_rate.scalar_type() != torch::kFloat32 ||
         conversion.conversion_rate.numel() != ledger.w_size))
        return "unsupported_canonical_w_conversion_rate_layout";
    if (!snapshot.K.defined() || !snapshot.F.defined() || !snapshot.R.defined() ||
        snapshot.K.dim() != 1 || snapshot.F.sizes() != snapshot.K.sizes() ||
        snapshot.R.sizes() != snapshot.K.sizes() ||
        snapshot.K.scalar_type() != torch::kFloat32 ||
        snapshot.F.scalar_type() != torch::kFloat32 ||
        snapshot.R.scalar_type() != torch::kFloat32 ||
        ledger.final_w_packed.scalar_type() != torch::kFloat32 ||
        ledger.w_start + ledger.w_size > snapshot.K.numel())
        return "unsupported_fp32_w_block_layout";
    torch::NoGradGuard no_grad;
    const auto K_w = snapshot.K.slice(0, ledger.w_start, ledger.w_start + ledger.w_size);
    const auto F_w = snapshot.F.slice(0, ledger.w_start, ledger.w_start + ledger.w_size);
    const auto R_w = snapshot.R.slice(0, ledger.w_start, ledger.w_start + ledger.w_size);
    const auto rw_final = std::find_if(
        ledger.terms.begin(), ledger.terms.end(), [](const auto& term) {
            return term.first == "rw_tend_final";
        });
    if (rw_final == ledger.terms.end()) return "missing_rw_tend_final";
    const auto converted_w = conversion.canonical_horizontal
        ? rw_final->second / conversion.velocity_mass_w -
            conversion.w_input * conversion.conversion_rate
        : rw_final->second / conversion.velocity_mass_w -
            conversion.w_input * conversion.mu_tend_at_w / conversion.velocity_mass_w;
    if (!torch::equal(converted_w, conversion.w_tend_after_conversion))
        return "fp32_rw_to_velocity_conversion_does_not_match_w_tend";
    if (!torch::equal(K_w - F_w, R_w)) return "fp32_k_minus_f_w_does_not_equal_r_w";
    if (!torch::equal(ledger.final_w_packed, F_w))
        return "same_call_final_packed_w_does_not_equal_f_w";
    return {};
}

inline bool stage2_terminal_snapshot_candidate_matches(
    const Stage2RejectionSnapshot& snapshot, int terminal_stall_iter,
    std::uint64_t terminal_generation = 0) {
    return snapshot.stage == 2 && snapshot.newton_iter == terminal_stall_iter &&
        (terminal_generation == 0 ||
         snapshot.solve_generation == terminal_generation);
}

inline std::string snapshot_json_number(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

inline void create_private_snapshot_temp(const std::string& path) {
    const int fd = ::open(path.c_str(), O_CREAT | O_EXCL | O_WRONLY,
                          S_IRUSR | S_IWUSR);
    if (fd < 0)
        throw std::runtime_error("cannot create private snapshot temporary file");
    if (::close(fd) != 0) {
        std::remove(path.c_str());
        throw std::runtime_error("cannot close snapshot temporary file");
    }
}

inline bool stage2_snapshot_path_absent(const std::string& path) {
    struct stat existing {};
    if (::lstat(path.c_str(), &existing) == 0) return false;
    return errno == ENOENT;
}

inline void publish_snapshot_file(const std::string& temporary,
                                  const std::string& destination,
                                  bool no_replace, bool* published) {
    if (!no_replace) {
        if (std::rename(temporary.c_str(), destination.c_str()) != 0)
            throw std::runtime_error("cannot publish snapshot file");
        if (published) *published = true;
        return;
    }
    // Same-directory hard-link publication is atomic and refuses to replace an
    // existing receipt, closing the lstat/rename race for the W-ledger artifact.
    if (::link(temporary.c_str(), destination.c_str()) != 0)
        throw std::runtime_error("cannot publish W-ledger file without replacement");
    if (published) *published = true;
    if (::unlink(temporary.c_str()) != 0)
        throw std::runtime_error("cannot remove linked W-ledger temporary file");
}

// Writes a versioned LibTorch archive plus a JSON sidecar. Temporary files are
// renamed only after each complete write. A failure is reported to the caller;
// it never changes the Newton/trust-region decision.
inline bool write_stage2_snapshot(const Stage2RejectionSnapshot& s,
                                  const std::string& archive_path,
                                  const std::string& metadata_path,
                                  const char* kind,
                                  int schema_version,
                                  int terminal_stall_iter,
                                  int stagnation_count,
                                  std::string* error,
                                  bool include_rw_ledger = false) {
    const std::string archive_tmp = archive_path + ".tmp";
    const std::string metadata_tmp = metadata_path + ".tmp";
    bool archive_tmp_created = false;
    bool metadata_tmp_created = false;
    bool archive_published = false;
    bool metadata_published = false;
    try {
        if (include_rw_ledger) {
            if (!stage2_snapshot_path_absent(archive_path) ||
                !stage2_snapshot_path_absent(metadata_path))
                throw std::runtime_error("terminal W-ledger output path already exists or is unavailable");
            const auto why = validate_stage2_rw_terminal_ledger(s);
            if (!why.empty()) throw std::runtime_error(why);
        }
        if (include_rw_ledger) {
            create_private_snapshot_temp(archive_tmp);
            archive_tmp_created = true;
            create_private_snapshot_temp(metadata_tmp);
            metadata_tmp_created = true;
        } else {
            // Preserve the original non-ledger snapshot temp-file behavior.
            archive_tmp_created = true;
            metadata_tmp_created = true;
        }
        torch::serialize::OutputArchive archive;
        archive.write("U_n", snapshot_cpu_tensor(s.U_n));
        archive.write("U_stage", snapshot_cpu_tensor(s.U_stage));
        archive.write("K", snapshot_cpu_tensor(s.K));
        archive.write("U_eval", snapshot_cpu_tensor(s.U_eval));
        archive.write("F", snapshot_cpu_tensor(s.F));
        archive.write("R", snapshot_cpu_tensor(s.R));
        archive.write("dK", snapshot_cpu_tensor(s.dK));
        archive.write("dK_trial", snapshot_cpu_tensor(s.dK_trial));
        archive.write("K_trial", snapshot_cpu_tensor(s.K_trial));
        archive.write("U_trial", snapshot_cpu_tensor(s.U_trial));
        archive.write("F_trial", snapshot_cpu_tensor(s.F_trial));
        archive.write("R_trial", snapshot_cpu_tensor(s.R_trial));
        archive.write("S_diag", snapshot_cpu_tensor(s.S_diag));
        archive.write("S_inv_diag", snapshot_cpu_tensor(s.S_inv_diag));
        archive.write("halo_mask", snapshot_cpu_tensor(s.halo_mask));
        archive.write("gmres_r_true", snapshot_cpu_tensor(s.gmres_r_true));
        if (include_rw_ledger) {
            archive.write("rw_final_w_packed",
                          snapshot_cpu_tensor(s.rw_term_ledger.final_w_packed));
            archive.write("w_conversion_input",
                          snapshot_cpu_tensor(s.rw_term_ledger.w_conversion.w_input));
            archive.write("w_conversion_velocity_mass",
                          snapshot_cpu_tensor(s.rw_term_ledger.w_conversion.velocity_mass_w));
            archive.write("w_conversion_mu_tend_at_w",
                          snapshot_cpu_tensor(s.rw_term_ledger.w_conversion.mu_tend_at_w));
            archive.write("w_conversion_rate",
                          snapshot_cpu_tensor(s.rw_term_ledger.w_conversion.conversion_rate));
            archive.write("w_tend_after_conversion",
                          snapshot_cpu_tensor(s.rw_term_ledger.w_conversion.w_tend_after_conversion));
            for (const auto& term : s.rw_term_ledger.terms)
                archive.write("rw_term_" + term.first, snapshot_cpu_tensor(term.second));
        }
        archive.save_to(archive_tmp);

        std::ofstream meta(metadata_tmp.c_str(), std::ios::out | std::ios::trunc);
        if (!meta) throw std::runtime_error("cannot open metadata temporary file");
        const bool terminal_stall = terminal_stall_iter >= 0;
        meta << "{\n"
             << "  \"schema_version\": " << schema_version << ",\n"
             << "  \"kind\": \"" << kind << "\",\n"
             << "  \"capture_scope\": \""
             << (terminal_stall
                    ? "latest evaluated common-path rejected trial; excludes earlier quality-gate and fallback paths"
                    : "after R_trial and trust metrics; excludes earlier quality-gate and fallback paths")
             << "\",\n"
             << "  \"precision_scope\": \"observed_fp32_operands_only\",\n"
             << "  \"rhs_replay_performed\": false,\n"
             << "  \"rejection_reason\": \"" << s.rejection_reason << "\",\n"
             << "  \"timestep\": " << s.timestep << ",\n"
             << "  \"solver_id\": " << s.solver_id << ",\n"
             << "  \"solve_generation\": " << s.solve_generation << ",\n"
             << "  \"rhs_input_digest\": \"0x" << std::hex << s.rhs_input_digest
             << std::dec << "\",\n"
             << "  \"stage\": " << s.stage << ",\n"
             << "  \"newton_iter\": " << s.newton_iter << ",\n"
             << "  \"trust_attempt\": " << s.trust_attempt << ",\n"
             << "  \"dt\": " << snapshot_json_number(s.dt) << ",\n"
             << "  \"gamma\": " << snapshot_json_number(s.gamma) << ",\n"
             << "  \"step_fraction\": " << snapshot_json_number(s.step_fraction) << ",\n"
             << "  \"trust_radius\": " << snapshot_json_number(s.trust_radius) << ",\n"
             << "  \"effective_limit\": " << snapshot_json_number(s.effective_limit) << ",\n"
             << "  \"residual_old_scaled_l2\": " << snapshot_json_number(s.residual_old_scaled_l2) << ",\n"
             << "  \"residual_trial_scaled_l2\": " << snapshot_json_number(s.residual_trial_scaled_l2) << ",\n"
             << "  \"actual_reduction\": " << snapshot_json_number(s.actual_reduction) << ",\n"
             << "  \"predicted_reduction\": " << snapshot_json_number(s.predicted_reduction) << ",\n"
             << "  \"rho\": " << snapshot_json_number(s.rho) << ",\n"
             << "  \"rho_accept_threshold\": " << snapshot_json_number(s.rho_accept_threshold) << ",\n"
             << "  \"gmres_relative_error\": " << snapshot_json_number(s.gmres_relative_error);
        if (terminal_stall_iter >= 0) {
            meta << ",\n"
                 << "  \"candidate_kind\": \"latest_common_path_rejected_trial\",\n"
                 << "  \"candidate_newton_iter\": " << s.newton_iter << ",\n"
                 << "  \"candidate_trust_attempt\": " << s.trust_attempt << ",\n"
                 << "  \"terminal_condition\": \"ZeroStepStall\",\n"
                 << "  \"terminal_stall_iter\": " << terminal_stall_iter << ",\n"
                 << "  \"stagnation_count\": " << stagnation_count;
            if (include_rw_ledger) {
                meta << ",\n  \"rw_term_ledger\": {\n"
                     << "    \"rhs_mode\": " << s.rw_term_ledger.rhs_mode << ",\n"
                     << "    \"w_block_start\": " << s.rw_term_ledger.w_start << ",\n"
                     << "    \"w_block_size\": " << s.rw_term_ledger.w_size << ",\n"
                     << "    \"rhs_input_digest\": \"0x" << std::hex
                     << s.rw_term_ledger.identity.input_digest << std::dec << "\",\n"
                     << "    \"canonical_horizontal\": "
                     << (s.rw_term_ledger.w_conversion.canonical_horizontal ? "true" : "false") << ",\n"
                     << "    \"coupled_slow_export\": "
                     << (s.rw_term_ledger.w_conversion.coupled_slow_export ? "true" : "false") << ",\n"
                     << "    \"conversion_bridge\": [\"w_input\", \"velocity_mass_w\", "
                     << "\"mu_tend_at_w\", \"conversion_rate\", "
                     << "\"w_tend_after_conversion\"],\n"
                     << "    \"fp32_rw_mass_to_velocity_conversion_matches\": true,\n"
                     << "    \"fp32_k_minus_f_w_equals_r_w\": true,\n"
                     << "    \"same_call_final_w_equals_f_w\": true,\n"
                     << "    \"bytes\": " << stage2_rw_term_ledger_nbytes(s.rw_term_ledger)
                     << ",\n    \"terms\": [";
                for (size_t i = 0; i < s.rw_term_ledger.terms.size(); ++i) {
                    if (i) meta << ", ";
                    meta << "\"" << s.rw_term_ledger.terms[i].first << "\"";
                }
                meta << "]\n  }";
            }
        }
        meta << "\n}\n";
        meta.close();
        if (!meta) throw std::runtime_error("failed writing metadata temporary file");

        publish_snapshot_file(metadata_tmp, metadata_path, include_rw_ledger,
                              &metadata_published);
        // The archive remains the existing success marker and is published last.
        publish_snapshot_file(archive_tmp, archive_path, include_rw_ledger,
                              &archive_published);
        return true;
    } catch (const std::exception& e) {
        if (archive_tmp_created) std::remove(archive_tmp.c_str());
        if (metadata_tmp_created) std::remove(metadata_tmp.c_str());
        if (archive_published) std::remove(archive_path.c_str());
        if (metadata_published) std::remove(metadata_path.c_str());
        if (error) *error = e.what();
        return false;
    }
}

inline bool write_stage2_rejection_snapshot(const Stage2RejectionSnapshot& s,
                                             const std::string& archive_path,
                                             const std::string& metadata_path,
                                             std::string* error) {
    return write_stage2_snapshot(s, archive_path, metadata_path,
                                 "stage2_first_common_trust_rejection", 2, -1, -1,
                                 error);
}

inline bool write_stage2_terminal_stall_snapshot(const Stage2RejectionSnapshot& s,
                                                  int terminal_stall_iter,
                                                  int stagnation_count,
                                                  const std::string& archive_path,
                                                  const std::string& metadata_path,
                                                  std::string* error,
                                                  bool require_rw_ledger = false) {
    if (!stage2_terminal_snapshot_candidate_matches(s, terminal_stall_iter)) {
        if (error) *error = "latest common-path candidate does not match terminal Stage-2 iteration";
        return false;
    }
    if (require_rw_ledger) {
        const auto ledger_error = validate_stage2_rw_terminal_ledger(s);
        if (!ledger_error.empty()) {
            if (error) *error = ledger_error;
            return false;
        }
    }
    return write_stage2_snapshot(s, archive_path, metadata_path,
                                 "stage2_terminal_zero_step_stall",
                                 require_rw_ledger ? 4 : 3,
                                 terminal_stall_iter, stagnation_count, error,
                                 require_rw_ledger);
}

} // namespace sdirk3
} // namespace wrf
