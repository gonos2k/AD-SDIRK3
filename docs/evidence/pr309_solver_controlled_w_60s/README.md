# PR309: 60초 여섯 필드와 W 궤적 재계산

두 실행은 같은 초기장·실행파일에서 `h=0.9375/0.46875 s`, Newton 절대 기준 `1e-9`로 60초까지 연속 적분했다. 이 폴더는 원자료에서 계산한 작은 배열과 실행·분석 연결 기록이다. 생산 코드나 기본 허용오차를 변경하지 않았다.

- `t60_published_fields.npz` (약 152 kB): 공식 60초 검사에 쓴 여섯 필드. 원래 FP32 NetCDF를 FP64로 승격한 값이며 T는 `T+300 K`, MU는 `MU+MUB Pa`다. 고정 V3 소유 영역을 적용했다.
- `compact_w_trajectories_and_t60_fields.npz` (약 6.3 MB): coarse 65개·fine 129개 실제 수용 FP64 W 끝값, 고정 M0/e1, 같은 60초 필드. W 축은 `[시각,k,j,i]`, 각각 16³개 소유면이다. 하단 고정면을 제외하고 동적 상단을 포함한다.
- `official_t60_screen.json`: 고정 예산과 실제 여섯 판정. T/MU의 0은 공개 출력에서의 차이다.
- `common_time_w_65.csv`, `accepted_step_solver_summary.csv`: 65개 정확한 공통 시각과 192개 수용 스텝의 작은 수치 요약. Newton check count 2는 최초 검사와 한 번의 보정이다.
- `numerical_summary.json`: 독립 Red 분석에서 필요한 범위만 발췌한 기록. 전체 단계 JSON·raw/projected 배열·실험용 분석 스크립트는 외부 검토 묶음에 보존했다.
- `compact_array_derivation_receipt.json`, `compact_recomputation_receipt.json`: 원 배열의 값 보존, 재계산 및 두 실행의 다섯 W 공개시각 연결 검산.

저장소 루트에서 NumPy만으로 60초의 여섯 차이를 재계산할 수 있다.

```sh
python3 - <<'PYCODE'
from pathlib import Path
import numpy as np
p = Path('docs/evidence/pr309_solver_controlled_w_60s')
z = np.load(p / 't60_published_fields.npz', allow_pickle=False)
for field in ('U', 'V', 'W', 'PH', 'T', 'MU'):
    d = z[f't60_{field}_fine'] - z[f't60_{field}_coarse']
    print(field, d.size, np.sqrt(np.mean(d*d)))
PYCODE
```

기대 RMS는 U `2.33502123e-8`, V `5.2800000e-9`, W `1.79844365e-6`, PH `1.02890808e-4`, T `0`, MU `0`이다. 단위는 각각 m/s, m/s, m/s, m²/s², K, Pa다.

전체 내부 W 궤적의 공통 시각은 `j×15/16 s`이며 fine의 `2j`번째 끝값과 대응한다.

```sh
python3 - <<'PYCODE'
from pathlib import Path
import numpy as np
z = np.load(Path('docs/evidence/pr309_solver_controlled_w_60s') /
            'compact_w_trajectories_and_t60_fields.npz', allow_pickle=False)
d = z['fine_W_projected_m_s'][::2] - z['coarse_W_projected_m_s']
r = np.sqrt(np.mean(d*d, axis=(1, 2, 3)))
j = int(r.argmax())
print('최대:', j*15/16, r[j], '내부 60초:', r[-1])
PYCODE
```

기대값은 최대 `56.25 s, 1.96831049217e-6 m/s`, 내부 60초 `1.79844384225e-6 m/s`다. 공개 FP32 60초 W 통계와 내부 FP64 W 통계를 혼동하지 않는다. 두 실행의 내부 W를 FP32로 내리면 0·15·30·45·60초의 실제 공개 W와 모두 비트 단위로 일치한다.

이번 결과는 **60초 6개 검사**만 충족한다. 새 strict 계열의 120·180·240초나 전체 기준해 적격성을 뜻하지 않는다. 기존 Newton `1e-7` 계열의 20/24 기록은 별도 실험으로 보존한다. 직접 단계 잔차 기여의 합은 전파된 상태오차 상한이 아니다.

실제 원자료 192개 CSV는 5.77 GB이며 무손실 zstd 압축과 원래 SHA 복원을 확인했다. `external_package_receipt.json`에는 coarse/fine 원자료 ZIP 및 다섯 선택 단계만 담은 작은 검토 ZIP의 경로·해시·검증 결과가 있다. 두 오프라인 분석 스크립트도 그 묶음에 있다. 전체 분석에는 원래 receipt 경로·해시 연결을 복원해야 하며, 이 폴더의 배열 재계산에는 WRF·libtorch·netCDF4가 필요하지 않다.
