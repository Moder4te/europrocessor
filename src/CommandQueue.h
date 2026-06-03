// ================================================================
//  CommandQueue.h — 크로스코어 명령 전달 + 비상정지 플래그
//  ----------------------------------------------------------------
//  웹 핸들러(Core 0)는 모터/레시피를 직접 건드리지 않고 명령만 enqueue.
//  loop(Core 1)이 drain 하여 단일 코어에서만 상태 변경 → 레이스 제거.
//
//  비상정지(estop)는 큐가 가득 차도 유실되면 안 되므로 큐를 우회하는
//  volatile 플래그로 별도 처리. Core 0 set, Core 1 확인 후 clear.
// ================================================================
#pragma once
#include "Types.h"
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

class CommandQueue {
public:
    void begin(uint8_t depth = 8) {
        _q = xQueueCreate(depth, sizeof(Cmd));
    }

    // Core 0(웹/디스플레이)에서 호출 — 실패(큐 full) 시 false
    bool enqueue(const Cmd& c) {
        if (!_q) return false;
        return xQueueSend(_q, &c, 0) == pdTRUE;
    }

    // Core 1(loop)에서 호출 — 큐에 명령 있으면 꺼내 true
    bool dequeue(Cmd& out) {
        if (!_q) return false;
        return xQueueReceive(_q, &out, 0) == pdTRUE;
    }

    // 비상정지 — 큐 우회 플래그
    void requestEstop()       { _estop = true; }
    bool consumeEstop() {           // set 되어 있었으면 clear 후 true
        if (!_estop) return false;
        _estop = false;
        return true;
    }

private:
    QueueHandle_t  _q     = nullptr;
    volatile bool  _estop = false;
};
