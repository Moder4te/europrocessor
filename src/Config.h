
// ================================================================
//  Config.h — 하드웨어 상수 (단일 진실원)
//  ----------------------------------------------------------------
//  물리 상수는 namespace Cfg. 핀 배치는 보드마다 배선이 달라
//  PinMap.h로 분리 — 부팅 시 칩 MAC으로 자동 선택 (pins().STEP 등).
// ================================================================
#pragma once
#include <Arduino.h>
#include "PinMap.h"   // 핀 배치는 보드별로 PinMap.h에서 (pins().STEP 등)


#ifndef FW_VARIANT
#define FW_VARIANT "unknown"   // platformio.ini build_flags에서 지정 (N16R8 / N8R2)
#endif

namespace Cfg {
    constexpr const char* FW_VERSION = "v4.8.1";

    // 모터 — 마이크로스텝 8 (MS1=LOW, MS2=LOW) → 1600 step/rev
    constexpr int   STEPS_PER_REV = 1600;        // 200 × 8
    constexpr float GEAR_RATIO    = 3.71f;
    constexpr float MAX_OUTPUT_RPM = 80.0f;      // 탈조 한계 기반 상한
    constexpr float MIN_OUTPUT_RPM = 5.0f;       // 실속 방지 하한

    // step/s 환산
    constexpr float MAX_SPEED = MAX_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV; // ≈7915
    constexpr float MIN_SPEED = MIN_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV;
    // S-커브(소프트스타트) 가감속 — 설정 RPM 도달까지 RAMP_SEC초.
    //   FastAccelStepper setLinearAcceleration: 가속도를 0→a로 선형 증가(저크 제한).
    //   handover(선형가속→정가속 전환)를 목표속도의 SCURVE_HANDOVER 지점에 둠.
    //   목표속도 v_t에 대해 per-run 계산:  a = v_t·(1+f)/T,  s_h = f²·v_t·T / (1.5·(1+f))
    constexpr float RAMP_SEC        = 2.0f;      // 0 → 설정RPM 도달 시간(가속·감속 동일)
    constexpr float SCURVE_HANDOVER = 0.5f;      // 저크제한 구간 비율(목표속도의 f까지 소프트스타트)

    // 타이밍
    constexpr uint32_t REST_MS = 2000;           // 방향전환 휴지 (관성 제어 + TMC2209 열관리)

    // 온도센서 유무·결선(2/3/4선)은 보드별 → PinMap.h BOARDS[].rtdWires

    // MAX31865 (RREF 실측 하드코딩)
    constexpr float RREF     = 412.0f;
    constexpr float RNOMINAL = 100.0f;
    constexpr uint16_t TEMP_FAULT_CLEAR_AFTER = 5;  // 연속 fault N회 후만 clear


    // 센티넬 온도값
    constexpr float TEMP_UNREAD = -999.0f;

    // 온도 스파이크 제거 — 수조(수 L)는 1초에 이만큼 못 변함 → 초과치는 노이즈로 버림.
    //   연속 N회 초과면 실제 변화(센서 이동 등)로 보고 새 기준 수용.
    constexpr float   TEMP_MAX_STEP_C         = 2.0f;
    constexpr uint8_t TEMP_SPIKE_ACCEPT_AFTER = 3;
    // 유효 온도 범위 — 현상 수조(실온~40°C대) 기준 넉넉히. 밖이면 센서 오류(fault 0x02)
    constexpr float   TEMP_MIN_VALID_C = -20.0f;
    constexpr float   TEMP_MAX_VALID_C = 100.0f;

    // ── 노이즈 가드 (NoiseGuard) ──
    //   ★실측 보정용 노브★ — 10초마다 [Noise] peak 로그 보고 조정. 정상 조작 피크의 ~3배로.
    //   입력 윈도우(NOISE_WIN_MS) 내 아래 중 하나라도 넘으면 → 패널 입력 차단 INPUT_BLOCK_MS.
    constexpr uint32_t NOISE_WIN_MS         = 500;
    constexpr uint32_t NOISE_ENC_EDGE_MAX   = 2000; // 인코더 ISR 폭주 상한 (Core 0 보호용, 바운스 포함 손조작보다 충분히 큼)
    constexpr uint32_t NOISE_ENC_BAD_MAX    = 60;   // A·B 동시변화(물리적으로 불가능한 전이) — 손조작이 아닐 때만 판정
    //   윈도우 내 한 방향 순이동이 이 전이 수 이상이면 손조작으로 보고 BAD 판정 면제
    //   (4전이 = 1디텐트. 노이즈는 제자리 왕복이라 순이동 ≈ 0, 손은 한 방향으로 누적)
    constexpr int32_t  NOISE_ENC_HUMAN_NET  = 8;
    constexpr uint32_t NOISE_BTN_GLITCH_MAX = 3;    // 디바운스 확정 전 원복된 순간 펄스
    constexpr uint32_t INPUT_BLOCK_MS       = 3000;
    //   ESCALATE_WIN_MS 안에 버스트 N회 → 모터 정지(레시피는 일시정지=재개 가능)
    constexpr uint8_t  NOISE_ESCALATE_BURSTS = 3;
    constexpr uint32_t NOISE_ESCALATE_WIN_MS = 60000;
    //   비정상 리셋(브라운아웃/패닉/WDT) 연속 N회 → 모터 잠금 (전원 재투입으로 해제)
    //   STABLE_MS 이상 정상 가동하면 카운터 리셋.
    constexpr uint8_t  GUARD_BAD_RESETS = 3;
    constexpr uint32_t GUARD_STABLE_MS  = 60000;

    // 웹 JSON 본문 상한 — 비정상 Content-Length로 힙 고갈 방지
    constexpr size_t MAX_JSON_BODY = 16384;
    constexpr size_t MAX_RECIPES_BODY = 65536;   // /recipes.json 상한 (검증 시 통째 파싱하므로 힙 보호)
}
