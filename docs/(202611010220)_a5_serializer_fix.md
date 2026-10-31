# A5 기록기 오류와 새 소스 검증 범위

작성: 2026-11-01T02:20:11.302510+09:00.

fe1afb78의 사전등록 native 한 스텝은 종료0·세 단계·원래 선형계 수렴을 완료했으나
A5 constants Tensor 초기화에 float/double이 섞여 기록 직전에 예외가 발생했다.
실제 V-primary 기여를 측정하기 전이므로 기존 결과는 INCONCLUSIVE로 보존한다.
174개 숫자 끝값과 첫 방향v/Az byte는 A4 B0와 같음을 별도 후처리로 확인했다.
원래 결과·사전규약·pin·기준 파일은 수정하지 않았다.

수정은 constants의 명시적 std::vector<double> 변환뿐이다. 물리식·참조연산자·기여율
기준·AD/FD 폭과 허용오차·600초 간격은 바꾸지 않는다. 평가 함수만 부르던 집중검사를
실제 raw-bound P0의 run() 및 두 파일 직렬화/역독해까지 확장해 같은 실패를 검출한다.
9개 상수의 순서·FP64 값, capture/decomposition/observed 출력, 파일 overwrite와 tmp
처리도 검사한다. 이 문서 시점의 확장 검사는 syntax만 통과했고 runtime은 아직 미수행이다.

이후 새 소스만 helper 한 파일을 재빌드하고 나머지29개 객체·모든 header의 일치를
확인한다. 확장 집중검사가 통과한 뒤 새 소스의 별도 사전등록 한 스텝으로 검증한다.
옛 binary의 재시도나 A4 비용시험 반복은 하지 않으며, 데이터 불완전은 여전히
INCONCLUSIVE이고 검증된 기여율이 기준을 실패할 때만 primary 가설을 NO-GO로 닫는다.
새 모델 실행이나 RK3 비교 실행은 이 문서 작성 시 수행하지 않았다.
