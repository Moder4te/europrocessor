// ================================================================
//  WebServer.h — ESPAsyncWebServer 라우트 + 상태 JSON + 레시피 스테이징
//  ----------------------------------------------------------------
//  웹 핸들러(Core 0)는 모터/레시피를 직접 건드리지 않는다:
//    · 모터/레시피 조작 → CommandQueue enqueue (Core 1이 처리)
//    · 비상정지        → CommandQueue::requestEstop (큐 우회)
//    · 레시피 시작     → RecipeStage::stage() → loop()이 consume()
//  의존성은 begin()에서 주입 (포인터). 소유하지 않음.
// ================================================================
#pragma once
#include <ESPAsyncWebServer.h>
#include "Types.h"
#include "MotionController.h"
#include "RecipeRunner.h"
#include "TemperatureSensor.h"
#include "CommandQueue.h"
#include "WifiManager.h"
#include "RecipeStage.h"
#include "ISaver.h"
#include "NoiseGuard.h"
#include "HwSafety.h"

class WebServer {
public:
    struct Deps {
        MotionController*  motion;
        RecipeRunner*      recipe;
        TemperatureSensor* temp;
        CommandQueue*      cmd;
        WifiManager*       wifi;
        ISaver*            saver;
        RecipeStage*       stage;
        NoiseGuard*        guard;
        HwSafety*          hw;
    };

    void begin(const Deps& deps);

private:
    void   setupRoutes();
    String buildStatus();
    // 모터 회전/감속 중이거나 레시피 진행 중(일시정지·확인대기 포함) → 플래시 쓰기·재부팅 금지
    bool   motorBusy() const { return _d.motion->state() != MotorState::IDLE || _d.recipe->active(); }

    AsyncWebServer    _server{80};
    Deps              _d{};

    // 화면보호기 업로드 — 본문 핸들러(set)와 완료 핸들러(read) 공유 상태
    bool              _upOk      = false;
    size_t            _upWritten = 0;
    File              _upFile;        // 업로드 중 열린 채 유지 (청크마다 open/close 회피)

    // WiFi 검색 결과 캐시 ({"nets":[…]} JSON)
    String            _scanJson;
    uint32_t          _scanMs = 0;
    bool              _scanPending = false;   // 내가 시작한 검색이 진행 중

    // 레시피 저장 — 동시 저장 차단용 소유자 + 열린 임시파일
    AsyncWebServerRequest* _recReq = nullptr;
    uint32_t          _recLastMs = 0;
    File              _recFile;

    // OTA — 본문 핸들러(set)와 완료 핸들러(read) 공유
    bool              _otaOk = false;
    String            _otaErr;
    AsyncWebServerRequest* _otaReq = nullptr;   // 진행 중 업로드 소유자 (동시 OTA 차단)
    uint32_t          _otaLastMs = 0;           // 마지막 청크 시각 — 끊긴 업로드 판정
};
