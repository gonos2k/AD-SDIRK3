#include "wrf_sdirk3_stage2_rejection_snapshot.h"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>

int main() {
    using namespace wrf::sdirk3;
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::string base = "/tmp/sdirk3_stage2_snapshot_test_" +
                             std::to_string(nonce);
    const std::string archive_path = base + "_first.pt";
    const std::string metadata_path = base + "_first.json";
    const std::string terminal_archive_path = base + "_terminal.pt";
    const std::string terminal_metadata_path = base + "_terminal.json";
    const std::string ledger_archive_path = base + "_terminal_rw.pt";
    const std::string ledger_metadata_path = base + "_terminal_rw.json";

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
    snapshot.solver_id = 77;
    snapshot.solve_generation = 3;
    snapshot.rhs_input_digest = 0x1234ULL;
    snapshot.stage = 2;
    snapshot.newton_iter = 6;
    snapshot.trust_attempt = 1;
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

    Stage2RwTermLedger ledger;
    ledger.valid = true;
    ledger.identity.valid = true;
    ledger.identity.timestep = snapshot.timestep;
    ledger.identity.solver_id = snapshot.solver_id;
    ledger.identity.stage = snapshot.stage;
    ledger.identity.newton_iter = snapshot.newton_iter;
    ledger.identity.solve_generation = snapshot.solve_generation;
    ledger.identity.input_digest = snapshot.rhs_input_digest;
    ledger.stage = snapshot.stage;
    ledger.newton_iter = snapshot.newton_iter;
    ledger.solve_generation = snapshot.solve_generation;
    ledger.input_digest = snapshot.rhs_input_digest;
    ledger.rhs_mode = 1;  // ImplicitOnly
    ledger.expect_wdamp = false;
    ledger.w_start = 2;
    ledger.w_size = 2;
    ledger.final_w_packed = snapshot.F.slice(0, 2, 4).clone();
    const char* rw_terms[] = {
        "pg", "buoy_mu1", "buoy_mu2", "rw_pre_pgf", "w_pgf_buoy_all",
        "w_top_contrib", "rw_pre_mask", "rw_post_mask", "rw_tend_final"};
    for (const auto* name : rw_terms) {
        const auto value = std::string(name) == "rw_tend_final"
            ? torch::tensor({16.0f, 40.0f}) : torch::ones({2}, torch::kFloat32);
        ledger.terms.emplace_back(name, value);
    }
    ledger.w_conversion.observed = true;
    ledger.w_conversion.canonical_horizontal = false;
    ledger.w_conversion.coupled_slow_export = false;
    ledger.w_conversion.w_input = torch::tensor({2.0f, 3.0f});
    ledger.w_conversion.velocity_mass_w = torch::tensor({2.0f, 4.0f});
    ledger.w_conversion.mu_tend_at_w = torch::tensor({2.0f, 4.0f});
    // Fallback path evaluates w * mu_tend_at_w / velocity_mass_w in that order.
    ledger.w_conversion.conversion_rate = torch::Tensor();
    ledger.w_conversion.w_tend_after_conversion = torch::tensor({6.0f, 7.0f});
    snapshot.rw_term_ledger = ledger;
    auto canonical_ledger_snapshot = snapshot;
    canonical_ledger_snapshot.rw_term_ledger.w_conversion.canonical_horizontal = true;
    canonical_ledger_snapshot.rw_term_ledger.w_conversion.conversion_rate =
        torch::tensor({1.0f, 2.0f});
    canonical_ledger_snapshot.rw_term_ledger.terms.back().second =
        torch::tensor({16.0f, 52.0f});
    const auto canonical_ledger_error =
        validate_stage2_rw_terminal_ledger(canonical_ledger_snapshot);
    TORCH_CHECK(canonical_ledger_error.empty(),
                "canonical W conversion identity must validate in source operation order: ",
                canonical_ledger_error);

    const auto expected_K = snapshot.K.clone();
    const auto expected_Rtrial = snapshot.R_trial.clone();
    const auto pending = clone_stage2_rejection_snapshot(snapshot);
    TORCH_CHECK(stage2_terminal_snapshot_candidate_matches(pending, 6),
                "matching terminal candidate was rejected");
    TORCH_CHECK(!stage2_terminal_snapshot_candidate_matches(pending, 7),
                "stale pending candidate was accepted for a later stall iteration");
    const auto retained_base_ledger = snapshot.rw_term_ledger;
    auto rejection_attempt0 = snapshot;
    auto rejection_attempt1 = snapshot;
    rejection_attempt0.trust_attempt = 0;
    rejection_attempt1.trust_attempt = 1;
    rejection_attempt0.rw_term_ledger = retained_base_ledger;
    rejection_attempt1.rw_term_ledger = retained_base_ledger;
    TORCH_CHECK(rejection_attempt0.rw_term_ledger.terms.size() ==
                    rejection_attempt1.rw_term_ledger.terms.size() &&
                    validate_stage2_rw_terminal_ledger(rejection_attempt0).empty() &&
                    validate_stage2_rw_terminal_ledger(rejection_attempt1).empty(),
                "consecutive rejected attempts must retain the same base-RHS W ledger");

    std::string error;
    if (!write_stage2_rejection_snapshot(snapshot, archive_path, metadata_path,
                                         &error)) {
        std::cerr << "snapshot write failed: " << error << '\n';
        return 1;
    }

    // Negative aliasing control: mutation after retaining the pending candidate
    // must not rewrite the tensors that a later terminal-stall event publishes.
    snapshot.K.fill_(99.0f);
    snapshot.R_trial.fill_(88.0f);
    const std::string stale_archive_path = base + "_stale.pt";
    const std::string stale_metadata_path = base + "_stale.json";
    error.clear();
    TORCH_CHECK(!write_stage2_terminal_stall_snapshot(
                    pending, 7, 3, stale_archive_path, stale_metadata_path, &error),
                "terminal writer accepted a stale candidate iteration");
    TORCH_CHECK(error.find("does not match terminal Stage-2 iteration") != std::string::npos,
                "stale-candidate rejection reason missing");
    std::ifstream stale_archive(stale_archive_path);
    std::ifstream stale_metadata(stale_metadata_path);
    TORCH_CHECK(!stale_archive.good() && !stale_metadata.good(),
                "stale candidate unexpectedly published a terminal artifact");

    if (!write_stage2_terminal_stall_snapshot(
            pending, 6, 3, ledger_archive_path, ledger_metadata_path, &error, true)) {
        std::cerr << "terminal W-ledger snapshot write failed: " << error << '\n';
        return 1;
    }
    struct stat archive_stat {}, metadata_stat {};
    TORCH_CHECK(::stat(ledger_archive_path.c_str(), &archive_stat) == 0 &&
                    (archive_stat.st_mode & 0777) == 0600,
                "terminal W-ledger archive must be private mode 0600");
    TORCH_CHECK(::stat(ledger_metadata_path.c_str(), &metadata_stat) == 0 &&
                    (metadata_stat.st_mode & 0777) == 0600,
                "terminal W-ledger metadata must be private mode 0600");
    error.clear();
    TORCH_CHECK(!write_stage2_terminal_stall_snapshot(
                    pending, 6, 3, ledger_archive_path, ledger_metadata_path,
                    &error, true),
                "terminal W-ledger writer replaced an existing artifact");
    TORCH_CHECK(error.find("already exists") != std::string::npos,
                "existing W-ledger path refusal reason missing");
    TORCH_CHECK(::stat(ledger_archive_path.c_str(), &archive_stat) == 0,
                "existing W-ledger archive was removed after a refused replacement");

    const auto expect_ledger_refusal = [&](Stage2RejectionSnapshot bad,
                                           const std::string& suffix,
                                           const std::string& reason) {
        const std::string a = base + suffix + ".pt";
        const std::string m = base + suffix + ".json";
        std::string why;
        TORCH_CHECK(!write_stage2_terminal_stall_snapshot(
                        bad, 6, 3, a, m, &why, true),
                    "invalid terminal W-ledger unexpectedly published");
        TORCH_CHECK(why.find(reason) != std::string::npos,
                    "terminal W-ledger refusal reason missing: ", reason);
        TORCH_CHECK(!std::ifstream(a).good() && !std::ifstream(m).good(),
                    "invalid W-ledger capture left a published artifact");
    };
    auto bad_digest = pending;
    bad_digest.rw_term_ledger.identity.input_digest++;
    expect_ledger_refusal(bad_digest, "_bad_digest", "identity_mismatch");
    auto bad_residual = pending;
    bad_residual.R = bad_residual.R.clone();
    bad_residual.R.slice(0, 2, 4).add_(1.0f);
    expect_ledger_refusal(bad_residual, "_bad_residual", "does_not_equal_r_w");
    auto bad_final_w = pending;
    bad_final_w.rw_term_ledger.final_w_packed =
        bad_final_w.rw_term_ledger.final_w_packed + 1.0f;
    expect_ledger_refusal(bad_final_w, "_bad_final_w", "does_not_equal_f_w");
    auto bad_conversion = pending;
    bad_conversion.rw_term_ledger.w_conversion.w_tend_after_conversion =
        bad_conversion.rw_term_ledger.w_conversion.w_tend_after_conversion + 1.0f;
    expect_ledger_refusal(bad_conversion, "_bad_conversion",
                          "rw_to_velocity_conversion");
    auto oversized_ledger = pending;
    oversized_ledger.rw_term_ledger.terms.emplace_back(
        "oversized", torch::zeros({8 * 1024 * 1024}, torch::kFloat32));
    expect_ledger_refusal(oversized_ledger, "_oversized", "32_mib_cap");
    if (!write_stage2_terminal_stall_snapshot(
            pending, 6, 3, terminal_archive_path, terminal_metadata_path, &error)) {
        std::cerr << "terminal snapshot write failed: " << error << '\n';
        std::remove(archive_path.c_str());
        std::remove(metadata_path.c_str());
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
        TORCH_CHECK(torch::equal(K, expected_K), "K archive mismatch");
        TORCH_CHECK(torch::equal(Rtrial, expected_Rtrial), "R_trial archive mismatch");
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
        TORCH_CHECK(json.find("terminal_condition") == std::string::npos,
                    "first-rejection artifact must remain distinct from terminal schema");

        torch::serialize::InputArchive terminal_archive;
        terminal_archive.load_from(terminal_archive_path);
        torch::Tensor terminal_K, terminal_Rtrial;
        terminal_archive.read("K", terminal_K);
        terminal_archive.read("R_trial", terminal_Rtrial);
        TORCH_CHECK(torch::equal(terminal_K, expected_K),
                    "pending clone did not preserve K after source mutation");
        TORCH_CHECK(torch::equal(terminal_Rtrial, expected_Rtrial),
                    "pending clone did not preserve R_trial after source mutation");
        std::ifstream terminal_metadata_file(terminal_metadata_path);
        const std::string terminal_json(
            (std::istreambuf_iterator<char>(terminal_metadata_file)),
            std::istreambuf_iterator<char>());
        TORCH_CHECK(terminal_json.find("\"schema_version\": 3") != std::string::npos,
                    "terminal snapshot schema version missing");
        TORCH_CHECK(terminal_json.find("stage2_terminal_zero_step_stall") != std::string::npos,
                    "terminal snapshot kind missing");
        TORCH_CHECK(terminal_json.find("latest_common_path_rejected_trial") != std::string::npos,
                    "terminal snapshot candidate provenance missing");
        TORCH_CHECK(terminal_json.find("\"candidate_newton_iter\": 6") != std::string::npos,
                    "candidate iteration missing from terminal metadata");
        TORCH_CHECK(terminal_json.find("\"terminal_stall_iter\": 6") != std::string::npos,
                    "terminal iteration missing from terminal metadata");
        TORCH_CHECK(terminal_json.find("\"stagnation_count\": 3") != std::string::npos,
                    "stagnation count missing from terminal metadata");
        TORCH_CHECK(terminal_json.find(
                        "excludes earlier quality-gate and fallback paths") != std::string::npos,
                    "terminal snapshot scope missing exclusions");
        TORCH_CHECK(terminal_json.find("\"rhs_replay_performed\": false") != std::string::npos,
                    "terminal metadata must not claim RHS replay");

        torch::serialize::InputArchive rw_archive;
        rw_archive.load_from(ledger_archive_path);
        torch::Tensor rw_final, packed_w, w_mass, w_converted;
        rw_archive.read("rw_term_rw_tend_final", rw_final);
        rw_archive.read("rw_final_w_packed", packed_w);
        rw_archive.read("w_conversion_velocity_mass", w_mass);
        rw_archive.read("w_tend_after_conversion", w_converted);
        TORCH_CHECK(torch::equal(rw_final, torch::tensor({16.0f, 40.0f})),
                    "coupled rw_tend_final term missing or changed");
        TORCH_CHECK(torch::equal(packed_w, ledger.final_w_packed),
                    "final packed W observation missing or changed");
        TORCH_CHECK(torch::equal(w_mass, torch::tensor({2.0f, 4.0f})),
                    "W conversion velocity mass missing or changed");
        TORCH_CHECK(torch::equal(w_converted, ledger.final_w_packed),
                    "converted W tendency missing or changed");
        std::ifstream ledger_metadata_file(ledger_metadata_path);
        const std::string ledger_json(
            (std::istreambuf_iterator<char>(ledger_metadata_file)),
            std::istreambuf_iterator<char>());
        TORCH_CHECK(ledger_json.find("\"schema_version\": 4") != std::string::npos,
                    "terminal ledger schema version missing");
        TORCH_CHECK(ledger_json.find("fp32_k_minus_f_w_equals_r_w\": true") != std::string::npos,
                    "K-F=R W proof missing from metadata");
        TORCH_CHECK(ledger_json.find("same_call_final_w_equals_f_w\": true") != std::string::npos,
                    "same-call final W proof missing from metadata");
    } catch (const std::exception& e) {
        std::remove(archive_path.c_str());
        std::remove(metadata_path.c_str());
        std::remove(terminal_archive_path.c_str());
        std::remove(terminal_metadata_path.c_str());
        std::remove(ledger_archive_path.c_str());
        std::remove(ledger_metadata_path.c_str());
        std::cerr << "snapshot round-trip failed: " << e.what() << '\n';
        return 1;
    }

    std::remove(archive_path.c_str());
    std::remove(metadata_path.c_str());
    std::remove(terminal_archive_path.c_str());
    std::remove(terminal_metadata_path.c_str());
    std::remove(ledger_archive_path.c_str());
    std::remove(ledger_metadata_path.c_str());
    std::cout << "PASS: terminal W ledger proves FP32 conversion and K-F=R closure\n";
    return 0;
}
