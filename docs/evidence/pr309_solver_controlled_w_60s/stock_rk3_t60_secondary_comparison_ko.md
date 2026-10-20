# 60초 stock RK3 비교 — 보조 결과

같은 e71 초기장·16³ 격자에서 보존된 stock RK3 h=1.875 실행의 실제 t=60초 프레임과 strict native ARK 60초 실행 둘을 V3 소유점 마스크로 비교했다. RK3 t=60은 t0 netCDF 파일의 두 번째 frame으로 저장되어 있어 보간하지 않았다.

| strict ARK dt (s) | U RMS | V RMS | W RMS | PH RMS | THM (T+300) RMS | MU+MUB RMS |
|---:|---:|---:|---:|---:|---:|---:|
| 0.9375 | 0.00103873 | 0.000434098 | 5.44902e-05 | 0.00393139 | 4.26096e-05 | 0.0124065 |
| 0.46875 | 0.00103873 | 0.000434098 | 5.48206e-05 | 0.00393375 | 4.26096e-05 | 0.0124065 |

이것은 archived stock RK3에 대한 보조 거리만 뜻한다. RK3는 `smdiv=0.1`, `emdiv=0.01`, `epssm=0.1`을 쓰는 split-explicit 경로이고, native ARK mode 3는 다른 발산 감쇠 경로를 사용한다. 따라서 이 비교는 native ARK 기준해 자격, 참오차, 또는 정확도 원인을 판정하지 않는다.

기존 archived RK3의 source revision은 run receipt에 기록되지 않았다. 실행파일 SHA와 초기 입력/nml/netCDF endpoint SHA는 JSON에 묶었다. 비교 대상은 60초 endpoint만이며, observer가 켜진 native run과 240초 archived wall timing을 나누어 효율 비율을 계산하지 않았다.

세부 hash, 모든 field RMS/max/상대신호 값, mask/count, input/grid/runtime bindings는 동봉 JSON을 참조한다.
