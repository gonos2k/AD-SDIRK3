#!/usr/bin/env python3
"""Run a controlled SDIRK3 Krylov early-stop ablation on em_b_wave.

The four cases keep the Stage-2 effective restart/tolerance explicit while
changing only the policy triggers that can terminate Arnoldi early:

A_legacy      stage2_restart>0, max_restarts=1 -> current aggressive midpoint gate
B_normal      same effective budget, but stage2_restart=0 -> normal stagnation only
C_full1       same one-restart budget, Arnoldi stagnation effectively disabled
D_full3       same as C, but three restart cycles

All cases enable the env-gated block-max Newton gate so the Stage-2 mu residual
cannot be hidden by the global RMS. Production defaults are not changed.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Iterable, Mapping, Optional

MARKER_FGMRES = "SDIRK3_FGMRES_DIAG"
MARKER_SHADOW = "SDIRK3_NEWTON_SHADOW"
EARLY_REASONS = {
    "arnoldi_stagnation",
    "mid_budget_hopeless",
    "restart_stagnation_threshold",
}

# Remove inherited values that would invalidate the paired comparison.
CONTROLLED_ENV_KEYS = {
    "OMP_NUM_THREADS",
    "OMP_DYNAMIC",
    "MKL_NUM_THREADS",
    "OPENBLAS_NUM_THREADS",
    "WRF_SDIRK3_BLOCKMAX_GATE",
    "WRF_SDIRK3_NUMERICAL_SHADOW",
    "WRF_SDIRK3_STAGE_DIAG",
    "WRF_SDIRK3_RHS_COUNT",
    "WRF_SDIRK3_STAGE_OPERAND_DIAG",
    "WRF_SDIRK3_SOLVER_TELEMETRY",
    "WRF_SDIRK3_DEBUG_LEVEL",
    "WRF_SDIRK3_GMRES_RESTART",
    "WRF_SDIRK3_MAX_KRYLOV_ITER",
    "WRF_SDIRK3_GMRES_TRUE_RESIDUAL_START_J",
    "WRF_SDIRK3_GMRES_TRUE_RESIDUAL_PERIOD",
    "WRF_SDIRK3_GMRES_ARNOLDI_STAG_WINDOW",
    "WRF_SDIRK3_GMRES_ARNOLDI_STAG_RATIO",
    "WRF_SDIRK3_STAGE2_GMRES_RESTART",
    "WRF_SDIRK3_STAGE2_MAX_KRYLOV_RESTARTS",
    "WRF_SDIRK3_STAGE2_KRYLOV_TOL",
    "WRF_SDIRK3_STAGE3_GMRES_RESTART",
    "WRF_SDIRK3_STAGE3_MAX_KRYLOV_RESTARTS",
    "WRF_SDIRK3_STAGE3_KRYLOV_TOL",
    "WRF_SDIRK3_JVP_AUTO_BENCH_CALLS",
    "WRF_SDIRK3_GMRES_WARMSTART",
    "WRF_SDIRK3_INN_WARMSTART_ENABLE",
    "WRF_SDIRK3_INN_TOL_RAMP_ENABLE",
}

DYNAMIC_PREFIXES = (
    "rsl.error.",
    "rsl.out.",
    "wrfinput_",
    "wrfbdy_",
    "wrfout_",
    "wrfrst_",
    "auxhist",
    "met_em.",
)
DYNAMIC_NAMES = {"fort.85", "namelist.output", "ideal.log", "wrf.log"}


@dataclass(frozen=True)
class Case:
    name: str
    stage2_restart_override: int
    max_restarts: int
    stagnation_window: int
    stagnation_ratio: float
    intent: str


@dataclass
class Result:
    case: str
    intent: str
    exit_code: int
    fgmres_records: int = 0
    shadow_records: int = 0
    ts: Optional[int] = None
    path: str = "missing"
    termination_reason: str = "missing"
    probe_j: Optional[int] = None
    probe_true_err: Optional[float] = None
    hopeless_floor: Optional[float] = None
    stag_ratio_used: Optional[float] = None
    stag_count: Optional[int] = None
    restart: Optional[int] = None
    max_restarts: Optional[int] = None
    krylov_iters: Optional[int] = None
    rel_err: Optional[float] = None
    krylov_tol: Optional[float] = None
    raw_ru_share: Optional[float] = None
    dyn_global_rms: Optional[float] = None
    fixed_s0_global_rms: Optional[float] = None
    block_max_s0: Optional[float] = None
    block_s0: str = "missing"
    block_equal_s0: Optional[float] = None
    newton_tol: Optional[float] = None
    rhs_total: Optional[int] = None
    log_path: str = ""

    @property
    def stopped_early(self) -> bool:
        if self.termination_reason in EARLY_REASONS:
            return True
        if self.krylov_iters is None or self.restart is None:
            return False
        return self.krylov_iters < self.restart and self.termination_reason not in {
            "initial_converged",
            "tolerance_reached",
            "happy_breakdown",
        }


def _to_int(value: Optional[str]) -> Optional[int]:
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def _to_float(value: Optional[str]) -> Optional[float]:
    try:
        return float(value) if value is not None else None
    except ValueError:
        return None


def parse_kv_record(line: str, marker: str) -> Optional[dict[str, str]]:
    """Return key/value tokens after marker; quoted msg fields are supported."""
    pos = line.find(marker)
    if pos < 0:
        return None
    try:
        tokens = shlex.split(line[pos:])
    except ValueError:
        tokens = line[pos:].split()
    if not tokens or tokens[0] != marker:
        return None
    out: dict[str, str] = {}
    for token in tokens[1:]:
        if "=" not in token:
            continue
        key, value = token.split("=", 1)
        out[key] = value
    return out


def records(text: str, marker: str) -> list[dict[str, str]]:
    parsed: list[dict[str, str]] = []
    for line in text.splitlines():
        row = parse_kv_record(line, marker)
        if row is not None:
            parsed.append(row)
    return parsed


def first_stage2_iter0(rows: Iterable[dict[str, str]]) -> Optional[dict[str, str]]:
    for row in rows:
        if row.get("stage") == "2" and row.get("iter") == "0":
            return row
    return None


def parse_result(case: Case, text: str, exit_code: int, log_path: Path) -> Result:
    fgmres_all = records(text, MARKER_FGMRES)
    shadow_all = records(text, MARKER_SHADOW)
    fgmres = first_stage2_iter0(fgmres_all)
    shadow = first_stage2_iter0(shadow_all)

    result = Result(
        case=case.name,
        intent=case.intent,
        exit_code=exit_code,
        fgmres_records=sum(
            1 for r in fgmres_all if r.get("stage") == "2" and r.get("iter") == "0"
        ),
        shadow_records=sum(
            1 for r in shadow_all if r.get("stage") == "2" and r.get("iter") == "0"
        ),
        log_path=str(log_path),
    )
    if fgmres:
        result.ts = _to_int(fgmres.get("ts"))
        result.path = fgmres.get("path", "missing")
        result.termination_reason = fgmres.get("termination_reason", "missing")
        result.probe_j = _to_int(fgmres.get("probe_j"))
        result.probe_true_err = _to_float(fgmres.get("probe_true_err"))
        result.hopeless_floor = _to_float(fgmres.get("hopeless_floor"))
        result.stag_ratio_used = _to_float(fgmres.get("stag_ratio"))
        result.stag_count = _to_int(fgmres.get("stag_count"))
        result.restart = _to_int(fgmres.get("restart"))
        result.max_restarts = _to_int(fgmres.get("max_restarts"))
        result.krylov_iters = _to_int(fgmres.get("iters"))
        result.rel_err = _to_float(fgmres.get("rel_err"))
        result.krylov_tol = _to_float(fgmres.get("tol"))
        result.raw_ru_share = _to_float(fgmres.get("ru_share"))
    if shadow:
        result.dyn_global_rms = _to_float(shadow.get("dyn_S_rms"))
        result.fixed_s0_global_rms = _to_float(shadow.get("fix_S0_rms"))
        result.block_max_s0 = _to_float(shadow.get("block_max_S0"))
        result.block_s0 = shadow.get("block_S0", "missing")
        result.block_equal_s0 = _to_float(shadow.get("block_equal_S0"))
        result.newton_tol = _to_float(shadow.get("newton_tol"))

    rhs = re.findall(r"SDIRK3_RHS_RUN_TOTAL[^\n]*\btotal=(\d+)", text)
    if rhs:
        result.rhs_total = int(rhs[-1])
    return result


def _fmt(value: object) -> str:
    if value is None:
        return "NA"
    if isinstance(value, float):
        return f"{value:.8g}"
    return str(value)


def write_summary(results: list[Result], output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    (output_dir / "summary.json").write_text(
        json.dumps([{**asdict(r), "stopped_early": r.stopped_early} for r in results], indent=2)
        + "\n",
        encoding="utf-8",
    )
    columns = [
        "case",
        "exit_code",
        "termination_reason",
        "probe_j",
        "probe_true_err",
        "stag_count",
        "restart",
        "max_restarts",
        "krylov_iters",
        "rel_err",
        "krylov_tol",
        "raw_ru_share",
        "dyn_global_rms",
        "fixed_s0_global_rms",
        "block_max_s0",
        "block_s0",
        "block_equal_s0",
        "newton_tol",
        "rhs_total",
        "stopped_early",
        "log_path",
    ]
    lines = ["\t".join(columns)]
    for result in results:
        row = {**asdict(result), "stopped_early": result.stopped_early}
        lines.append("\t".join(_fmt(row[c]) for c in columns))
    (output_dir / "summary.tsv").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (output_dir / "verdict.md").write_text(build_verdict(results), encoding="utf-8")


def build_verdict(results: list[Result]) -> str:
    by_name = {r.case: r for r in results}
    a = by_name.get("A_legacy")
    b = by_name.get("B_normal")
    c = by_name.get("C_full1")
    d = by_name.get("D_full3")

    lines = ["# Krylov early-stop ablation verdict", ""]
    if not all((a, b, c, d)):
        lines += ["Incomplete matrix: one or more cases are missing.", ""]
        return "\n".join(lines)

    assert a and b and c and d
    lines += [
        "## Observations",
        "",
        f"- A legacy: reason `{a.termination_reason}`, iters `{_fmt(a.krylov_iters)}`, rel_err `{_fmt(a.rel_err)}`.",
        f"- B normal detector: reason `{b.termination_reason}`, iters `{_fmt(b.krylov_iters)}`, rel_err `{_fmt(b.rel_err)}`.",
        f"- C full one restart: reason `{c.termination_reason}`, iters `{_fmt(c.krylov_iters)}`, rel_err `{_fmt(c.rel_err)}`.",
        f"- D full three restarts: reason `{d.termination_reason}`, iters `{_fmt(d.krylov_iters)}`, rel_err `{_fmt(d.rel_err)}`.",
        "",
        "## Interpretation",
        "",
    ]

    c_improves_a = (
        a.rel_err is not None
        and c.rel_err is not None
        and c.rel_err < 0.9 * max(a.rel_err, 1.0e-30)
    )
    d_improves_c = (
        c.rel_err is not None
        and d.rel_err is not None
        and d.rel_err < 0.9 * max(c.rel_err, 1.0e-30)
    )
    d_plateau = d.rel_err is not None and d.rel_err >= 0.99

    if a.stopped_early and c_improves_a:
        lines.append(
            "- **Early-stop policy is causally limiting the solve:** the full one-restart case materially improves on legacy."
        )
    elif a.stopped_early:
        lines.append(
            "- Legacy exits early, but one full restart does not materially improve the residual; early-stop is not yet the main blocker."
        )
    else:
        lines.append("- Legacy did not terminate through a recognized early-stop reason in the selected record.")

    if d_improves_c:
        lines.append(
            "- **Restart budget matters:** three restarts materially improve on one full restart."
        )
    elif d_plateau:
        lines.append(
            "- **Plateau persists after the long run:** prioritize operator/JVP/preconditioner/IMEX split diagnosis."
        )
    else:
        lines.append(
            "- Additional restarts do not materially change the result, but the final residual is below the hard plateau threshold."
        )

    if d.termination_reason in EARLY_REASONS:
        lines.append(
            "- Warning: D still ended under an early-stop reason, so it is not a clean full-budget control. Inspect its log before assigning root cause."
        )
    if d.termination_reason in {"nan_retry_exhausted", "happy_breakdown"}:
        lines.append(
            f"- Warning: D terminated as `{d.termination_reason}`; this is a different failure class from stagnation."
        )

    lines += [
        "",
        "The script does not change production defaults; it only controls existing environment variables in isolated workspaces.",
        "",
    ]
    return "\n".join(lines)


def read_dt(namelist: Path) -> Optional[int]:
    text = namelist.read_text(encoding="utf-8", errors="replace")
    # Strip Fortran comments before matching.
    text = "\n".join(line.split("!", 1)[0] for line in text.splitlines())
    match = re.search(r"(?im)^\s*time_step\s*=\s*(\d+)", text)
    return int(match.group(1)) if match else None


def is_dynamic_entry(name: str) -> bool:
    return name in DYNAMIC_NAMES or name.startswith(DYNAMIC_PREFIXES)


def prepare_workspace(run_dir: Path, workspace: Path) -> None:
    workspace.mkdir(parents=True, exist_ok=False)
    for src in run_dir.iterdir():
        if is_dynamic_entry(src.name):
            continue
        dst = workspace / src.name
        if src.name == "namelist.input":
            shutil.copy2(src, dst)
        else:
            dst.symlink_to(src.resolve(), target_is_directory=src.is_dir())


def run_process(command: list[str], cwd: Path, env: Mapping[str, str], log: Path) -> int:
    with log.open("wb") as handle:
        completed = subprocess.run(
            command,
            cwd=cwd,
            env=dict(env),
            stdout=handle,
            stderr=subprocess.STDOUT,
            check=False,
        )
    return completed.returncode


def controlled_env(case: Case, args: argparse.Namespace) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if k not in CONTROLLED_ENV_KEYS}
    env.update(
        {
            "OMP_NUM_THREADS": "1",
            "OMP_DYNAMIC": "FALSE",
            "MKL_NUM_THREADS": "1",
            "OPENBLAS_NUM_THREADS": "1",
            "WRF_SDIRK3_BLOCKMAX_GATE": "1",
            "WRF_SDIRK3_NUMERICAL_SHADOW": "1",
            "WRF_SDIRK3_STAGE_DIAG": "1",
            "WRF_SDIRK3_RHS_COUNT": "1",
            "WRF_SDIRK3_STAGE_OPERAND_DIAG": "1",
            "WRF_SDIRK3_SOLVER_TELEMETRY": "1",
            "WRF_SDIRK3_DEBUG_LEVEL": str(args.debug_level),
            "WRF_SDIRK3_GMRES_RESTART": str(args.restart),
            "WRF_SDIRK3_MAX_KRYLOV_ITER": str(case.max_restarts),
            "WRF_SDIRK3_GMRES_TRUE_RESIDUAL_START_J": str(args.probe_start),
            "WRF_SDIRK3_GMRES_TRUE_RESIDUAL_PERIOD": str(args.probe_period),
            "WRF_SDIRK3_GMRES_ARNOLDI_STAG_WINDOW": str(case.stagnation_window),
            "WRF_SDIRK3_GMRES_ARNOLDI_STAG_RATIO": str(case.stagnation_ratio),
            "WRF_SDIRK3_STAGE2_GMRES_RESTART": str(case.stage2_restart_override),
            "WRF_SDIRK3_STAGE2_MAX_KRYLOV_RESTARTS": str(case.max_restarts),
            # Explicit override prevents EW budget coupling from changing restart counts.
            "WRF_SDIRK3_STAGE2_KRYLOV_TOL": str(args.krylov_tol),
            "WRF_SDIRK3_STAGE3_GMRES_RESTART": "0",
            "WRF_SDIRK3_STAGE3_MAX_KRYLOV_RESTARTS": "0",
            "WRF_SDIRK3_STAGE3_KRYLOV_TOL": "0",
            "WRF_SDIRK3_JVP_AUTO_BENCH_CALLS": "0",
            "WRF_SDIRK3_GMRES_WARMSTART": "0",
            "WRF_SDIRK3_INN_WARMSTART_ENABLE": "0",
            "WRF_SDIRK3_INN_TOL_RAMP_ENABLE": "0",
        }
    )
    return env


def validate_result_contract(case: Case, result: Result, args: argparse.Namespace) -> None:
    """Fail closed when a run does not preserve the paired-budget contract."""
    problems: list[str] = []
    if result.fgmres_records < 1:
        problems.append("no Stage-2 iter-0 FGMRES diagnostic")
    if result.shadow_records < 1:
        problems.append("no Stage-2 iter-0 Newton shadow")
    if result.restart != args.restart:
        problems.append(f"effective restart={result.restart}, expected {args.restart}")
    if result.max_restarts != case.max_restarts:
        problems.append(
            f"effective max_restarts={result.max_restarts}, expected {case.max_restarts}"
        )
    if result.krylov_tol is None or abs(result.krylov_tol - args.krylov_tol) > 1.0e-6:
        problems.append(
            f"effective tol={result.krylov_tol}, expected {args.krylov_tol}"
        )
    if result.block_max_s0 is None or result.newton_tol is None:
        problems.append("block-max/newton-tolerance evidence missing")
    if problems:
        raise RuntimeError(f"{case.name}: invalid paired run: " + "; ".join(problems))


def append_rsl(workspace: Path, combined_log: Path) -> None:
    with combined_log.open("ab") as out:
        for pattern in ("rsl.error.*", "rsl.out.*"):
            for path in sorted(workspace.glob(pattern)):
                out.write(f"\n===== {path.name} =====\n".encode())
                out.write(path.read_bytes())


def cases(args: argparse.Namespace) -> list[Case]:
    return [
        Case(
            "A_legacy",
            args.restart,
            1,
            args.normal_stag_window,
            args.normal_stag_ratio,
            "current aggressive midpoint policy",
        ),
        Case(
            "B_normal",
            0,
            1,
            args.normal_stag_window,
            args.normal_stag_ratio,
            "same effective budget, normal measured-stagnation window",
        ),
        Case(
            "C_full1",
            0,
            1,
            args.full_stag_window,
            1.0,
            "one full restart with Arnoldi stagnation effectively disabled",
        ),
        Case(
            "D_full3",
            0,
            args.long_restarts,
            args.full_stag_window,
            1.0,
            "long control with multiple full restart cycles",
        ),
    ]


def run_matrix(args: argparse.Namespace) -> int:
    script = Path(__file__).resolve()
    repo_root = script.parents[4]
    run_dir = Path(args.run_dir).resolve() if args.run_dir else repo_root / "test" / "em_b_wave"
    for required in (run_dir / "ideal.exe", run_dir / "wrf.exe", run_dir / "namelist.input"):
        if not required.exists():
            raise RuntimeError(f"required path not found: {required}")
    if not os.access(run_dir / "ideal.exe", os.X_OK) or not os.access(run_dir / "wrf.exe", os.X_OK):
        raise RuntimeError("ideal.exe and wrf.exe must be executable")

    actual_dt = read_dt(run_dir / "namelist.input")
    if actual_dt != args.expect_dt and not args.allow_dt_mismatch:
        raise RuntimeError(
            f"namelist time_step={actual_dt!r}, expected {args.expect_dt}; "
            "use --allow-dt-mismatch only for an intentional different ladder point"
        )

    if args.output_dir:
        output_dir = Path(args.output_dir).resolve()
        if output_dir == run_dir or run_dir in output_dir.parents:
            raise RuntimeError("--output-dir must be outside the WRF run directory")
        output_dir.mkdir(parents=True, exist_ok=False)
    else:
        stamp = dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
        output_dir = Path(tempfile.mkdtemp(prefix=f"sdirk3-krylov-ablation-{stamp}-"))

    manifest = {
        "run_dir": str(run_dir),
        "output_dir": str(output_dir),
        "time_step": actual_dt,
        "restart": args.restart,
        "krylov_tol": args.krylov_tol,
        "probe_start": args.probe_start,
        "probe_period": args.probe_period,
        "normal_stag_window": args.normal_stag_window,
        "normal_stag_ratio": args.normal_stag_ratio,
        "full_stag_window": args.full_stag_window,
        "long_restarts": args.long_restarts,
        "cases": [asdict(case) for case in cases(args)],
    }
    (output_dir / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")

    results: list[Result] = []
    for case in cases(args):
        workspace = output_dir / f"work-{case.name}"
        prepare_workspace(run_dir, workspace)
        env = controlled_env(case, args)
        (output_dir / f"{case.name}.env").write_text(
            "".join(f"{key}={env[key]}\n" for key in sorted(CONTROLLED_ENV_KEYS) if key in env),
            encoding="utf-8",
        )

        ideal_log = output_dir / f"{case.name}.ideal.log"
        ideal_code = run_process([str(workspace / "ideal.exe")], workspace, env, ideal_log)
        if ideal_code != 0 or not (workspace / "wrfinput_d01").is_file():
            raise RuntimeError(
                f"{case.name}: ideal.exe failed ({ideal_code}) or produced no wrfinput_d01; see {ideal_log}"
            )

        combined_log = output_dir / f"{case.name}.combined.log"
        wrf_code = run_process([str(workspace / "wrf.exe")], workspace, env, combined_log)
        append_rsl(workspace, combined_log)
        text = combined_log.read_text(encoding="utf-8", errors="replace")
        result = parse_result(case, text, wrf_code, combined_log)
        validate_result_contract(case, result, args)
        results.append(result)
        write_summary(results, output_dir)

        print(
            f"{case.name}: exit={wrf_code} reason={result.termination_reason} "
            f"iters={_fmt(result.krylov_iters)} rel_err={_fmt(result.rel_err)} "
            f"probe_j={_fmt(result.probe_j)} block={result.block_s0}:"
            f"{_fmt(result.block_max_s0)}"
        )
        if not args.keep_workspaces:
            shutil.rmtree(workspace)

    print(f"results: {output_dir}")
    print((output_dir / "verdict.md").read_text(encoding="utf-8"))
    return 0


def self_test() -> int:
    synthetic = """
SDIRK3_NEWTON_SHADOW stage=2 iter=0 dyn_S_rms=2.35e-2 fix_S0_rms=2.35e-2 unscaled_rms=1.0 block_max_dyn=2.73e-1 block_dyn=mu block_max_S0=2.73e-1 block_S0=mu block_equal_S0=1.13e-1 newton_tol=2.0e-1
SDIRK3_FGMRES_DIAG ts=0 stage=2 iter=0 path=fgmres rhs_norm=1 x0_norm=0 tol=3e-1 restart=15 max_restarts=1 iters=7 restarts=1 final_res=1 rel_err=1 converged=0 breakdown=0 stagnation=1 termination_reason=arnoldi_stagnation ru_share=0.99 probe_j=7 probe_true_err=1 hopeless_floor=0.9 stag_ratio=0.995 stag_count=1 dx_finite=1 variable_pc=0 msg="FGMRES Arnoldi stagnation early exit"
SDIRK3_RHS_RUN_TOTAL generation=1 total=17
""".strip()
    case = Case("A_legacy", 15, 1, 3, 0.995, "test")
    result = parse_result(case, synthetic, 0, Path("synthetic.log"))
    assert result.termination_reason == "arnoldi_stagnation"
    assert result.probe_j == 7
    assert result.krylov_iters == 7
    assert result.restart == 15
    assert result.stopped_early
    assert result.block_s0 == "mu"
    assert abs((result.block_max_s0 or 0.0) - 0.273) < 1.0e-12
    assert result.rhs_total == 17

    full = Result(
        case="C_full1",
        intent="test",
        exit_code=0,
        termination_reason="max_budget",
        restart=15,
        max_restarts=1,
        krylov_iters=15,
        rel_err=0.7,
    )
    long = Result(
        case="D_full3",
        intent="test",
        exit_code=0,
        termination_reason="max_budget",
        restart=15,
        max_restarts=3,
        krylov_iters=45,
        rel_err=0.3,
    )
    normal = Result(
        case="B_normal",
        intent="test",
        exit_code=0,
        termination_reason="arnoldi_stagnation",
        restart=15,
        max_restarts=1,
        krylov_iters=8,
        rel_err=0.95,
    )
    verdict = build_verdict([result, normal, full, long])
    assert "Early-stop policy is causally limiting" in verdict
    assert "Restart budget matters" in verdict
    print("krylov_early_stop_ablation: SELF-TEST PASS")
    return 0


def make_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--run-dir", help="WRF em_b_wave run directory (default: <repo>/test/em_b_wave)")
    parser.add_argument("--output-dir", help="new directory for logs/results (default: unique /tmp directory)")
    parser.add_argument("--expect-dt", type=int, default=600)
    parser.add_argument("--allow-dt-mismatch", action="store_true")
    parser.add_argument("--restart", type=int, default=15)
    parser.add_argument("--krylov-tol", type=float, default=0.30)
    parser.add_argument("--probe-start", type=int, default=2)
    parser.add_argument("--probe-period", type=int, default=2)
    parser.add_argument("--normal-stag-window", type=int, default=3)
    parser.add_argument("--normal-stag-ratio", type=float, default=0.995)
    parser.add_argument("--full-stag-window", type=int, default=20)
    parser.add_argument("--long-restarts", type=int, default=3)
    parser.add_argument("--debug-level", type=int, default=1)
    parser.add_argument("--keep-workspaces", action="store_true")
    parser.add_argument("--self-test", action="store_true")
    return parser


def main() -> int:
    args = make_parser().parse_args()
    if args.self_test:
        return self_test()
    if not (1 <= args.restart <= 100):
        raise ValueError("--restart must be in [1,100]")
    if not (2 <= args.long_restarts <= 20):
        raise ValueError("--long-restarts must be in [2,20]")
    if not (1 <= args.normal_stag_window <= 20):
        raise ValueError("--normal-stag-window must be in [1,20]")
    if not (1 <= args.full_stag_window <= 20):
        raise ValueError("--full-stag-window must be in [1,20]")
    if not (0.0 < args.krylov_tol <= 1.0):
        raise ValueError("--krylov-tol must be in (0,1]")
    if args.probe_start < 0 or not (1 <= args.probe_period <= 100):
        raise ValueError("--probe-start must be >=0 and --probe-period in [1,100]")
    if not (0.5 <= args.normal_stag_ratio <= 1.0):
        raise ValueError("--normal-stag-ratio must be in [0.5,1]")
    last_j = args.restart - 1
    periodic_probes = (
        0
        if args.probe_start > last_j
        else 1 + (last_j - args.probe_start) // args.probe_period
    )
    # The final Arnoldi index is always probed, even if it is not on the periodic lattice.
    if last_j >= 0 and (args.probe_start > last_j or
                        (last_j - args.probe_start) % args.probe_period != 0):
        periodic_probes += 1
    if args.full_stag_window <= periodic_probes:
        raise ValueError(
            "--full-stag-window must exceed the maximum probes per restart "
            f"({periodic_probes}) so C/D cannot trip Arnoldi stagnation"
        )
    return run_matrix(args)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except KeyboardInterrupt:
        raise SystemExit(130)
    except Exception as exc:  # fail closed with one actionable message
        print(f"FATAL: {exc}", file=sys.stderr)
        raise SystemExit(2)
