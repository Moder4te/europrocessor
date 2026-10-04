// ================================================================
//  Types.h — 모듈 간 공유 도메인 타입
// ================================================================
#pragma once
#include <Arduino.h>
#include <vector>

// 모터 상태 머신
enum class MotorState : uint8_t {
    IDLE,         // 정지
    RUN_FWD,      // 정방향 정속
    STOP_FWD,     // 정방향 감속 → REST
    REST,         // 방향전환 휴지 (EN=HIGH)
    RUN_REV,      // 역방향 정속
    STOP_REV,     // 역방향 감속 → REST
    STOP_RECIPE,  // 레시피 단계 종료 감속 → waitConfirm
    STOP_SAFE     // 안전 정지 감속 → IDLE (다음 사이클 없음)
};

// 레시피 단계
struct StepInfo {
    String name;
    int    speedRpm;
    int    durSec;
    int    rotIntSec;
};

// 명령 큐 — 웹/디스플레이(Core 0) → loop(Core 1) 안전 전달
enum class CmdType : uint8_t {
    STOP,          // 즉시 정지 (비상)
    PAUSE_TOGGLE,
    CONFIRM,       // 확인대기 → 다음 단계
    MANUAL,
    SAFE_STOP,     // 가속도 곡선 감속 후 정지
    SKIP_STEP,     // 레시피 진행 중 현재 단계 건너뛰고 다음 단계
    GOTO_STEP      // 레시피 진행 중 지정 단계(step)로 이동
};
struct Cmd {
    CmdType type;
    int     rpm    = 0;
    int     rotSec = 0;
    bool    fwd    = true;
    bool    cycle  = true;
    int     step   = 0;      // GOTO_STEP 대상 인덱스 (0부터)
};
