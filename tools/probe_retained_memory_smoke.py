#!/usr/bin/env python3
"""Build and capture a three-step bounded-tape Valgrind memory smoke."""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import shlex
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "external/libtorch_wrf/sdirk3/tests/test_native_wave_refinement.cpp"
FIXTURE_SHA = "e170be5d1e2b0d87efcdc9ebe0f4dd1d381aceb9e0777dcd074e0dcfe1329d8c"
SOURCE_SHA = "a058e570e1527b5167c0f999188ecc3f520c48a6421cf18c8f2c7098320b9967"
TARGET = "test_native_wave_refinement"
COMPILE_DATABASE = "compile_commands.json"
MARKER = "MEMORY_ONLY completed_steps=3"
PATCH_NEEDLE = """                g.solver.closeFixedTrajectory();
                if(n==physical_wave_steps/2-1) checkpoint_150=handoff_state.detach().clone();"""
PATCH_INSERT = """                g.solver.closeFixedTrajectory();
                if(n==2) {
                    std::cout << \"MEMORY_ONLY completed_steps=3\\n\" << std::flush;
                    out.close();
                    std::remove(argv[4]);
                    return 0;
                }
                if(n==physical_wave_steps/2-1) checkpoint_150=handoff_state.detach().clone();"""


def file_sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write_report(path, report):
    path.write_text(json.dumps(report, indent=2, allow_nan=False) + "\n")


def append_step(report, target, step):
    report.setdefault("steps", []).append(step)
    write_report(target, report)


def parse_command(text, directory):
    tokens = shlex.split(text, posix=True)
    if tokens[:2] == [":", "&&"]:
        tokens = tokens[2:]
    if tokens[-2:] == ["&&", ":"]:
        tokens = tokens[:-2]
    cwd = Path(directory)
    if len(tokens) >= 4 and tokens[0] == "cd" and tokens[2] == "&&":
        requested = Path(tokens[1])
        cwd = (requested if requested.is_absolute() else cwd / requested).resolve()
        tokens = tokens[3:]
    forbidden = {"&&", "||", ";", "|", ">", "<"}
    if not tokens or any(token in forbidden for token in tokens):
        raise ValueError("compile/link command requires unsupported shell syntax")
    if any(token.startswith("@") for token in tokens):
        raise ValueError("response-file commands are unsupported; refusing incomplete path rewrite")
    return tokens, cwd


def resolved_token(token, cwd):
    if token.startswith("-") or "=" in token:
        return None
    path = Path(token)
    return (path if path.is_absolute() else cwd / path).resolve()


def output_argument(tokens):
    found = []
    for index, token in enumerate(tokens):
        if token == "-o" and index + 1 < len(tokens):
            found.append((index, index + 1))
    if len(found) != 1:
        raise ValueError(f"expected one unambiguous -o output, found {len(found)}")
    return found[0]


def replace_output(tokens, index_pair, output):
    result = list(tokens)
    _, value_index = index_pair
    result[value_index] = str(output)
    return result


def run_logged(name, command, cwd, report, report_path, outdir, env=None):
    command_path = outdir / f"{name}.command.json"
    stdout_path = outdir / f"{name}.stdout.log"
    stderr_path = outdir / f"{name}.stderr.log"
    record = {"name": name, "argv": list(command), "cwd": str(cwd),
              "command_sha256": hashlib.sha256(json.dumps(list(command)).encode()).hexdigest(),
              "stdout_log": str(stdout_path), "stderr_log": str(stderr_path)}
    if env is not None:
        record["environment_overrides"] = env
    command_path.write_text(json.dumps(record, indent=2) + "\n")
    record["command_log"] = str(command_path)
    try:
        result = subprocess.run(command, cwd=cwd, env=None if env is None else {**os.environ, **env},
                                capture_output=True, text=True, errors="replace", check=False)
        stdout_path.write_text(result.stdout)
        stderr_path.write_text(result.stderr)
        record["return_code"] = result.returncode
        record["stdout_sha256"] = hashlib.sha256(result.stdout.encode()).hexdigest()
        record["stderr_sha256"] = hashlib.sha256(result.stderr.encode()).hexdigest()
        record["stdout"] = result.stdout
        record["stderr"] = result.stderr
    except Exception as error:
        message = f"{type(error).__name__}: {error}"
        stdout_path.write_text("")
        stderr_path.write_text(message + "\n")
        record.update(return_code=None, launch_exception=message,
                      stdout_sha256=hashlib.sha256(b"").hexdigest(),
                      stderr_sha256=hashlib.sha256((message + "\n").encode()).hexdigest(),
                      stdout="", stderr=message)
    append_step(report, report_path, record)
    return record


def stop(report, report_path, status, reason, **fields):
    report.update(status=status, failure=reason, **fields)
    write_report(report_path, report)
    return report


def main(build_dir, fixture_zip, outdir, valgrind, ninja):
    build_dir = build_dir.resolve()
    fixture_zip = fixture_zip.resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    report_path = outdir / "report.json"
    if any(outdir.iterdir()):
        report_path = outdir / f"preflight_refused_{os.getpid()}.json"
        report = {"schema": "retained-tape-memory-smoke-v1", "status": "output_directory_not_empty",
                  "failure": "use a fresh output directory; existing logs were preserved", "output_directory": str(outdir)}
        write_report(report_path, report)
        return report
    report = {
        "schema": "retained-tape-memory-smoke-v1",
        "status": "preflight",
        "scope": "CPU Linux Valgrind lifetime capture after exactly three physical bounded-tape steps",
        "forecast_results_produced": False,
        "numerical_validation_claimed": False,
        "marker_required": MARKER,
        "partial_csv_must_be_absent": True,
        "expected_source_sha256": SOURCE_SHA,
        "source": str(SOURCE),
        "build_directory": str(build_dir),
        "fixture": str(fixture_zip),
        "fixture_expected_sha256": FIXTURE_SHA,
        "source_revision": None,
        "platform": platform.platform(),
        "steps": [],
    }
    write_report(report_path, report)
    if platform.system() != "Linux":
        return stop(report, report_path, "unsupported_platform", "Valgrind smoke is intended for Linux")
    if not SOURCE.is_file() or not fixture_zip.is_file():
        return stop(report, report_path, "preflight_failed", "source or fixture is missing")
    source_hash = file_sha(SOURCE)
    report["source_cpp_sha256"] = source_hash
    if source_hash != SOURCE_SHA:
        return stop(report, report_path, "source_hash_mismatch", "authoritative C++ source differs from the pinned revision")
    try:
        report["source_revision"] = subprocess.check_output(
            ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    except Exception as error:
        return stop(report, report_path, "source_revision_unavailable", f"{type(error).__name__}: {error}")
    if file_sha(fixture_zip) != FIXTURE_SHA:
        return stop(report, report_path, "fixture_hash_mismatch", "fixture differs from the pinned artifact")
    scratch = outdir / "scratch"
    scratch.mkdir()
    source_bytes = SOURCE.read_bytes()
    source_text = source_bytes.decode("utf-8")
    if source_text.count(PATCH_NEEDLE) != 1:
        return stop(report, report_path, "scratch_patch_failed", "bounded close insertion point did not match exactly once")
    if "#include <cstdio>" not in source_text:
        source_text = source_text.replace("#include <cmath>", "#include <cmath>\n#include <cstdio>", 1)
    scratch_source = scratch / "test_native_wave_refinement_memory.cpp"
    scratch_source.write_text(source_text.replace(PATCH_NEEDLE, PATCH_INSERT, 1))
    report["scratch_source"] = str(scratch_source)
    report["scratch_source_sha256"] = file_sha(scratch_source)
    report["scratch_patch"] = "after bounded close at n==2, print MEMORY_ONLY, close/remove partial CSV, return for Grid destruction"
    write_report(report_path, report)

    try:
        with zipfile.ZipFile(fixture_zip) as archive:
            if archive.testzip() is not None:
                return stop(report, report_path, "fixture_corrupt", "fixture CRC check failed")
            manifest = json.loads(archive.read("manifest.json"))
            inputs = outdir / "inputs"
            inputs.mkdir()
            for name in ("initial_state.txt", "observations.txt"):
                data = archive.read(name)
                expected = manifest["files"][name]["sha256"]
                if hashlib.sha256(data).hexdigest() != expected:
                    return stop(report, report_path, "fixture_member_hash_mismatch", f"fixture member hash mismatch: {name}")
                (inputs / name).write_bytes(data)
    except Exception as error:
        return stop(report, report_path, "fixture_read_failed", f"{type(error).__name__}: {error}")
    report["input_sha256"] = {name: file_sha(outdir / "inputs" / name) for name in ("initial_state.txt", "observations.txt")}
    write_report(report_path, report)

    compile_db = build_dir / COMPILE_DATABASE
    if not compile_db.is_file():
        return stop(report, report_path, "compile_database_missing", f"{compile_db} missing; configure CMAKE_EXPORT_COMPILE_COMMANDS=ON")
    try:
        entries = json.loads(compile_db.read_text())
        matches = []
        for entry in entries:
            entry_file = Path(entry["file"])
            if not entry_file.is_absolute():
                entry_file = Path(entry["directory"]) / entry_file
            if entry_file.resolve() == SOURCE.resolve():
                matches.append(entry)
        if len(matches) != 1:
            return stop(report, report_path, "compile_entry_ambiguous", f"expected one compile entry for source, found {len(matches)}")
        entry = matches[0]
        compile_cwd = Path(entry["directory"]).resolve()
        original_compile = entry.get("arguments") or shlex.split(entry["command"], posix=True)
        original_compile, compile_cwd = parse_command(" ".join(shlex.quote(str(x)) for x in original_compile), compile_cwd)
        object_out_index = output_argument(original_compile)
        original_object_token = original_compile[object_out_index[1]]
        original_object = Path(original_object_token)
        original_object = (original_object if original_object.is_absolute() else compile_cwd / original_object).resolve()
        compile_argv = list(original_compile)
        if any(token.startswith(("-MF", "-MT")) and token not in ("-MF", "-MT")
               for token in compile_argv):
            return stop(report, report_path, "compile_depfile_unsupported",
                        "combined -MF/-MT forms cannot be safely rewritten")
        compile_argv = replace_output(compile_argv, object_out_index, scratch / "test_native_wave_refinement_memory.o")
        source_indices = [i for i, token in enumerate(compile_argv)
                          if resolved_token(token, compile_cwd) == SOURCE.resolve()]
        if len(source_indices) != 1:
            return stop(report, report_path, "compile_source_rewrite_failed", f"expected one source argument, found {len(source_indices)}")
        compile_argv[source_indices[0]] = str(scratch_source)
        quote_dir = str(SOURCE.parent)
        if not any(compile_argv[i] == "-iquote" and compile_argv[i + 1] == quote_dir
                   for i in range(len(compile_argv) - 1)):
            compile_argv.extend(["-iquote", quote_dir])
        mf_indices = [i for i, token in enumerate(compile_argv[:-1]) if token == "-MF"]
        for i in mf_indices:
            compile_argv[i + 1] = str(scratch / "test_native_wave_refinement_memory.d")
        mt_indices = [i for i, token in enumerate(compile_argv[:-1]) if token == "-MT"]
        for i in mt_indices:
            compile_argv[i + 1] = str(scratch / "test_native_wave_refinement_memory.o")
        if "-c" not in compile_argv:
            return stop(report, report_path, "compile_command_invalid", "selected compile entry is not a compile-only command")
        compile_record = {
            "cwd": str(compile_cwd), "argv": compile_argv,
            "command_sha256": hashlib.sha256(json.dumps(compile_argv).encode()).hexdigest(),
            "original_object": str(original_object),
        }
        (outdir / "scratch_compile_command.json").write_text(json.dumps(compile_record, indent=2) + "\n")
        report["compile_command_sha256"] = compile_record["command_sha256"]
        report["compile_command_log"] = str(outdir / "scratch_compile_command.json")
        write_report(report_path, report)
    except Exception as error:
        return stop(report, report_path, "compile_command_parse_failed", f"{type(error).__name__}: {error}")

    # `ninja -t commands` supplies the configured core, Torch, and system link flags.
    commands_record = run_logged("ninja_commands", [ninja, "-C", str(build_dir), "-t", "commands", TARGET],
                                 ROOT, report, report_path, outdir)
    if commands_record.get("return_code") != 0:
        return stop(report, report_path, "ninja_commands_failed", "could not obtain the configured link command")
    link_candidates = []
    for line in commands_record["stdout"].splitlines():
        try:
            argv, cwd = parse_command(line, build_dir)
            out_index = output_argument(argv)
            out_token = argv[out_index[1]]
            has_object_input = any(
                i not in (out_index[0], out_index[1]) and resolved_token(token, cwd) == original_object
                for i, token in enumerate(argv)
            )
            if has_object_input and Path(out_token).name == TARGET:
                link_candidates.append((argv, cwd, out_index))
        except (ValueError, OSError):
            continue
    if not link_candidates:
        return stop(report, report_path, "link_command_not_found", "ninja output contained no matching target link command with the test object")
    link_argv, link_cwd, link_out_index = link_candidates[-1]
    link_argv = list(link_argv)
    link_object_indices = [i for i, token in enumerate(link_argv)
                           if resolved_token(token, link_cwd) == original_object]
    if not link_object_indices:
        return stop(report, report_path, "link_object_rewrite_failed", "selected link command lost the original test object")
    for index in link_object_indices:
        link_argv[index] = str(scratch / "test_native_wave_refinement_memory.o")
    scratch_exe = scratch / TARGET
    link_argv = replace_output(link_argv, link_out_index, scratch_exe)
    report["link_command_sha256"] = hashlib.sha256(json.dumps(link_argv).encode()).hexdigest()
    report["link_command"] = link_argv
    report["link_command_cwd"] = str(link_cwd)
    archives = []
    for token in link_argv:
        if token.endswith(".a"):
            archive = resolved_token(token, link_cwd)
            if archive is not None and archive.is_file():
                archives.append({"path": str(archive), "size_bytes": archive.stat().st_size,
                                 "sha256": file_sha(archive)})
    report["existing_static_archive_inputs"] = archives
    write_report(report_path, report)

    vg_version = run_logged("valgrind_version", [valgrind, "--version"], ROOT, report, report_path, outdir)
    if vg_version.get("return_code") is None or vg_version.get("return_code") != 0:
        return stop(report, report_path, "tool_unsupported", "Valgrind is unavailable or does not start; raw exception/output preserved")
    report["valgrind_version"] = vg_version["stdout"].strip()
    write_report(report_path, report)

    compile_result = run_logged("scratch_compile", compile_argv, compile_cwd, report, report_path, outdir)
    if compile_result.get("return_code") != 0:
        return stop(report, report_path, "scratch_compile_failed", "scratch source compilation failed; raw logs preserved")
    scratch_object = scratch / "test_native_wave_refinement_memory.o"
    if not scratch_object.is_file():
        return stop(report, report_path, "scratch_object_missing", "compiler returned success without producing scratch object")
    report["scratch_object_sha256"] = file_sha(scratch_object)
    write_report(report_path, report)

    link_result = run_logged("scratch_link", link_argv, link_cwd, report, report_path, outdir)
    if link_result.get("return_code") != 0:
        return stop(report, report_path, "scratch_link_failed", "scratch binary link failed; raw logs preserved")
    if not scratch_exe.is_file():
        return stop(report, report_path, "scratch_executable_missing", "linker returned success without producing scratch executable")
    report["scratch_executable_sha256"] = file_sha(scratch_exe)
    write_report(report_path, report)

    partial_csv = outdir / "memory_only_partial.csv"
    partial_csv.unlink(missing_ok=True)
    valgrind_log = outdir / "valgrind.log"
    inputs = outdir / "inputs"
    argv = [valgrind, "--leak-check=full", "--show-leak-kinds=all", "--num-callers=30",
            f"--log-file={valgrind_log}", str(scratch_exe), "16", "12", "8", str(partial_csv),
            "--physical-wave-inverse", str(inputs / "initial_state.txt"), str(inputs / "observations.txt"),
            "30", "10", "--newton-tol", "1e-14", "--krylov-tol", "1e-12",
            "--bounded-tape-forward-only"]
    overrides = {"ATEN_CPU_CAPABILITY": "default", "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1"}
    report["valgrind_command"] = argv
    report["valgrind_command_sha256"] = hashlib.sha256(json.dumps(argv).encode()).hexdigest()
    report["valgrind_log"] = str(valgrind_log)
    report["run_contract"] = {"grid": [16, 12, 8], "physical_steps": 3, "dt": 10,
                               "solver_configuration": "N14/K12 native test settings",
                               "environment_overrides_only": overrides,
                               "production_or_TLS_guards_changed": False,
                               "forecast_or_numerical_result_claimed": False}
    write_report(report_path, report)
    run_result = run_logged("valgrind_memory_smoke", argv, ROOT, report, report_path, outdir, env=overrides)
    marker_seen = MARKER in run_result.get("stdout", "")
    csv_absent = not partial_csv.exists()
    heap_log = valgrind_log.read_text(errors="replace") if valgrind_log.is_file() else ""
    heap_summary_seen = "HEAP SUMMARY:" in heap_log
    leak_summary_seen = ("LEAK SUMMARY:" in heap_log or
                         "All heap blocks were freed" in heap_log)
    report["capture_checks"] = {"process_return_code": run_result.get("return_code"),
                                "marker_seen_after_three_steps": marker_seen,
                                "partial_csv_absent": csv_absent,
                                "valgrind_log_exists": valgrind_log.is_file(),
                                "heap_summary_seen": heap_summary_seen,
                                "leak_summary_seen": leak_summary_seen,
                                "valgrind_log_sha256": file_sha(valgrind_log) if valgrind_log.is_file() else None}
    if (run_result.get("return_code") == 0 and marker_seen and csv_absent and
            heap_summary_seen and leak_summary_seen):
        report["status"] = "memory_capture_complete_not_numerical_validation"
    else:
        report["status"] = "memory_capture_incomplete"
    report["authoritative_source_unchanged"] = file_sha(SOURCE) == SOURCE_SHA
    if not report["authoritative_source_unchanged"]:
        report["status"] = "authoritative_source_changed_during_run"
    write_report(report_path, report)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="configured Ninja build directory with compile_commands.json")
    parser.add_argument("--fixture", type=Path, default=ROOT / "tools/fixtures/pr282-exact-returned-state.zip")
    parser.add_argument("--out", type=Path, required=True, help="fresh directory for scratch build and raw logs")
    parser.add_argument("--valgrind", default="valgrind")
    parser.add_argument("--ninja", default="ninja")
    args = parser.parse_args()
    result = main(args.build_dir, args.fixture, args.out, args.valgrind, args.ninja)
    raise SystemExit(0 if result.get("status") == "memory_capture_complete_not_numerical_validation" else 1)
