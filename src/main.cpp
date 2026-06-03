// ================================================================
//  main.cpp — App 코디네이터 / setup / loop
//  ----------------------------------------------------------------
//  서브시스템 객체를 소유하고 참조를 주입해 배선한다.
//  Core 1(loop): 명령 dispatch + motion/recipe update (시간 크리티컬)
//  Core 0: WiFi/Web + tempTask + displayTask (비시간 크리티컬)
//
//  manualMode / cycle 같은 시스템 상태 조정은 모두 여기(App)에서:
//    stopAll() = 비상정지(모터+레시피+모드 리셋)
//    safeStop()= 가속도 곡선 감속 후 정지
//    manualStart() = 수동 운전 시작
// ================================================================
#include <Arduino.h>
#include <LittleFS.h>
#include <esp_task_wdt.h>
#include <esp_partition.h>

#include "Config.h"
#include "Types.h"
#include "CommandQueue.h"
#include "MotionController.h"
#include "RecipeRunner.h"
#include "TemperatureSensor.h"
#include "WifiManager.h"
#include "WebServer.h"

#if UI_DISPLAY_PRESENT
  #include "DisplayUI.h"
#else
  #include "ISaver.h"
#endif

// ── 서브시스템 객체 ──
static MotionController  motion;
static RecipeRunner      recipe(motion);
static TemperatureSensor temp;
static CommandQueue       cmd;
static WifiManager       wifi;
static WebServer         web;

#if UI_DISPLAY_PRESENT
  static DisplayUI display;
#else
  static NullSaver nullSaver;
#endif

// ──────────────────────────────────────────────────────────────
// 코디네이터 — 시스템 상태 전이 (Core 1 전용)
// ──────────────────────────────────────────────────────────────
static void stopAll() {                 // = 기존 emergencyStop
    motion.stopImmediate();
    recipe.clear();
    motion.setManualMode(false);
    motion.setCycle(true);
}
static void safeStop() {                 // 가속도 곡선 감속 후 정지
    if (recipe.active()) recipe.clear();
    motion.setManualMode(false);
    motion.setCycle(true);
    motion.requestSafeStop();
}
static void manualStart(int rpm, bool fwd, bool cycle, int rotSec) {
    stopAll();
    motion.setManualMode(true);
    motion.setCycle(cycle);
    motion.setRotIntSec(rotSec);
    motion.setAcceleration(Cfg::ACCEL);
    motion.beginRun(rpm, fwd);
    Serial.printf("[Manual] %dRPM %s cycle=%d rot=%ds\n", rpm, fwd ? "FWD" : "REV", cycle, rotSec);
}
static void dispatch(const Cmd& c) {
    switch (c.type) {
        case CmdType::STOP:         stopAll();                                  break;
        case CmdType::PAUSE_TOGGLE: recipe.pauseToggle();                       break;
        case CmdType::CONFIRM:      recipe.confirm();                           break;
        case CmdType::MANUAL:       manualStart(c.rpm, c.fwd, c.cycle, c.rotSec); break;
        case CmdType::SAFE_STOP:    safeStop();                                 break;
    }
}

// ──────────────────────────────────────────────────────────────
// 부팅 핀 안전 고정 (★ 물리 손상 방지 — 무조건 최우선)
// ──────────────────────────────────────────────────────────────
static void safePinInit() {
    // TMC2209 EN active-low: 즉시 OUTPUT+HIGH(코일 차단)
    pinMode(Pin::EN, OUTPUT);  digitalWrite(Pin::EN, HIGH);
    // panel 신호 핀 idle 고정 (floating noise → 컨트롤러 손상 예방)
    pinMode(Pin::TFT_CS,   OUTPUT); digitalWrite(Pin::TFT_CS,   HIGH);
    pinMode(Pin::TFT_DC,   OUTPUT); digitalWrite(Pin::TFT_DC,   HIGH);
    pinMode(Pin::TFT_RST,  OUTPUT); digitalWrite(Pin::TFT_RST,  HIGH);
    pinMode(Pin::TFT_BL,   OUTPUT); digitalWrite(Pin::TFT_BL,   LOW);
    pinMode(Pin::TFT_SCK,  OUTPUT); digitalWrite(Pin::TFT_SCK,  LOW);
    pinMode(Pin::TFT_MOSI, OUTPUT); digitalWrite(Pin::TFT_MOSI, LOW);
    // 입력 핀 즉시 풀업 (floating noise 방지)
    pinMode(Pin::ENC_A,    INPUT_PULLUP);
    pinMode(Pin::ENC_B,    INPUT_PULLUP);
    pinMode(Pin::ENC_PUSH, INPUT_PULLUP);
    pinMode(Pin::KEY_OK,   INPUT_PULLUP);
}

static void mountFs() {
    if (!LittleFS.begin(false)) {
        Serial.println("[LittleFS] 1차 마운트 실패 — partition 진단:");
        const esp_partition_t* sp = esp_partition_find_first(
            ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, NULL);
        if (sp) Serial.printf("[LittleFS] spiffs 파티션: label='%s' size=%u KB offset=0x%X\n",
                              sp->label, sp->size / 1024, sp->address);
        else    Serial.println("[LittleFS] spiffs subtype 없음 — partitions.csv 미적용!");
        Serial.println("[LittleFS] format() 시도...");
        Serial.flush();
        if (LittleFS.format() && LittleFS.begin(false))
            Serial.printf("[LittleFS] 포맷 후 재마운트 성공 (%u KB)\n", LittleFS.totalBytes() / 1024);
        else
            Serial.println("[LittleFS] 포맷/재마운트 실패 — 저장 기능 비활성");
    } else {
        Serial.printf("[LittleFS] 마운트 성공 (%u KB)\n", LittleFS.totalBytes() / 1024);
    }
}

static void configWdt() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    esp_task_wdt_config_t wdtCfg = { .timeout_ms = 6000, .idle_core_mask = 0, .trigger_panic = true };
    if (esp_task_wdt_reconfigure(&wdtCfg) == ESP_ERR_INVALID_STATE) esp_task_wdt_init(&wdtCfg);
#else
    esp_task_wdt_init(6, true);
#endif
    esp_task_wdt_add(nullptr);   // setup/loop 태스크 등록
}

// ──────────────────────────────────────────────────────────────
void setup() {
    safePinInit();

    Serial.begin(115200);
    // USB-CDC(S3 네이티브 USB): 호스트가 포트를 열 때까지 최대 2초 대기 →
    // 부팅 초반 로그 유실 방지. UART 빌드에선 Serial이 곧 true라 즉시 통과.
    { uint32_t t0 = millis(); while (!Serial && millis() - t0 < 2000) delay(10); }
    delay(300);
    Serial.println("\n================================");
    Serial.println("[BOOT] Film Processor v3.1 (C++/OOP) 시작");
    Serial.printf("[BOOT] reset reason: %d, free heap: %u\n",
                  (int)esp_reset_reason(), (unsigned)ESP.getFreeHeap());
    Serial.flush();

    motion.begin();      // 스테퍼 엔진 (EN은 safePinInit에서 이미 차단)
    Serial.printf("[Stepper] MAX_SPEED=%.0f steps/s (출력축 최대 %.0f RPM)\n",
                  (float)Cfg::MAX_SPEED, (float)Cfg::MAX_OUTPUT_RPM);
    recipe.begin();
    cmd.begin(8);
    temp.begin();        // MAX31865 + Core 0 tempTask

    wifi.load();
    wifi.begin();        // AP+STA + DNS + mDNS
    mountFs();

    // 웹 서버 — 의존성 주입
    WebServer::Deps wd{};
    wd.motion = &motion; wd.recipe = &recipe; wd.temp = &temp;
    wd.cmd = &cmd; wd.wifi = &wifi;
#if UI_DISPLAY_PRESENT
    wd.saver = &display;
#else
    wd.saver = &nullSaver;
#endif
    web.begin(wd);

    configWdt();

#if UI_DISPLAY_PRESENT
    DisplayUI::Deps dd{ &motion, &recipe, &temp, &cmd };
    display.begin(dd);
    display.start();
    Serial.println("[Core] DisplayTask → Core 0 시작\n");
#else
    Serial.println("[Core] DisplayTask 비활성 (UI_DISPLAY_PRESENT=0)\n");
#endif
}

void loop() {
    esp_task_wdt_reset();

    // 0) 비상정지 최우선 (큐 우회)
    if (cmd.consumeEstop()) stopAll();

    // 1) 명령 큐 dispatch (모든 모터/레시피 변경은 Core 1에서만)
    Cmd c;
    while (cmd.dequeue(c)) dispatch(c);

    // 2) 스테이징 레시피 적용
    {
        String                name;
        std::vector<StepInfo> steps;
        if (web.consumeStaged(name, steps)) {
            stopAll();                 // 현재 레시피/모터 정지 후
            recipe.load(name, steps);  // 0단계부터 실행
        }
    }

    // 3) 비차단 업데이트
    wifi.processDns();
    motion.update();
    recipe.update();
}
