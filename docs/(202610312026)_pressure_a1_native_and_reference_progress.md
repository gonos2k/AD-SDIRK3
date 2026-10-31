# Pressure-Schur A1 progress

Local timestamp: 2026-10-31T20:26:12.696084+09:00.
Native A1 source70f37dfcd5020d6c7d0263e66973376cfd3913d6, executable SHA
2792161cc857818f5a9cdff5663d6534cd286c625a4b504c51c0298133f1e601.

One registered native600s step completed with rc0, stages2/3/4 below1e-9,
wrapper3/0 and174numeric endpoint arrays bit-identical to the be661 OFF control.
The probe makes no extra original RHS/JVP call: it clones the actual original
EOS inputs once and reports saved native FGMRES residuals explicitly as saved.
Both EOS primal outputs were bit equal. Maximum EOS AD relative difference
was2.873e-16 and the largest best-width FD relative error8.635e-10.
Projection deltas for captured Phi/T/mu versus U_eval were zero in this case.
Imported raw-P ALB differs from the live EOS ALB by6.175e-8 relative; raw K/B
coefficient discrepancies are about5.5e-8, consistent with documented precision.
These are observations, not evidence that raw P is the full Jacobian.

The physical/scaled saved native residual ratios were3.726e-9/1.653e-8.
They are unmasked/unweighted explicit residuals returned by FGMRES, not a new
independent evaluation or an offline replay fixture. Instrumented timing is
ineligible; no speedup is inferred. Rank peak RSS2,022,555,648 bytes and both
disk-floor checks passed. Red independently re-parsed logs, pins and174 arrays.
No new RK3 integration was performed; same-setup native fields match their
existing ARK control, with RK3 references preserved for Gates B/C.

The separate reference scalar pressure solver and full mixed P0 were implemented
and tested, including dynamic top and Phi0/complement semantics. They use FFT-X,
a cached dense DCT-II Y reference and FP64 per-mode Thomas factors; the dense
Y transform is not a scalability result. The mixed-block test reports dense
relative5.58e-16 and illustrative600s scaled reconstruction8.04e-13. A pure
packed mapping test checks1,160 native units, physical core, seam/wall/top,
Phi0 coupling and general S. These are model/component tests, not native P0
performance or exact A=P0. Native C/Omega/PGF omissions are documented.

The next item is A2: two independently initialized native processes with common
source/input/operator and a before-M preflight, changing only the first physical
preconditioner action. Its source is under review and no A2 model run has begun.
Prepared-JVP-only expansion, new split modes and five-day runs remain stopped.
See [the active checklist](<(202610311826)_pressure_schur_checklist.md>).

## A2 빌드 전 소스 고정

2026-10-31T20:32:02.340746+09:00 기준 reference P0, packed 어댑터, native profile factory와 첫 FGMRES 연구용 연결을 통합했다. Green/Red 소스 검토는 완료했다. 실제 잔차의 적용 노름이 허용오차를 충족해야 비교 적격으로 표시한다. 세 reference/배열 집중 검사는 통과했으나 전체29개 C++ 재빌드와 A2 native 대조는 아직 미수행이다. 일반 빌드에서는 두 연구 macro가 없고 새로운 namelist 옵션은 없다.

A2 사전규약은 두 독립 초기화 과정의 기존/압력 전처리 각1회, h600 한 스텝, 재시도 없음이다. 기준 과정의 문맥 영수증을 외부 SHA256으로 고정한 뒤 후보의 첫 전처리 적용 전에 일치를 확인한다. 준비 포함 첫점 비용2배 개선과 원래 잔차·배열·자원 조건을 모두 만족할 때만 후속 연속 스텝을 검토한다. 현 단계에서는 RK3 대비 가속이나 장시간 잡음 해결을 주장하지 않는다.
