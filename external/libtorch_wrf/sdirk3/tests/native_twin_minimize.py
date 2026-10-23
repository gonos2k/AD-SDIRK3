#!/usr/bin/env python3
"""Bounded two-control inverse-BFGS policy for the native idealized twin.

The caller owns all model work. ``cost(a)`` performs exactly one objective
forward and returns a mapping with ``J`` plus an opaque trajectory record.
``gradient(record)`` reuses that record and returns the two-control gradient;
it performs no objective forward. Truth controls are intentionally absent.
"""
from __future__ import annotations

from collections.abc import Callable, Mapping
from pathlib import Path
import sys
from typing import Any

import numpy as np

# Reuse the repository's finite/SPD-checked inverse-BFGS formula. Keep the
# helper's owner in test_fine_wave_inverse.py rather than forking its algebra.
ROOT = Path(__file__).resolve().parents[4]
TOOLS = ROOT / "tools"
if str(TOOLS) not in sys.path:
    sys.path.insert(0, str(TOOLS))
from test_fine_wave_inverse import safe_bfgs_inverse_update  # noqa: E402

ARMIJO_RHO = 1.0e-4
INITIAL_STEP = 1.0
STEP_SHRINK = 0.5
MAX_BACKTRACKS = 4
MAX_ACCEPTED_UPDATES = 12
MAX_OBJECTIVE_FORWARDS = 40
MAX_GRADIENT_CALLS = MAX_ACCEPTED_UPDATES + 1
MAX_NORMALIZED_GRADIENT_INF = 1.0e-4
MAX_FINAL_OVER_INITIAL_COST = 1.0e-4


class TrialRejected(Exception):
    """The caller rejected a trial for physical admissibility or schema rules."""


def _cost(record: Mapping[str, Any]) -> float:
    try:
        value = float(record["J"])
    except (KeyError, TypeError, ValueError) as exc:
        raise ValueError("cost callback record must contain scalar J") from exc
    if not np.isfinite(value) or value < 0.0:
        raise ValueError("objective J must be finite and nonnegative")
    return value


def _validate_box(x0: np.ndarray, lower: np.ndarray, upper: np.ndarray) -> None:
    if x0.shape != (2,) or lower.shape != (2,) or upper.shape != (2,):
        raise ValueError("x0 and box bounds must each have shape (2,)")
    if not (np.isfinite(x0).all() and np.isfinite(lower).all() and np.isfinite(upper).all()):
        raise ValueError("x0 and box bounds must be finite")
    if np.any(lower >= upper) or np.any(x0 < lower) or np.any(x0 > upper):
        raise ValueError("box must be ordered and contain x0")


def e0_zero_innovation_pass(j: float, gradient: np.ndarray,
                            update_inf: float) -> bool:
    """Absolute E0 gate; a passing zero-innovation point needs no optimizer step."""
    g = np.asarray(gradient, dtype=np.float64)
    return bool(g.shape == (2,) and np.isfinite(j) and np.isfinite(g).all()
                and np.isfinite(update_inf)
                and j <= 1.0e-12 and np.linalg.norm(g, ord=np.inf) <= 1.0e-8
                and update_inf <= 1.0e-8)


def minimize(cost: Callable[[np.ndarray], Mapping[str, Any]],
             gradient: Callable[[Mapping[str, Any]], np.ndarray],
             h0: np.ndarray, x0: np.ndarray | None = None,
             lower: np.ndarray | None = None, upper: np.ndarray | None = None,
             e0_zero_innovation: bool = False,
             max_updates: int = MAX_ACCEPTED_UPDATES,
             max_cost_forwards: int = MAX_OBJECTIVE_FORWARDS,
             max_gradient_calls: int | None = None,
             max_backtracks: int = MAX_BACKTRACKS) -> dict[str, Any]:
    """Run bounded inverse-BFGS with projected Armijo trials and a hard ledger.

    A clipped trial uses its actual displacement in the Armijo directional
    term. Cost-only rejected trials never call ``gradient``. An accepted trial
    is differentiated from its same opaque record, with no repeated forward.
    The caller supplies the pre-frozen SPD H0 (recommended:
    ``inv(A_0.01.T @ A_0.01)`` after both-width rank checks). Reaching both
    scientific convergence conditions is required for success.
    """
    x = np.zeros(2, dtype=np.float64) if x0 is None else np.asarray(x0, dtype=np.float64).copy()
    lo = np.array([-1.0, -1.0], dtype=np.float64) if lower is None else np.asarray(lower, dtype=np.float64)
    hi = np.array([1.0, 1.0], dtype=np.float64) if upper is None else np.asarray(upper, dtype=np.float64)
    _validate_box(x, lo, hi)
    min_updates = 0 if e0_zero_innovation else 1
    if not (min_updates <= max_updates <= MAX_ACCEPTED_UPDATES):
        raise ValueError("max_updates must be in [0, 12] for E0 or [1, 12] for E1")
    if max_gradient_calls is None:
        max_gradient_calls = min(MAX_GRADIENT_CALLS, max_updates + 1)
    if not (1 <= max_cost_forwards <= MAX_OBJECTIVE_FORWARDS):
        raise ValueError("max_cost_forwards must be in [1, 40]")
    if not (1 <= max_gradient_calls <= min(MAX_GRADIENT_CALLS, max_updates + 1)):
        raise ValueError("max_gradient_calls must not exceed accepted updates plus one")
    if not (1 <= max_backtracks <= MAX_BACKTRACKS):
        raise ValueError("max_backtracks must be in [1, 4]")
    H0 = np.asarray(h0, dtype=np.float64).copy()
    if H0.shape != (2, 2) or not np.isfinite(H0).all():
        raise ValueError("h0 must be a finite 2x2 inverse metric")
    hscale = max(1.0, float(np.linalg.norm(H0, ord=np.inf)))
    if float(np.linalg.norm(H0 - H0.T, ord=np.inf)) > 1.0e-12 * hscale:
        raise ValueError("h0 must be symmetric positive definite")
    try:
        np.linalg.cholesky(0.5 * (H0 + H0.T))
    except np.linalg.LinAlgError as exc:
        raise ValueError("h0 must be symmetric positive definite") from exc
    H = H0.copy()

    calls: list[dict[str, Any]] = []
    trace: list[dict[str, Any]] = []
    ncost = 0
    ngrad = 0

    def do_cost(point: np.ndarray, label: str) -> tuple[Mapping[str, Any] | None, str | None]:
        nonlocal ncost
        if ncost >= max_cost_forwards:
            return None, "objective_forward_cap"
        ncost += 1  # Count attempted model calls too, including rejected/failing trials.
        try:
            record = cost(point.copy())
            if not isinstance(record, Mapping):
                raise ValueError("cost callback must return a mapping record")
            J = _cost(record)
        except TrialRejected as exc:
            calls.append({"label": label, "kind": "cost", "control": point.tolist(),
                          "accepted": False, "reason": str(exc)})
            return None, "trial_rejected"
        except Exception as exc:
            calls.append({"label": label, "kind": "cost", "control": point.tolist(),
                          "accepted": False, "reason": f"{type(exc).__name__}: {exc}"})
            return None, "cost_callback_error"
        calls.append({"label": label, "kind": "cost", "control": point.tolist(),
                      "J": J, "accepted": True})
        return record, None

    def do_gradient(record: Mapping[str, Any], point: np.ndarray,
                    label: str) -> tuple[np.ndarray | None, str | None]:
        nonlocal ngrad
        if ngrad >= max_gradient_calls:
            return None, "gradient_call_cap"
        ngrad += 1  # Gradient consumes the existing cost/trajectory record only.
        try:
            g = np.asarray(gradient(record), dtype=np.float64)
            if g.shape != (2,) or not np.isfinite(g).all():
                raise ValueError("gradient callback must return a finite length-2 vector")
        except Exception as exc:
            calls.append({"label": label, "kind": "gradient", "control": point.tolist(),
                          "accepted": False, "reason": f"{type(exc).__name__}: {exc}"})
            return None, "gradient_callback_error"
        g = g.copy()
        calls.append({"label": label, "kind": "gradient", "control": point.tolist(),
                      "gradient": g.tolist(), "reuses_cost_record": True})
        return g, None

    record, failure = do_cost(x, "initial")
    if failure or record is None:
        return _result("incomplete_initial_cost", x, None, None, None, None,
                       ncost, ngrad, calls, trace, max_cost_forwards, max_gradient_calls)
    J0 = _cost(record)
    if J0 <= 0.0 and not e0_zero_innovation:
        return _result("unresolved_nonpositive_initial_cost", x, J0, None, None,
                       None, ncost, ngrad, calls, trace,
                       max_cost_forwards, max_gradient_calls, terminal_J=J0)
    g, failure = do_gradient(record, x, "gradient_initial")
    if failure or g is None:
        return _result("incomplete_initial_gradient", x, J0, None, 1.0,
                       None, ncost, ngrad, calls, trace,
                       max_cost_forwards, max_gradient_calls, terminal_J=J0)
    g0_inf = float(np.linalg.norm(g, ord=np.inf))
    if e0_zero_innovation:
        passed = e0_zero_innovation_pass(J0, g, 0.0)
        return _result("converged_zero_innovation" if passed else "failed_zero_innovation",
                       x, J0, g, None, None, ncost, ngrad, calls, trace,
                       max_cost_forwards, max_gradient_calls,
                       initial_gradient_inf=g0_inf, accepted_updates=0,
                       e0_gate_passed=passed, terminal_J=J0)
    grad_scale = max(1.0, g0_inf)
    updates = 0

    def converged(current_J: float, current_g: np.ndarray) -> tuple[bool, float, float]:
        ratio = current_J / J0
        normalized = float(np.linalg.norm(current_g, ord=np.inf)) / grad_scale
        return (ratio <= MAX_FINAL_OVER_INITIAL_COST and
                normalized <= MAX_NORMALIZED_GRADIENT_INF, ratio, normalized)

    ok, ratio, normalized = converged(J0, g)
    if ok:
        return _result("converged", x, J0, g, ratio, normalized, ncost, ngrad,
                       calls, trace, max_cost_forwards, max_gradient_calls,
                       initial_gradient_inf=g0_inf, accepted_updates=0,
                       terminal_J=J0)

    status = "incomplete_update_limit"
    while updates < max_updates:
        if ngrad >= max_gradient_calls:
            status = "incomplete_gradient_call_cap"
            break
        direction = -(H @ g)
        slope = float(g @ direction)
        reset = False
        if not np.isfinite(direction).all() or not np.isfinite(slope) or slope >= 0.0:
            H = H0.copy()
            direction = -(H @ g)
            slope = float(g @ direction)
            reset = True
        if not np.isfinite(direction).all() or not np.isfinite(slope) or slope >= 0.0:
            status = "incomplete_non_descent_direction"
            break

        accepted_record = None
        accepted_point = None
        accepted_alpha = None
        update_trace = {"update": updates + 1, "control": x.tolist(),
                        "J": _cost(record), "gradient": g.tolist(),
                        "direction": direction.tolist(), "slope": slope,
                        "reset_to_h0": reset, "trials": []}
        for trial in range(max_backtracks):
            alpha = INITIAL_STEP * STEP_SHRINK**trial
            raw = x + alpha * direction
            candidate = np.clip(raw, lo, hi)
            displacement = candidate - x
            trial_slope = float(g @ displacement)
            item = {"alpha": float(alpha), "raw_control": raw.tolist(),
                    "control": candidate.tolist(), "displacement": displacement.tolist(),
                    "projected_slope": trial_slope}
            if np.array_equal(candidate, x) or not np.isfinite(trial_slope) or trial_slope >= 0.0:
                item.update(accepted=False, reason="no_projected_descent")
                update_trace["trials"].append(item)
                continue
            trial_record, trial_failure = do_cost(candidate, f"trial_u{updates}_b{trial}")
            if trial_failure:
                item.update(accepted=False, reason=trial_failure)
                update_trace["trials"].append(item)
                if trial_failure in ("objective_forward_cap", "cost_callback_error"):
                    status = f"incomplete_{trial_failure}"
                    break
                continue
            trial_J = _cost(trial_record)
            armijo_bound = _cost(record) + ARMIJO_RHO * trial_slope
            take = trial_J < _cost(record) and trial_J <= armijo_bound
            item.update(J=trial_J, armijo_bound=armijo_bound,
                        armijo_accepted=bool(take))
            update_trace["trials"].append(item)
            if take:
                accepted_record, accepted_point, accepted_alpha = trial_record, candidate, alpha
                break
        trace.append(update_trace)
        if status.startswith("incomplete_") and status != "incomplete_update_limit":
            break
        if accepted_record is None or accepted_point is None:
            status = "incomplete_line_search_exhausted"
            break
        # Differentiate the accepted trial's exact record; this is not another cost forward.
        g_new, failure = do_gradient(accepted_record, accepted_point,
                                     f"gradient_u{updates + 1}")
        if failure or g_new is None:
            status = "incomplete_gradient_call_cap" if failure == "gradient_call_cap" else "incomplete_gradient_callback"
            update_trace["decision"] = "armijo_trial_without_usable_gradient"
            break
        s = accepted_point - x
        y = g_new - g
        H, bfgs = safe_bfgs_inverse_update(H, s, y)
        updates += 1
        x, record, g = accepted_point, accepted_record, g_new
        update_trace.update(accepted=True, accepted_alpha=accepted_alpha,
                            decision="accepted",
                            accepted_control=x.tolist(), accepted_J=_cost(record),
                            bfgs_update=bfgs)
        ok, ratio, normalized = converged(_cost(record), g)
        if ok:
            status = "converged"
            break

    final_J = _cost(record)
    ratio = final_J / J0
    normalized = float(np.linalg.norm(g, ord=np.inf)) / grad_scale
    if status == "incomplete_update_limit" and updates >= max_updates:
        status = "incomplete_update_limit"
    return _result(status, x, J0, g, ratio, normalized, ncost, ngrad, calls,
                   trace, max_cost_forwards, max_gradient_calls,
                   initial_gradient_inf=g0_inf, accepted_updates=updates,
                   terminal_J=final_J)


def _result(status: str, x: np.ndarray, initial_J: float | None,
            gradient: np.ndarray | None, cost_ratio: float | None,
            normalized_gradient_inf: float | None, objective_forwards: int,
            gradient_calls: int, calls: list[dict[str, Any]],
            trace: list[dict[str, Any]], cost_cap: int, gradient_cap: int,
            **extra: Any) -> dict[str, Any]:
    return {"status": status, "converged": status == "converged",
            "control": np.asarray(x, dtype=np.float64).tolist(),
            "initial_J": initial_J,
            "terminal_J": extra.pop("terminal_J", None),
            "gradient": None if gradient is None else np.asarray(gradient, dtype=np.float64).tolist(),
            "cost_ratio": cost_ratio,
            "normalized_gradient_inf": normalized_gradient_inf,
            "objective_forwards": objective_forwards,
            "objective_forward_cap": cost_cap,
            "gradient_calls": gradient_calls,
            "gradient_call_cap": gradient_cap,
            "accepted_updates": extra.pop("accepted_updates", 0),
            "calls": calls, "updates": trace, **extra}


def _self_test() -> None:
    # Exact convex quadratic with an interior minimizer and a supplied inverse
    # Hessian. The optimizer sees callbacks and a metric, never the minimizer.
    Q = np.array([[4.0, 1.0], [1.0, 2.0]], dtype=np.float64)
    target = np.array([0.35, -0.25], dtype=np.float64)
    counters = {"cost": 0, "gradient": 0}

    def cost(a: np.ndarray) -> dict[str, Any]:
        counters["cost"] += 1
        d = np.asarray(a, dtype=np.float64) - target
        return {"J": 0.5 * float(d @ Q @ d), "a": np.asarray(a).copy()}

    def gradient(record: Mapping[str, Any]) -> np.ndarray:
        counters["gradient"] += 1
        d = np.asarray(record["a"], dtype=np.float64) - target
        return Q @ d

    result = minimize(cost, gradient, np.linalg.inv(Q))
    assert result["status"] == "converged", result
    assert np.linalg.norm(np.asarray(result["control"]) - target, ord=np.inf) < 1.0e-8
    assert result["objective_forwards"] == counters["cost"]
    assert result["gradient_calls"] == counters["gradient"] <= 13
    assert result["gradient_calls"] == result["accepted_updates"] + 1
    assert result["objective_forwards"] <= 40
    assert result["cost_ratio"] <= 1.0e-4
    assert result["normalized_gradient_inf"] <= 1.0e-4

    # A truth-centered zero innovation is evaluated and gated without invoking
    # an optimizer update, so J=0 never enters an undefined J/J0 ratio.
    assert e0_zero_innovation_pass(0.0, np.zeros(2), 0.0)
    assert not e0_zero_innovation_pass(1.0, np.zeros(2), 0.0)

    # E0 must request a real gradient even when J is exactly zero. A bad
    # nonzero gradient at zero cost fails the gate and never starts an update.
    e0_counts = {"cost": 0, "gradient": 0}

    def e0_cost(_: np.ndarray) -> dict[str, float]:
        e0_counts["cost"] += 1
        return {"J": 0.0}

    def e0_bad_gradient(_: Mapping[str, Any]) -> np.ndarray:
        e0_counts["gradient"] += 1
        return np.array([2.0e-8, 0.0])

    def e0_good_gradient(_: Mapping[str, Any]) -> np.ndarray:
        e0_counts["gradient"] += 1
        return np.zeros(2)

    e0_pass = minimize(e0_cost, e0_good_gradient, np.eye(2),
                       e0_zero_innovation=True, max_updates=0,
                       max_cost_forwards=1, max_gradient_calls=1)
    assert e0_pass["status"] == "converged_zero_innovation"
    assert e0_pass["e0_gate_passed"] is True and e0_pass["accepted_updates"] == 0
    assert e0_counts == {"cost": 1, "gradient": 1}
    e0_counts = {"cost": 0, "gradient": 0}
    e0_result = minimize(e0_cost, e0_bad_gradient, np.eye(2),
                         e0_zero_innovation=True, max_updates=0,
                         max_cost_forwards=1, max_gradient_calls=1)
    assert e0_counts == {"cost": 1, "gradient": 1}
    assert e0_result["status"] == "failed_zero_innovation"
    assert e0_result["accepted_updates"] == 0

    # Box projection uses actual displacement in Armijo. Once the constrained
    # point is reached, zero projected steps are skipped rather than accepted.
    outside = np.array([2.0, 0.0])

    def box_cost(a: np.ndarray) -> dict[str, Any]:
        d = np.asarray(a, dtype=np.float64) - outside
        return {"J": 0.5 * float(d @ d), "a": np.asarray(a).copy()}

    def box_grad(record: Mapping[str, Any]) -> np.ndarray:
        return np.asarray(record["a"], dtype=np.float64) - outside

    box_result = minimize(box_cost, box_grad, np.eye(2), max_updates=2)
    assert np.all(np.asarray(box_result["control"]) <= 1.0)
    assert np.all(np.asarray(box_result["control"]) >= -1.0)
    assert box_result["status"] == "incomplete_non_descent_direction" or box_result["status"] == "incomplete_line_search_exhausted", box_result

    cost_calls_before_cap = counters["cost"]
    capped = minimize(cost, gradient, np.linalg.inv(Q), max_cost_forwards=1)
    assert capped["status"] == "incomplete_objective_forward_cap", capped
    assert capped["objective_forwards"] == counters["cost"] - cost_calls_before_cap == 1
    assert capped["gradient_calls"] == 1


def main() -> int:
    _self_test()
    print("native twin minimizer self-test: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
