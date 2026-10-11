# 비영 Coriolis의 실제 사전검사

작성 시각: 2026-10-11 11:39:20 JST

현재 기준은 병합 main `67ec1ed394a3ae3379c4b30df933c93a9cd19e6c`와 같은 tree인 `9126d906`이다. 별도 worktree에서 시험 C++에만 default-off `--rotating-wave`를 추가했다. 생산 C++·Fortran·ABI·solver 설정은 변경하지 않았다. 실제 f 필드는 FP32 `9.999999747378752e-5`이며 e/sina=0, cosa=1이다.

## 확인한 실행

기존 core archive `d17fe820baf9639e7a911fb26ec2a9dc354924b5a36bfabe482963e07121d3c5`에 현재 시험 CPP `8a9cb7875984304340a9e4e60940d1ff8e86484bf2d2ac434daf2f4f4fc7ef10`만 AppleClang21/Torch2.10 설정으로 컴파일·링크했다. 새 실행파일 SHA-256은 `35abb97801ef1cfd6f8da23ede49988f0719e9008918712f7bc9f4373c605e24`다. 기존 CMake cache가 과거 checkout을 가리키므로 그 cache에서 오래된 시험 소스를 빌드하지 않고 기록된 동일 compiler/link 설정을 사용했다.

- 기본값 descriptor와 16스텝 결과 배열은 이전 실행파일과 bitwise 동일했다.
- f 활성화 16스텝 retained/replay endpoint와 전체 pullback도 정확히 일치했다. 이 첫 활성화 검사는 보관 관측을 interface 입력으로만 사용했으며 회전 truth의 최적화 결과가 아니다.
- 별도의 독립 네 점 보간 산식과 실제 전체 RHS의 `F_f-F_0`를 두 호출·세 진폭에서 비교했다. 최대 상대 L2 차이는 `2.0199530219e-16`, 균일 질량의 회전 일 상쇄 잔차는 `8.7158932076e-18` 이하였다. U의 주기 중복면과 고정 V벽은 일 합산에서 제외했다. 변수 질량의 일반 에너지 중립성을 주장하지 않는다.

그 다음 **새 source 관측·초기장으로 실제 네 호출**의 회전 preflight를 실행했다: descriptor, 16스텝 replay, 두 RHS 방향. 실제 signed f의 min=max와 shape12×16을 확인하고, native rdy FP32 bits를 사용했다. Full-Y source는 484성분이다.

| source/native 진단 | 결과 |
|---|---:|
| f=0 기존 41성분의 invariant embedding 잔차 | 2.2204460493e-15 (roundoff 기준 통과, bitwise exact 아님) |
| 균일 배경의 가중 Coriolis skew 잔차 | 0 |
| 선택 고유주파수 | 0.00257017549849 rad/s |
| 기존 f=0 모드와의 물리 에너지 overlap | 0.99840451946 |
| 관측행렬 H_BG/sigma 특이값 | 176.12626343, 154.16882140 |
| 관측행렬 rank | 2 |
| 원시 plus/minus의 성분별 RHS 비교 | 36개 모두 기존 예산 통과 |
| 가장 큰 실제차/예산 | 약 0.46938 |
| V벽 RHS | 모든 폭에서 정확히 0 |
| source 초기장의 16스텝 full/replay | endpoint와 pullback 정확 일치 |

관측은 독립 선형 source에서 새로 만들었으며 native 결과에 다시 맞추지 않았다. 두 제어는 한 복소 고유모드 q와 iq의 실제 X-phase 진폭·위상이다. 현재 41성분 cos/sin 부분공간은 f≠0에서 닫히지 않으므로 그대로 사용하지 않았다.

## 출처와 완료 범위

Actual preflight receipt는 `/private/tmp/pr289-rotating-preflight-actual/report.json`, SHA-256 `f5f49b31479b177b7a057195864bdd44996d5347a242980249788f06621841cf`다. 이 실행의 driver source는 `becba183...`; 이후 현재 source의 `4f8d53e9...`는 사용하지 않는 import 두 개만 제거한 차이다. 수치 reference의 SHA `f998967f...`와 시험 CPP는 실행 이후 변경하지 않았다. macOS에서는 외부 시험 adapter로 GNU time wrapper만 `/usr/bin/time -l`로 바꿨다. 이 adapter를 native solver 변경 또는 새 플랫폼 지원으로 표현하지 않는다. Time log 파싱 수정은 저장된 로그의 재산술이며 native 호출을 반복하지 않았다.

Independent Red 검산은 원시 CSV·input·B·H·source 재구성으로 36개 비교, 동일 절대 제어좌표 `base+B@[0.25,0.15]`, signed f 및 16스텝 재생을 확인했다. Full controller와 두 forecast schema의 mock도 통과했지만 실제 native 호출에 합산하지 않았다. Graphify의 clean 44-source snapshot은 해시 불일치가 없고 동적/Fortran/header 추출 공백을 기록했다.

- [x] 시험 옵션 off의 배열 parity 및 실제 회전항 활성화.
- [x] 독립 이산 보간·균일 배경의 회전 일과 native 원식 비교.
- [x] 484성분 source 및 실제 RHS·짧은 재생의 새 관성중력파 preflight.
- [ ] 같은 설정의 960스텝 실제 역문제와 fresh gradient 종료.
- [ ] 수용 분석장의 미사용 시각 W·V·전체 상태 및 회전 성분 비교.

이 문서 작성 시점에는 새 회전파 960스텝 VJP·최적화·예측을 실행하지 않았다. 새 WRF em_b_wave 또는 같은 설정 RK3 필드/runtime 비교도 수행하지 않았다. 이전 적합 회귀와 구분하며, 자동 CI는 기존 다섯 CTest·16스텝 replay·metadata 소비 및 문법 검사로 유지한다. 장시간 실행은 수동 연구 진단이다.
