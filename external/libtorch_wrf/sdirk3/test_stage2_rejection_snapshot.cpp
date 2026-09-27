#include "wrf_sdirk3_stage2_rejection_snapshot.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

int main() {
    using namespace wrf::sdirk3;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string base = "/tmp/sdirk3_stage2_snapshot_test_" +
                             std::to_string(nonce);
    const std::string archive_path = base + ".pt";
    const std::string metadata_path = base + ".json";

    Stage2RejectionSnapshot snapshot;
    snapshot.U_n = torch::arange(4, torch::kFloat32);
    snapshot.U_stage = snapshot.U_n + 1.0f;
    snapshot.K = snapshot.U_n + 2.0f;
    snapshot.U_eval = snapshot.U_n + 3.0f;
    snapshot.F = snapshot.U_n + 4.0f;
    snapshot.R = snapshot.K - snapshot.F;
    snapshot.dK = torch::ones_like(snapshot.K);
    snapshot.dK_trial = 0.25f * snapshot.dK;
    snapshot.K_trial = snapshot.K + snapshot.dK_trial;
    snapshot.U_trial = snapshot.U_stage + snapshot.dK_trial;
    snapshot.F_trial = snapshot.U_n + 5.0f;
    snapshot.R_trial = snapshot.K_trial - snapshot.F_trial;
    snapshot.S_diag = torch::full_like(snapshot.K, 2.0f);
    snapshot.S_inv_diag = torch::full_like(snapshot.K, 0.5f);
    snapshot.gmres_r_true = torch::zeros_like(snapshot.K);
    snapshot.timestep = 1;
    snapshot.stage = 2;
    snapshot.newton_iter = 4;
    snapshot.trust_attempt = 0;
    snapshot.dt = 15.0;
    snapshot.gamma = 0.4358665215;
    snapshot.step_fraction = 0.06;
    snapshot.trust_radius = 1.0e-6;
    snapshot.effective_limit = 1.0e-6;
    snapshot.residual_old_scaled_l2 = 3.0;
    snapshot.residual_trial_scaled_l2 = 4.0;
    snapshot.actual_reduction = -7.0;
    snapshot.predicted_reduction = 2.0;
    snapshot.rho = -3.5;
    snapshot.rho_accept_threshold = 0.25;
    snapshot.gmres_relative_error = 9.0e-8;
    snapshot.rejection_reason = "nonpositive_actual_reduction";

    std::string error;
    if (!write_stage2_rejection_snapshot(snapshot, archive_path, metadata_path,
                                         &error)) {
        std::cerr << "snapshot write failed: " << error << '\n';
        return 1;
    }

    try {
        torch::serialize::InputArchive archive;
        archive.load_from(archive_path);
        torch::Tensor K, Rtrial, dKtrial, Sinv, halo;
        archive.read("K", K);
        archive.read("R_trial", Rtrial);
        archive.read("dK_trial", dKtrial);
        archive.read("S_inv_diag", Sinv);
        archive.read("halo_mask", halo);
        TORCH_CHECK(torch::equal(K, snapshot.K), "K archive mismatch");
        TORCH_CHECK(torch::equal(Rtrial, snapshot.R_trial), "R_trial archive mismatch");
        TORCH_CHECK(torch::equal(dKtrial, snapshot.dK_trial), "tested dK archive mismatch");
        TORCH_CHECK(torch::equal(Sinv, snapshot.S_inv_diag), "S_inv archive mismatch");
        TORCH_CHECK(halo.numel() == 0, "undefined halo mask must serialize as empty");
        std::ifstream metadata(metadata_path);
        const std::string json((std::istreambuf_iterator<char>(metadata)),
                               std::istreambuf_iterator<char>());
        TORCH_CHECK(json.find("stage2_first_common_trust_rejection") != std::string::npos,
                    "snapshot kind missing from metadata");
        TORCH_CHECK(json.find("excludes earlier quality-gate and fallback paths") != std::string::npos,
                    "snapshot scope missing from metadata");
        TORCH_CHECK(json.find("\"schema_version\": 2") != std::string::npos,
                    "snapshot schema version missing from metadata");
        TORCH_CHECK(json.find("observed_fp32_operands_only") != std::string::npos,
                    "precision scope missing from metadata");
        TORCH_CHECK(json.find("\"rhs_replay_performed\": false") != std::string::npos,
                    "metadata must not claim RHS replay");
    } catch (const std::exception& e) {
        std::remove(archive_path.c_str());
        std::remove(metadata_path.c_str());
        std::cerr << "snapshot round-trip failed: " << e.what() << '\n';
        return 1;
    }

    std::remove(archive_path.c_str());
    std::remove(metadata_path.c_str());
    std::cout << "PASS: Stage-2 rejection snapshot round-trip preserves tested operands and scope\n";
    return 0;
}
