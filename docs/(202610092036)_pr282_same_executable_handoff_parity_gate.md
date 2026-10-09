# Bind exact handoff parity to the current executable

Local timestamp: 2026-10-09T20:36:38.990596+09:00.

Run 37923014265 stopped after the 120×2.5 bounded call; no 960-step call ran. Its artifact SHA-256 is beb3d2e9a2c203f14a0aa24627c034783cda07c989687bc51b21f85c67b324b0. The new production binary differs from the old-source artifact in checkpoint maxima by 1.27e-11/2.91e-11 and prediction RMS by 4.68e-14/1.19e-13 m/s at 150/300 s; objective difference is -4.60e-9. Inputs, observations, brackets and context match exactly. New bounded RSS is 161580 KiB. These observations do not prove a numerical defect, a benign cause, or old-source bit identity. The failed historical comparison is preserved.

The purpose of the gate is to establish exact full-trajectory versus one-step handoff semantics for the code being measured. Both paths must therefore use the same current executable. The wrapper now runs full-retained 120×2.5 with --forward-only (no VJP), then bounded 120×2.5 using identical state, data, context and N14/K12. Initial arrays, both checkpoints, predictions, observations, brackets and objective must match exactly before a fresh descriptor and the selected long call. No numerical tolerance replaces np.array_equal. The historical-source result is reported separately, not counted as a passed gate or relabeled current evidence. No observations are regenerated.

Green mocked the sequence and failure-stop behavior; Red reviewed the exact same-executable contract. Explicit retained FP64/carry/graph metadata and absent pullback are checked. The 0.1 sigma time diagnostic remains unchanged and does not certify a stationary point. Full required CI 37923001929 continues on d2ed2d6, whose production/header sources are unchanged by this wrapper adjustment.

Next: one corrected h=0.3125 diagnostic on the separate diagnostic branch. No production equation/ABI change, optimizer rerun, new WRF/RK3 comparison or merge occurred for this adjustment. Prior fresh memory-fix WRF evidence remains qualified.
