# AD-SDIRK3 Repository Guidelines

## Project Scope
- Improve WRF SDIRK3 convergence, runtime, forecast quality, and data-assimilation compatibility with minimal changes; target longer stable timesteps than split-explicit RK3.
- Fortran integration lives in `dyn_em/`; the C++ solver, AD code, and standalone tests live in `external/libtorch_wrf/sdirk3/`. The required WRF validation case is `test/em_b_wave`.
- Follow `external/libtorch_wrf/sdirk3/README.md` for build and test entry points. Select configuration for the installed compiler, MPI, netCDF, and libtorch; match MPI ranks to the namelist decomposition.

## Implementation
- Code must be simple, clear, concise, and intuitive.
- Think mathematically and apply numerical analysis before changing code; also check engineering constraints.
- Prioritize theoretical consistency over symptom-level patches. Establish the root cause and check equations, invariants, units, scaling, stagger locations, boundary conditions, and implementation contracts before choosing a fix.
- Choose the smallest change that satisfies theoretical consistency. Keep shared formulas and decisions in one authoritative implementation; do not add unnecessary abstractions, duplication, wrappers, options, or validation layers.
- Follow surrounding Fortran/C++ conventions. Edit authoritative sources (`*.F` where preprocessing is used), not generated files; keep helpers with the owning component.
- Prioritize Stage 3 convergence and stability. Preserve stationarity, stagger boundary rules, and fallback safety; keep INN warm-start default-off with gates and fallback.
- Keep JVP mode selection solver-local; do not mutate global configuration during a solve.
- All new options must be default-off and regression-neutral. Wire each new config key through env parsing, namelist parsing, `validate()`, effective print, and runtime setters.

## Validation
- Cleanly rebuild affected components after ABI changes; use matching headers and libraries.
- Validate affected behavior in `test/em_b_wave`. Compare forecast fields and runtime against the archived split-explicit RK3 reference on the same setup; reuse the existing reference run.
- Check regression parity with new options off, no NaN/Inf, and expected GMRES/log trends with features enabled. Promote features only with demonstrated numerical correctness, safety, performance, and reproducibility.
- Tie evidence to exact source and executable revisions, inputs, and compiler/MPI stack. Preserve archived results and rerun affected checks after changes.
- Reuse existing investigation and validation evidence. Mark progress or completion only when sufficient evidence meets the stated scope; keep unsupported claims open and fill only the missing evidence.

## Team Work
- Actively use sub-agents to reduce total token use. Organize sub-agent work into Green and Red teams using `gpt-5.6-luna` with `reasoning_effort=high`.
- Delegate bounded independent tasks without a fixed two-agent cap, reuse agents and verified evidence, and request concise reports. Avoid duplicate investigation, repeated context, and tests unaffected by changes.
- At the end of each session, have the Green and Red sub-agent teams review the work and its validation evidence before closing the session.

## Documentation
- Keep only durable project principles in `AGENTS.md`; record experiment details and progress in `docs/`.
- Name progress reports `(YYYYMMDDHHMM)_<topic>_<summary>.md`, with an exact local timestamp in the header. Include context, changes, validation status, and next actions; use the latest entries to resume work.
- Include same-setup RK3 field/runtime comparisons. If no model run or comparison was performed, state that explicitly.
