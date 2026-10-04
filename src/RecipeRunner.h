// ================================================================
//  RecipeRunner.h — 레시피 실행 (단계 시퀀스 + 확인대기 + 정전 복구)
//  ----------------------------------------------------------------
//  steps 벡터는 웹 buildStatus(Core 0)이 동시에 읽으므로 뮤텍스 보호.
//  모터는 MotionController 경유로만 구동. 단계 종료 감속 완료는
//  motion.takeStepStopped() 이벤트로 받아 waitConfirm 전환.
//
//  [정전·리셋 복구] 진행 상태를 NVS "rec"에 저장 → 재부팅 후 복구 대기.
//   · 쓰기는 모터가 멈춘 순간에만 (운전 중 플래시 쓰기 금지 원칙):
//     단계 시작 직전, 확인대기, 일시정지 완료, 방향전환 휴지(REST) 중 10초마다.
//     → 정전 시 경과시간은 마지막 REST 기준 근사값(보통 rotIntSec 이내 오차).
//   · 재부팅 후 모터는 자동으로 돌지 않음 — 사용자가 applyRecovery()로 선택.
// ================================================================
#pragma once
#include "Types.h"
#include "MotionController.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// 상태 스냅샷 — 웹/디스플레이 표시용 (뮤텍스 밖에서 안전하게 사용)
struct RecipeStatus {
    bool   running     = false;
    bool   paused      = false;
    bool   waitConfirm = false;
    String name;
    int    stepIdx     = 0;
    int    stepTotal   = 0;
    String curName;
    String nextName;
    int    stepDurSec  = 0;
    long   stepRemSec  = 0;
};

// 재부팅 후 복구 대기 정보
struct RecoveryInfo {
    bool     valid     = false;
    String   name;
    int      stepIdx   = 0;
    int      total     = 0;
    String   stepName;
    int      stepDurSec = 0;
    uint32_t elapsedMs = 0;      // 해당 단계 경과 (근사)
    uint8_t  state     = 0;      // 0 진행 중, 1 일시정지, 2 확인대기
};

enum class RecoverAction : uint8_t { DISCARD = 0, RESUME = 1, RESTART_STEP = 2, NEXT_STEP = 3 };

class RecipeRunner {
public:
    explicit RecipeRunner(MotionController& motion) : _motion(motion) {}

    void begin();     // 뮤텍스 + NVS 복구 기록 로드 (모터 구동 안 함)
    void update();    // loop()에서 매 사이클 호출

    // 레시피 적용 — steps 교체 후 0단계부터 실행
    void load(const String& name, const std::vector<StepInfo>& steps);
    void clear();     // 레시피 데이터 초기화 (모터는 건드리지 않음)

    void pauseToggle();
    void forcePause();        // 하드웨어 고장 등 — 모터 동작 없이 일시정지 상태로 보존 (호출 측이 모터 차단)
    void confirm();
    void skipStep();   // 진행 중 현재 단계 건너뛰고 다음 단계로 (확인대기 불문)
    void gotoStep(int idx);   // 지정 단계로 이동 (범위 밖이면 무시 — skip과 달리 레시피 종료 안 함)

    bool running()     const { return _running; }
    bool paused()      const { return _paused; }
    bool waitConfirm() const { return _waitConfirm; }
    bool active()      const { return _running || _paused || _waitConfirm; }

    RecipeStatus snapshot() const;
    void copySteps(String& name, std::vector<StepInfo>& out) const;   // 웹 /api/recipe 용 (Core 0)

    // ── 정전·리셋 복구 ──
    RecoveryInfo recovery() const;               // Core 0 읽기 (뮤텍스 복사)
    bool recoveryPending() const { return _recov.valid; }
    void applyRecovery(RecoverAction a);         // Core 1 — 모터 IDLE일 때만

private:
    void startStep(int idx);
    void switchTo(int idx);   // 감속 정지 후 idx 단계 시작 (skip/goto)
    void setRecipe(const String& name, const std::vector<StepInfo>& steps);

    // 영구 저장 — 모터가 멈춘 순간에만 실제 쓰기
    bool     motorQuiet() const;
    void     persistNowOrLater();     // 멈춰 있으면 즉시, 아니면 다음 멈춤에
    void     flushPersist();          // update() 맨 앞에서 호출
    void     writeRecord();
    uint32_t currentElapsedMs() const;

    MotionController& _motion;
    SemaphoreHandle_t _mux = nullptr;

    bool   _running = false, _paused = false, _waitConfirm = false;
    String _name;
    int    _stepIdx = 0;
    uint32_t _stepStartMs = 0, _pausedMs = 0;
    int    _pendingStep = -1;   // 감속 완료 대기 중인 전환 목표 단계 (-1 = 없음)
    std::vector<StepInfo> _steps;

    bool     _persistDirty  = false;   // 상태 쓰기 필요
    bool     _persistSteps  = false;   // 단계 목록도 같이 써야 함 (레시피 로드 직후)
    bool     _persistErase  = false;   // 기록 삭제 필요 (정지/완료)
    bool     _persistActive = false;   // 이번 부팅에서 기록을 썼음
    uint32_t _lastPersistMs = 0;

    RecoveryInfo          _recov;
    std::vector<StepInfo> _recovSteps;
};
