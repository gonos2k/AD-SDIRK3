#pragma once

// One-shot, opt-in evidence for the first evaluated rejected Stage-2 Newton
// trial. This records tensors already produced by the solve; it never calls
// the RHS, JVP, or preconditioner.
#include <torch/torch.h>
#include <torch/serialize.h>

#include <cstdio>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace wrf {
namespace sdirk3 {

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

    int64_t timestep = -1;
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

inline std::string snapshot_json_number(double value) {
    if (!std::isfinite(value)) return "null";
    std::ostringstream out;
    out << std::setprecision(17) << value;
    return out.str();
}

// Writes a versioned LibTorch archive plus a JSON sidecar. Temporary files are
// renamed only after each complete write. A failure is reported to the caller;
// it never changes the Newton/trust-region decision.
inline bool write_stage2_rejection_snapshot(const Stage2RejectionSnapshot& s,
                                             const std::string& archive_path,
                                             const std::string& metadata_path,
                                             std::string* error) {
    const std::string archive_tmp = archive_path + ".tmp";
    const std::string metadata_tmp = metadata_path + ".tmp";
    try {
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
        archive.save_to(archive_tmp);

        std::ofstream meta(metadata_tmp.c_str(), std::ios::out | std::ios::trunc);
        if (!meta) throw std::runtime_error("cannot open metadata temporary file");
        meta << "{\n"
             << "  \"schema_version\": 2,\n"
             << "  \"kind\": \"stage2_first_common_trust_rejection\",\n"
             << "  \"capture_scope\": \"after R_trial and trust metrics; excludes earlier quality-gate and fallback paths\",\n"
             << "  \"precision_scope\": \"observed_fp32_operands_only\",\n"
             << "  \"rhs_replay_performed\": false,\n"
             << "  \"rejection_reason\": \"" << s.rejection_reason << "\",\n"
             << "  \"timestep\": " << s.timestep << ",\n"
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
             << "  \"gmres_relative_error\": " << snapshot_json_number(s.gmres_relative_error) << "\n"
             << "}\n";
        meta.close();
        if (!meta) throw std::runtime_error("failed writing metadata temporary file");

        if (std::rename(metadata_tmp.c_str(), metadata_path.c_str()) != 0)
            throw std::runtime_error("cannot publish metadata sidecar");
        // Publish the tensor archive last; its presence is the success marker.
        if (std::rename(archive_tmp.c_str(), archive_path.c_str()) != 0)
            throw std::runtime_error("cannot publish tensor archive");
        return true;
    } catch (const std::exception& e) {
        std::remove(archive_tmp.c_str());
        std::remove(metadata_tmp.c_str());
        if (error) *error = e.what();
        return false;
    }
}

} // namespace sdirk3
} // namespace wrf
