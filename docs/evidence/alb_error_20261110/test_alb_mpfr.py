import json
import pathlib
import subprocess
import tempfile
import numpy as np

E = pathlib.Path(__file__).resolve().parent
names = [
    "base_ph",
    "base_t",
    "base_mu",
    "base_alt",
    "base_p_pert",
    "accepted_ph",
    "accepted_t",
    "accepted_mu",
    "accepted_alt",
    "accepted_p_pert",
    "mu_base",
    "alb",
    "p_base",
    "c1h",
    "c2h",
    "rdnw",
]
with tempfile.TemporaryDirectory() as td:
    p = pathlib.Path(td)
    d = {
        "theta": [2, 2],
        "base_ph": [0, 0, 0],
        "base_t": [0, 0],
        "base_mu": [0],
        "base_alt": [2, 2],
        "base_p_pert": [3, 3],
        "mu_base": [2],
        "alb": [2, 2],
        "p_base": [1, 1],
        "c1h": [1, 1],
        "c2h": [0, 0],
        "rdnw": [1, 1],
    }
    for n in names:
        src = n.replace("accepted_", "base_") if n.startswith("accepted_") else n
        np.save(p / (n + ".npy"), np.array(d[src], dtype="<f8"))
    np.save(p / "theta.npy", np.array(d["theta"], dtype="<f8"))
    cmd = [
        str(E / "alb_mpfr"),
        "1",
        "2",
        "1",
        "1",
        "1",
        "2",
        "1",
        "4",
        "1",
        str(p / "theta.npy"),
    ] + [str(p / (n + ".npy")) for n in names]

    def run():
        return subprocess.run(cmd, text=True, capture_output=True)

    r = run()
    assert r.returncode == 0, r.stderr
    j = json.loads(r.stdout)
    assert j["upstream_alb"]["linf_upper"] == 0
    for s in j["states"]:
        for k in (
            "induced_alpha",
            "induced_al_pert",
            "induced_p_pert",
            "composed_native_alpha",
            "composed_native_p_pert",
        ):
            assert s[k]["linf_upper"] == 0, (k, s[k])
    # alb*=2, captured=1, mu=1: delta alpha=2/3 and delta al_pert=-1/3.
    np.save(p / "alb.npy", np.array([1.0, 1.0]))
    np.save(p / "base_mu.npy", np.array([1.0]))
    np.save(p / "accepted_mu.npy", np.array([1.0]))
    np.save(p / "base_alt.npy", np.array([2 / 3, 2 / 3]))
    np.save(p / "accepted_alt.npy", np.array([2 / 3, 2 / 3]))
    np.save(p / "base_p_pert.npy", np.array([35.0, 35.0]))
    np.save(p / "accepted_p_pert.npy", np.array([35.0, 35.0]))
    r = run()
    assert r.returncode == 0, r.stderr
    j = json.loads(r.stdout)
    assert j["upstream_alb"]["linf_upper"] >= 1
    for s in j["states"]:
        assert (
            s["induced_alpha"]["linf_upper"] >= 2 / 3
            and s["induced_al_pert"]["linf_upper"] >= 1 / 3
            and s["induced_p_pert"]["linf_upper"] >= 27
        )
        assert s["composed_native_p_pert"]["linf_upper"] >= 27
    np.save(p / "base_ph.npy", np.array([0.0, -100.0, -200.0]))
    r = run()
    assert r.returncode != 0 and "alpha" in r.stderr
    np.save(p / "base_ph.npy", np.array([0.0, 0.0, 0.0]))
    np.save(p / "rdnw.npy", np.array([-1.0, 1.0]))
    r = run()
    assert r.returncode != 0 and "rdnw" in r.stderr
    # Wrapper validates shapes, dtype labels, hashes, and fingerprint-qualified theta.
    import hashlib

    raw = p / "eos_raw"
    raw.mkdir()
    records = []
    for n in names:
        src = n.replace("accepted_", "base_") if n.startswith("accepted_") else n
        v = np.array(d[src], dtype="<f8")
        np.save(raw / (n + ".npy"), v)
        h = 14695981039346656037
        for b in v.tobytes():
            h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
        shape = (
            [1, 3, 1]
            if n.endswith("_ph")
            else [1, 1]
            if n.endswith("_mu") or n == "mu_base"
            else [2]
            if n in ("c1h", "c2h", "rdnw")
            else [1, 2, 1]
        )
        records.append(
            {
                "name": n,
                "path": "eos_raw/" + n + ".npy",
                "shape": shape,
                "source_dtype": 7 if n.startswith(("base_", "accepted_")) else 6,
                "elements": v.size,
                "data_bytes": v.nbytes,
                "fnv64": str(h),
            }
        )
    tb = np.load(p / "theta.npy")
    h = 14695981039346656037
    for b in tb.tobytes():
        h = ((h ^ b) * 1099511628211) & ((1 << 64) - 1)
    receipt = p / "receipt.json"
    receipt.write_text(
        json.dumps(
            {
                "raw_eos": {
                    "scope": "rounded_EOS_subroutine_inputs_and_outputs_only",
                    "files": records,
                },
                "eos_scalars": {"rd": 1, "cv": 1, "cp": 2, "p0": 1, "t0": 4},
                "base_context_hashes": [{"name": "th_base", "fnv64": str(h)}],
            }
        )
    )
    recon = p / "recon.json"
    recon.write_text(
        json.dumps(
            {
                "theta_shape": [1, 2, 1],
                "theta_dtype": "float64",
                "theta_file_sha256": hashlib.sha256(
                    (p / "theta.npy").read_bytes()
                ).hexdigest(),
                "theta_fnv64": str(h),
                "receipt_sha256": hashlib.sha256(receipt.read_bytes()).hexdigest(),
                "pb_bitwise_equal_to_captured": True,
            }
        )
    )
    wrapper = [
        "python3",
        str(E / "evaluate_alb.py"),
        str(receipt),
        str(raw),
        str(p / "theta.npy"),
        str(recon),
        str(p / "analysis.json"),
    ]
    w = subprocess.run(wrapper, text=True, capture_output=True)
    assert w.returncode == 0, w.stderr
    out = json.loads((p / "analysis.json").read_text())
    assert (
        out["analysis"]["cells_per_state"] == 2
        and float.fromhex(out["analysis"]["upstream_alb"]["linf_upper_binary64_hex"])
        == out["analysis"]["upstream_alb"]["linf_upper"]
    )
    (p / "analysis.json").unlink()
    good_reconstruction = json.loads(recon.read_text())
    for key, invalid in (("theta_shape", [2, 1, 1]), ("theta_dtype", "float32")):
        modified = dict(good_reconstruction)
        modified[key] = invalid
        recon.write_text(json.dumps(modified))
        assert subprocess.run(wrapper, text=True, capture_output=True).returncode != 0
        assert not (p / "analysis.json").exists()
    recon.write_text(json.dumps(good_reconstruction))
    records[0]["source_dtype"] = 6
    receipt.write_text(
        json.dumps(
            {
                "raw_eos": {
                    "scope": "rounded_EOS_subroutine_inputs_and_outputs_only",
                    "files": records,
                },
                "eos_scalars": {"rd": 1, "cv": 1, "cp": 2, "p0": 1, "t0": 4},
                "base_context_hashes": [{"name": "th_base", "fnv64": str(h)}],
            }
        )
    )
    assert subprocess.run(wrapper, text=True, capture_output=True).returncode != 0
    print(
        "PASS exact identity, wrong alb propagation and al_pert factor, domain crossing, rdnw sign, receipt metadata wrapper"
    )
