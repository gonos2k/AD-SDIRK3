# PR308의 작은 재계산 자료

같은 e71 입력·PR307 실행파일의 0–15초 ARK 궤적이다. 기본 쌍은 `h=0.9375/0.46875 s`, 별도 대조는 같은 미세 간격에서 Newton 기준만 `1e-7→1e-9`로 바꿨다. 생산 수치 소스는 변경하지 않았다.

- `fine_pair_w_trajectory_analysis.npz`: 17개 coarse·33개 fine의 실제 수용 FP64 W 끝값, 고정 M0/e1, 원 상태·raw/projected 구분.
- `newton_control_trajectories.npz`: 같은 baseline 배열과 33개 허용오차 대조 W 끝값. 배열 축은 `[시각,k,j,i]`, 물리 W 소유면은 16³개다. 고정 하단을 제외하고 동적 상단을 포함한다.
- 두 분석 JSON/CSV: 같은 시각의 차이·고정 방향 투영, 단계별 잔차와 압력/부력 요약. 직접 잔차 기여의 합은 전파오차 상한이 아니다.
- protocol·basis·run receipt·Graphify receipt·독립 검산: 실행 전 고정한 정책과 실제 실행·분석의 해시 연결.
- 두 PNG: baseline 궤적과 별도 Newton 대조. 주파수·차수·참오차를 추정하지 않는다.

이 폴더의 NPZ만 있으면 NumPy로 주요 W 차이를 재계산할 수 있다. 저장소 루트에서 다음을 실행한다.

```sh
python3 - <<'PYCODE'
from pathlib import Path
import numpy as np
p = Path('docs/evidence/pr308_fine_pair_w')
b = np.load(p / 'fine_pair_w_trajectory_analysis.npz')
c = np.load(p / 'newton_control_trajectories.npz')
rms = lambda x: np.sqrt(np.mean(x*x, axis=(1, 2, 3)))
d0 = b['fine_w_projected_m_s'][::2] - b['coarse_w_projected_m_s']
d1 = c['W_control_projected'][::2] - c['W_coarse_projected']
for j in (1, 16):
    print(j * 15/16, rms(d0)[j], rms(d1)[j])
PYCODE
```

기대하는 두 행의 `(시각, 기본 쌍 RMS, Newton 대조−coarse RMS)`는 다음과 같다.

```text
0.9375  2.488718515025602e-7  3.2138305034026466e-8
15.0    2.705154689949601e-7  4.6656761167328086e-7
```

`compact_recomputation_receipt.json`은 이 재계산의 실제 결과다. NPZ와 JSON에는 결과를 보기 전에 고정한 영역·방향을 그대로 썼다. 기본 쌍과 대조 쌍의 차이가 작다는 사실은 60–240초 기준해 합격을 뜻하지 않는다. PR306의 20/24, W 네 시각 실패는 유지한다.

전체 80개 단계 CSV·RSL·NetCDF·namelist와 실행 스크립트는 외부 `fine-pair-w-trajectory-20261015` 근거 폴더 및 별도 무손실 원자료 묶음에 있다. 전체 단계 분석기를 재실행하려면 해당 원자료를 복원하고, 계획/receipt의 로컬 경로를 복원 위치로 맞춰야 한다. 경로 변경은 bytes 해시 고정과 구분한다. 이 폴더의 통계 재계산에는 netCDF4·libtorch·WRF 실행파일이 필요하지 않다.
