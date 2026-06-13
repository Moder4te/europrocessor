// ================================================================
//  RecipeStage.h — 레시피 스테이징 버퍼 (Core 0 → Core 1)
//  ----------------------------------------------------------------
//  웹(WebServer)·디스플레이(DisplayUI)가 시작할 레시피를 stage() 하면
//  loop(Core 1)이 consume() 하여 단일 코어에서 RecipeRunner에 적용.
//  Core 0 측이 레시피 실행을 트리거하는 유일한 합법 경로.
// ================================================================
#pragma once
#include "Types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <vector>

class RecipeStage {
public:
    void begin() { _mux = xSemaphoreCreateMutex(); }

    // Core 0 — 적재 성공 시 true. 실패(빈 단계/뮤텍스 타임아웃) 시
    // steps 는 이동되지 않고 호출자에 그대로 남는다 (재시도 가능).
    bool stage(const String& name, std::vector<StepInfo>&& steps) {
        if (steps.empty() || !_mux) return false;
        if (xSemaphoreTake(_mux, pdMS_TO_TICKS(100)) != pdTRUE) return false;
        _name  = name;
        _steps = std::move(steps);
        _ready = true;               // 마지막에 플래그 set
        xSemaphoreGive(_mux);
        return true;
    }

    // Core 1(loop) — 적재분 있으면 꺼내 true
    bool consume(String& name, std::vector<StepInfo>& steps) {
        if (!_ready) return false;
        bool got = false;
        if (xSemaphoreTake(_mux, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (_ready) {
                _ready = false;
                name  = _name;
                steps = std::move(_steps);
                got   = true;
            }
            xSemaphoreGive(_mux);
        }
        return got;
    }

private:
    SemaphoreHandle_t     _mux = nullptr;
    String                _name;
    std::vector<StepInfo> _steps;
    volatile bool         _ready = false;
};
