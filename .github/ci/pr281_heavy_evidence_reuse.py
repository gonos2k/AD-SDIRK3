#!/usr/bin/env python3
"""One-time PR281 evidence assembly for an exact, output-only solver change.

This is not a general numerical cache. Any source/artifact mismatch selects the
ordinary full suite. The candidate still runs every nonheavy CTest and parity.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import zipfile

BASE = "12c12e7501807681ec5072cb8dd2d1bac2717607"
PRODUCER = "8d8f84d92f5be40bf0230bcc05781274d604f857"
TREE = "b0c7531636c60634077ad4dde46b7aebc57dcb56"
SOLVER = "external/libtorch_wrf/sdirk3/wrf_sdirk3_newton_solver.cpp"
SOLVER_SHA = "7e46446b1bbdbc9f86527c786144a5c4b2eadc9bfc08876ec254b0db7d62d205"
ZIP_SHA = "77f0c38ce5a465df6cc65733ed78797f17bcdceef4964e647f63368a4db0549c"
ARTIFACT_ID = 11595799461
HEAVY = {"Native_Physical_Wave_Inverse", "FP64_Fixed_Data_Refinement",
         "Native_Quadratic_Wave_Forcing"}
ALLOWED = {SOLVER, "external/libtorch_wrf/sdirk3/test_trust_model_contract.cpp",
           ".github/workflows/sdirk3-ci.yml", ".github/ci/pr281_heavy_evidence_reuse.py"}


def git(*args):
    return subprocess.check_output(["git", *args], text=True).strip()


def sha(blob):
    return hashlib.sha256(blob).hexdigest()


def source_proof():
    old = subprocess.check_output(["git", "show", f"{BASE}:{SOLVER}"]).decode()
    assert sha(old.encode()) == SOLVER_SHA
    expected = old
    for kind in ("GMRES", "FGMRES"):
        start = old.index(f'        std::cerr << "[{kind}] "')
        end = old.index("<< std::defaultfloat << std::endl;", start)
        end += len("<< std::defaultfloat << std::endl;")
        before = old[start:end]
        after = "        std::ostringstream summary;\n" + before.replace(
            "std::cerr <<", "summary <<", 1)
        after = after.replace("\n                  <<", "\n                <<")
        after = after.replace('\n                << std::defaultfloat << std::endl;',
                              ';\n        std::cerr << summary.str() << std::endl;')
        expected = expected.replace(before, after, 1)
    current = Path(SOLVER).read_text()
    assert current == expected, "solver differs beyond the two exact IO substitutions"
    changed = git("diff", "--name-only", BASE).splitlines()
    for name in changed:
        doc = name.startswith("docs/") and Path(name).suffix in {".md", ".json", ".log"}
        assert name in ALLOWED or doc, f"numerical/test/config source changed: {name}"
    return {"baseline_solver_sha256": SOLVER_SHA,
            "candidate_solver_sha256": sha(current.encode()),
            "proof": "candidate equals baseline with ONLY two exact local-stream substitutions",
            "changed_paths": changed}


def artifact_proof(archive):
    assert sha(archive.read_bytes()) == ZIP_SHA, "baseline ZIP digest mismatch"
    with zipfile.ZipFile(archive) as z:
        assert z.testzip() is None
        def member(suffix):
            names = [n for n in z.namelist() if n.endswith(suffix)]
            assert len(names) == 1, (suffix, names)
            return z.read(names[0]).decode()
        assert member("/source-revisions.txt").splitlines() == [PRODUCER, TREE]
        expected = member("/ctest_expected.txt").splitlines()
        assert expected == member("/ctest_actual.txt").splitlines()
        assert expected == sorted(Path(".github/ci/expected_ctest_names.txt").read_text().splitlines())
        assert len(expected) == 129
        logs = member("/LastTest.log") + "\n" + member("/LastTest.PhysicalInverse.log")
        records = {}
        for block in re.split(r"(?=^\d+/\d+ Testing: )", logs, flags=re.M):
            m = re.match(r"\d+/\d+ Testing: (.+)", block)
            if m:
                name = m.group(1).strip()
                assert name not in records
                records[name] = block
        assert set(records) == set(expected)
        outcomes = {}
        for name, block in records.items():
            terminal = re.findall(r"^Test (Passed|Failed)\.\s*$", block, flags=re.M)
            assert len(terminal) == 1, (name, terminal)
            outcomes[name] = terminal[0]
        failures = {name for name, status in outcomes.items() if status == "Failed"}
        assert failures == {"Trust_Model_Contract"}
        assert {name for name, status in outcomes.items() if status == "Passed"} == set(expected) - failures
        assert member("/LastTestsFailed.log").splitlines() == ["41:Trust_Model_Contract"]
        assert member("/LastTestsDisabled.log").splitlines() == ["91:MSF_Map_Transfer_MPS"]
        assert "MPS_MAP_TRANSFER: SKIP" in records["MSF_Map_Transfer_MPS"]
        for name in HEAVY:
            assert "Test Passed." in records[name] and "Test Failed." not in records[name]
        return {"registered_records": 129, "reused_passes": sorted(HEAVY),
                "baseline_failed_test_not_reused": "Trust_Model_Contract"}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output-dir", type=Path, required=True)
    p.add_argument("--artifact-zip", type=Path)
    a = p.parse_args()
    a.output_dir.mkdir(parents=True, exist_ok=True)
    report = {"eligible": False, "baseline_run": 37873496629,
              "baseline_commit": BASE, "baseline_producer": PRODUCER,
              "baseline_tree": TREE, "baseline_artifact_id": ARTIFACT_ID,
              "baseline_zip_sha256": ZIP_SHA, "candidate_commit": git("rev-parse", "HEAD"),
              "interpretation": "three baseline passes reused; all 126 other tests fresh, not 129 rerun"}
    try:
        report["source_proof"] = source_proof()
        archive = a.artifact_zip or a.output_dir / "pr281-heavy-baseline.zip"
        if a.artifact_zip is None:
            with archive.open("wb") as out:
                subprocess.run(["gh", "api", f"repos/gonos2k/AD-SDIRK3/actions/artifacts/{ARTIFACT_ID}/zip"],
                               stdout=out, check=True)
        report["artifact_proof"] = artifact_proof(archive)
        report["eligible"] = True
    except (AssertionError, OSError, ValueError, KeyError, zipfile.BadZipFile,
            subprocess.CalledProcessError) as error:
        report["fallback_full_suite_reason"] = repr(error)
    (a.output_dir / "pr281-heavy-reuse-receipt.json").write_text(json.dumps(report, indent=2) + "\n")
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as out:
            out.write(f"eligible={str(report['eligible']).lower()}\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
