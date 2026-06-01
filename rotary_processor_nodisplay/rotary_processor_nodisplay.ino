/*
 * ================================================================
 * 아날로그 필름 로터리 프로세서 펌웨어 v3.0 — NO-DISPLAY EDITION
 * Analog Film Development Rotary Processor Firmware v3.0 (headless)
 *
 * [본 빌드 변종]
 *   - 디스플레이/인코더/KO 버튼 모듈 전부 제거 (display_ui.ino 없음)
 *   - U8g2 / Adafruit_GFX / Adafruit_ST7789 라이브러리 의존성 없음
 *   - 제어는 오직 웹 UI (AP http://192.168.4.1 또는 STA DHCP)로만 수행
 *   - 모터/온도/레시피 코어는 디스플레이 버전과 동일 (코드 동기화는 수동)
 *
 * [v2.2 → v3.0 변경사항] — 본 변종은 코어 로직만 공유
 *   - 신규 안전 정지 명령 CMD_SAFE_STOP / 상태 MS_STOP_SAFE
 *     · stopMove()로 가속도 곡선 따라 감속 후 IDLE (탈조/관성 충격 없음)
 *     · 레시피/수동 모드 모두 동일하게 안전 종료
 *     · emergencyStop()(즉시 정지)은 비상용으로 보존
 *   - 핀 정의 섹션에 ESP32-S3 GPIO 제약 검증 주석 추가
 *   - 디스플레이 변종에서 추가된 UI 개편 사항(STATUS/MENU/WARN 페이지,
 *     깜빡임 제거, 한글 폰트, KO 멀티 동작 등)은 본 변종에 적용 안 됨
 *
 * [v2.1 → v2.2 변경사항] — 크로스코어 안전성 & 장기 안정성
 *   - 명령 큐(g_cmdQueue): 웹 핸들러(Core 0)에서 모터/레시피 조작 금지,
 *     모든 변경은 loop(Core 1)에서만 수행 → mCtx/rCtx/stepper 레이스 제거
 *   - g_stagedMux: /api/start 스테이징 버퍼 양쪽 보호
 *   - MAX31865 fault 누적 카운터: 즉시 clear 금지(연속 N회 후만) → 단선 가시화
 *   - WiFi STA 자동 재연결 이벤트 핸들러 추가
 *   - g_startBuf / buildStatus 출력 String reserve()로 힙 단편화 억제
 *   - LittleFS 레시피 저장: remove() 제거, rename만으로 원자적 교체
 *   - getCurrentSpeedInMilliHz int32 abs() UB 제거
 *   - recipeUpdate / startStep의 rCtx.steps 접근에 g_rCtxMux 적용
 *   - loop / tempTask 태스크 워치독 등록
 *
 * [v2.0 → v2.1 변경사항]
 *   - 듀얼코어 분리: Core 1 = 모터 전용 / Core 0 = 온도센서
 *   - FreeRTOS tempTask: MAX31865 소프트웨어 SPI를 Core 0에서 실행
 *   - Mutex(g_tempMux): 온도 데이터 크로스코어 접근 보호
 *   - loop()에서 SPI 지연 완전 제거 → FastAccelStepper ISR 펄스 간격 균일화
 *
 * [v1.2 → v2.0 변경사항]
 *   - TB6612FNG (DC모터 PWM) → TMC2209 (스테퍼 STEP/DIR/EN)
 *   - FastAccelStepper 라이브러리: 하드웨어 타이머 ISR 기반, WiFi 병목 탈조 방지
 *   - EN 핀 HIGH/LOW: REST 구간 코일 전류 차단 (드라이버 발열 억제)
 *   - MAX31865 + PT100: 약품 온도 측정 (비차단, 1초 주기)
 *   - speedRpm: 레시피/수동 속도를 출력축 RPM으로 직접 지정 (1~100)
 *   - 웹 UI 온도 카드 추가, 디바이스 정보 업데이트
 *
 * [이식된 v1.2 기능 전체]
 *   - 레시피 관리 (B&W / C-41 / ECN-2 / E-6)
 *   - 단계별 설정: 속도%, 시간(초), 방향전환 주기(초)
 *   - 레시피 실행 / 단계 전환 확인 (약품 교체 후 계속)
 *   - 일시정지 / 재개 / 비상정지
 *   - 수동 제어 (속도, 방향, 자동 방향전환 토글)
 *   - 수동 스톱워치
 *   - WiFi AP + STA 동시 모드
 *   - 설정 저장 (Preferences)
 *   - 한국어 / 영어 전환
 *   - 단계 타임라인 시각화
 *
 * [필요 라이브러리]
 *   - ESPAsyncWebServer  (me-no-dev/ESPAsyncWebServer)
 *   - AsyncTCP           (me-no-dev/AsyncTCP)
 *   - ArduinoJson        (v6 or v7)
 *   - FastAccelStepper   (Library Manager / GitHub: gin66/FastAccelStepper)
 *   - Adafruit MAX31865  (Library Manager)
 *   - (디스플레이 관련 라이브러리는 본 변종에서 불필요)
 *
 * ──────────────────────────────────────────────────────────────
 * [핀 배정]
 *   TMC2209: 허용 핀 목록 (4,5,6,7,15,16,17,18,8,3,46,9~14) 내 사용
 *     STEP → GPIO 5   DIR → GPIO 6   EN → GPIO 7
 *   MAX31865: 허용 핀 목록 이외의 핀 사용
 *     CS   → GPIO 2   MOSI → GPIO 38  MISO → GPIO 39  CLK → GPIO 40
 *
 * [속도 계산]
 *   마이크로스텝: 8 (MS1=LOW, MS2=LOW — TMC2209 스탠드얼론 기본값)
 *   스텝/회전   = 200 × 8 = 1,600
 *   기어비      = 3.71 : 1
 *   출력축 65RPM → 모터 241.15RPM → 6,430 스텝/초
 *
 * [접속]  AP: http://192.168.4.1  |  홈WiFi: DHCP IP (설정 페이지 확인)
 * ================================================================
 */

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <FastAccelStepper.h>
#include <Adafruit_MAX31865.h>
#include <LittleFS.h>
#include <vector>
#include <esp_task_wdt.h>     // 루프/태스크 워치독
#include <cstdlib>            // std::abs(int64_t)

// ──────────────────────────────────────────────────────────────
// 핀 정의 — ESP32-S3 GPIO 제약 검증 완료
// ──────────────────────────────────────────────────────────────
// [ESP32-S3 GPIO 사용 규칙 — 본 보드 회피 항목]
//   - Strapping 핀:  0, 3, 45, 46  (부팅 시 레벨 영향 → 회피)
//   - UART0 TX/RX :  43, 44        (Serial 모니터 점유 → 회피)
//   - USB D-/D+   :  19, 20        (USB-CDC/JTAG 점유 → 회피)
//   - SPI Flash   :  26~32         (내장 플래시 전용 → 사용 불가)
//   - Octal PSRAM :  33~37         (N8R8/N16R8 모듈에서 전용 → 회피)
//   - ADC2 핀     :  11~20         (WiFi 동시 사용 시 ADC 불가 / 디지털은 무관)
//
//   본 펌웨어는 위 항목 모두 회피하며, ADC2 영역(15~18)은 디지털 입력으로만 사용.

// [TMC2209 스테퍼 드라이버] — STEP/DIR/EN (출력 전용)
//   GPIO 5/6/7 : 일반 GPIO, strapping 무관, 고속 펄스 OK
#define PIN_STEP    5    // STEP 펄스 (FastAccelStepper 하드웨어 ISR)
#define PIN_DIR     6    // 방향 제어 (HIGH/LOW)
#define PIN_EN      7    // 드라이버 EN (LOW=코일 활성, HIGH=전류 차단)

// [MAX31865 PT100 변환기] — 소프트웨어 SPI
//   GPIO 2  : strapping 아님(S3), 일반 GPIO
//   GPIO 38~40 : PSRAM/플래시 영역 밖, 안전
//   소프트웨어 SPI 채택 이유 — Core 0 별도 태스크에서 1Hz로만 폴링하면 충분,
//   하드웨어 SPI(FSPI)는 TFT가 점유하므로 핀 분리가 더 단순함
#define PIN_CS      2    // Chip Select
#define PIN_MOSI   38    // SDI
#define PIN_MISO   39    // SDO
#define PIN_CLK    40    // CLK

// [무디스플레이 변종] TFT/EC11/KO 핀 정의 없음
//   해당 GPIO(9~18, 21)는 본 보드에서 미사용 — 회로 미장착
//   향후 다른 주변기기로 재할당해도 무방

// ──────────────────────────────────────────────────────────────
// 모터 파라미터
// ──────────────────────────────────────────────────────────────
// 마이크로스텝 8배 (MS1=LOW, MS2=LOW) → 탈조 없는 안정적인 속도 범위 확보
// FastAccelStepper 하드웨어 타이머 ISR: WiFi 인터럽트와 무관하게 정밀 펄스 생성
#define STEPS_PER_REV   1600        // 200 스텝 × 8 마이크로스텝
#define GEAR_RATIO      3.71f
// 출력축 최대 RPM = 80. 80 초과 시 탈조: 마이크로스텝 계산 오류가 아니라
// 스테퍼 토크-속도 한계(출력 80=모터 ~297RPM에서 back-EMF로 토크 급감).
// 펄스 생성(7.9k step/s)은 여유 → 코드/배선 결함 아님. 더 높이려면
// SpreadCycle 활성 / VMOT↑ / 전류↑ (HW).
#define MAX_OUTPUT_RPM   80.0f      // 출력축 최대 RPM (탈조 한계 기반 안전 상한)
#define MIN_OUTPUT_RPM    5.0f      // 출력축 최소 RPM (실속 방지)

// MAX_SPEED: 80RPM × 3.71 / 60 × 1600 ≈ 7,915 스텝/초
#define MAX_SPEED   (MAX_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV)
#define MIN_SPEED   (MIN_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV)
#define ACCEL       6000.0f               // 스텝/초² (0→80RPM 약 1.3초)

// ──────────────────────────────────────────────────────────────
// 타이밍
// ──────────────────────────────────────────────────────────────
#define REST_MS     1000UL   // 방향 전환 휴지 시간 (TMC2209 열 관리)

// ──────────────────────────────────────────────────────────────
// MAX31865 설정 (RREF 하드코딩)
// ──────────────────────────────────────────────────────────────
#define RREF       412.0f   // 기준 저항 (Ω)
#define RNOMINAL   100.0f   // PT100 공칭 저항

// ──────────────────────────────────────────────────────────────
// 모터 상태 머신
// ──────────────────────────────────────────────────────────────
enum MotorState : uint8_t {
    MS_IDLE,          // 정지
    MS_RUN_FWD,       // 정방향 가속/정속
    MS_STOP_FWD,      // 정방향 감속 → REST 전환
    MS_REST,          // 방향전환 휴지 (EN=HIGH, 코일 전류 차단)
    MS_RUN_REV,       // 역방향 가속/정속
    MS_STOP_REV,      // 역방향 감속 → REST 전환
    MS_STOP_RECIPE,   // 레시피 단계 종료 감속 → waitConfirm 대기
    MS_STOP_SAFE      // 사용자 요청 안전 정지: 감속 완료 후 IDLE (다음 사이클 없음)
};

// ──────────────────────────────────────────────────────────────
// 구조체
// ──────────────────────────────────────────────────────────────
struct MotorCtx {
    MotorState state = MS_IDLE;
    int   targetRpm = 0;    // 설정 속도 RPM (출력축, 1~100)
    float curRpm    = 0.0f; // 실제 현재 RPM (출력축, FastAccelStepper 실측)
    bool isFwd    = true;
    uint32_t stateMs = 0;
    int rotIntSec = 30;  // 방향당 구동 시간 (초)
} mCtx;

struct StepInfo { String name; int speedRpm; int durSec; int rotIntSec; };

struct RecipeCtx {
    bool running=false, paused=false, waitConfirm=false;
    String name=""; int stepIdx=0;
    uint32_t stepStartMs=0, pausedMs=0;
    std::vector<StepInfo> steps;
} rCtx;

// ──────────────────────────────────────────────────────────────
// 명령 큐 — 웹 핸들러(Core 0) → loop(Core 1) 안전 전달
// 웹 핸들러는 모터/레시피 상태를 직접 건드리지 않고 큐에 명령만 넣는다.
// loop이 매 사이클 큐를 비우며 실제 핸들러를 호출하므로
// mCtx, rCtx, stepper 객체는 항상 단일 코어(Core 1)에서만 변경된다.
// ──────────────────────────────────────────────────────────────
enum CmdType : uint8_t {
    CMD_STOP,            // /api/stop — 즉시 정지 (emergencyStop, 비상용)
    CMD_PAUSE_TOGGLE,    // /api/pause
    CMD_CONFIRM,         // /api/confirm
    CMD_MANUAL,          // /api/manual
    CMD_SAFE_STOP,       // 디스플레이 KO — 가속도 곡선 따라 감속 후 정지 (탈조 방지)
};
struct Cmd {
    CmdType type;
    int   rpm;
    int   rotSec;
    bool  fwd;
    bool  cycle;
};
QueueHandle_t g_cmdQueue = nullptr;

// ──────────────────────────────────────────────────────────────
// 전역 변수
// ──────────────────────────────────────────────────────────────
String   g_apSSID="FilmProcessor", g_apPass="12345678";
String   g_staSSID="", g_staPass="";
// 홈 WiFi 고정 IP 설정
bool     g_staStatic   = false;
String   g_staStaticIP = "192.168.1.100";
String   g_staGW       = "192.168.1.1";
String   g_staSN       = "255.255.255.0";
String   g_staDNS      = "8.8.8.8";
bool     g_noCycle=false, g_manualMode=false;
// 웹 비상정지 — 명령 큐가 가득 차도 정지가 유실되지 않도록 큐를 우회하는
// 단일 플래그. Core 0(웹 핸들러)가 set, Core 1(loop)이 확인 후 clear.
volatile bool g_estop = false;
float    g_temperature = -999.0f;   // tempTask(Core 0)가 쓰고, Core 1이 읽음
uint8_t  g_tempFault   = 0;
uint16_t g_tempFaultCount = 0;      // 연속 fault 카운터 — N회 누적 후에만 clear
static const uint16_t TEMP_FAULT_CLEAR_AFTER = 5;  // 5초 연속 fault 시 래치 해제
SemaphoreHandle_t g_tempMux   = nullptr;  // 크로스코어 온도 데이터 보호
SemaphoreHandle_t g_rCtxMux   = nullptr;  // rCtx.steps 크로스코어 보호 (Core 0 ↔ Core 1)
SemaphoreHandle_t g_stagedMux = nullptr;  // g_staged 양쪽 보호 (Core 0 쓰기 ↔ Core 1 이동)

// ── 청크 수신 버퍼: /api/start 대용량 JSON을 분할 수신할 때 조립 ──────────
String g_startBuf;

// ── 스테이징 레시피: Core 0(웹 핸들러)에서 준비 → Core 1(loop)에서 원자적 적용 ──
// volatile ready 플래그를 마지막에 쓰는 것으로 메모리 배리어 역할
struct StagedRecipe {
    String               name;
    std::vector<StepInfo> steps;
    volatile bool         ready = false;
} g_staged;

Preferences      g_prefs;
AsyncWebServer   g_server(80);
DNSServer        g_dnsServer;        // AP 전용 DNS 서버 (port 53)
FastAccelStepperEngine engine;
FastAccelStepper      *stepper = nullptr;
Adafruit_MAX31865 tempSensor(PIN_CS, PIN_MOSI, PIN_MISO, PIN_CLK);

const IPAddress AP_IP (192,168,4,1);
const IPAddress AP_GW (192,168,4,1);
const IPAddress AP_SUB(255,255,255,0);

// ──────────────────────────────────────────────────────────────
// 모터 제어 헬퍼
// ──────────────────────────────────────────────────────────────
inline void enableMotor()  { digitalWrite(PIN_EN, LOW);  }   // 코일 전류 공급
inline void disableMotor() { digitalWrite(PIN_EN, HIGH); }   // 코일 전류 차단

// RPM ↔ 스텝/초 변환 (출력축 기준)
inline float rpmToSteps(float rpm) { return rpm * GEAR_RATIO / 60.0f * STEPS_PER_REV; }
inline float stepsToRpm(float sps) { return sps / STEPS_PER_REV * 60.0f / GEAR_RATIO; }

// 스테퍼 즉시 정지 (비상용) — 현재 위치를 그대로 유지하며 강제 정지
inline void stepperHalt() {
    stepper->forceStopAndNewPosition(stepper->getCurrentPosition());
}

// rpm: 출력축 RPM (1 ~ MAX_OUTPUT_RPM)
void beginMotorRun(int rpm, bool fwd) {
    if (rpm <= 0) { disableMotor(); mCtx.state = MS_IDLE; return; }

    int   safeRpm = constrain(rpm, (int)MIN_OUTPUT_RPM, (int)MAX_OUTPUT_RPM);
    float spd     = rpmToSteps((float)safeRpm);

    enableMotor();
    stepper->setSpeedInHz((uint32_t)spd);   // 목표 최고속 (steps/s)
    stepper->setCurrentPosition(0);          // 위치 카운터 오버플로 방지
    if (fwd) stepper->runForward();          // 정방향 연속 구동
    else     stepper->runBackward();         // 역방향 연속 구동

    mCtx.targetRpm = safeRpm;
    mCtx.isFwd     = fwd;
    mCtx.stateMs   = millis();
    mCtx.state     = fwd ? MS_RUN_FWD : MS_RUN_REV;
}

void emergencyStop() {
    stepperHalt();
    disableMotor();
    mCtx = {};
    rCtx.running=false; rCtx.paused=false; rCtx.waitConfirm=false;
    // g_rCtxMux 보호: Core 0(웹 핸들러)와 Core 1(loop) 양쪽에서 호출 가능
    if (g_rCtxMux && xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(10)) == pdTRUE) {
        rCtx.steps.clear();
        xSemaphoreGive(g_rCtxMux);
    } else {
        rCtx.steps.clear();  // 뮤텍스 미초기화 시(setup 전) 직접 접근
    }
    rCtx.name="";
    g_manualMode=false; g_noCycle=false;
}

// ──────────────────────────────────────────────────────────────
// 모터 상태 머신 업데이트 (loop()에서 매 사이클 호출)
// ──────────────────────────────────────────────────────────────
void motorUpdate() {
    uint32_t now = millis();
    uint32_t el  = now - mCtx.stateMs;

    // 실제 현재 출력축 RPM 갱신 → 웹 UI 표시용
    // getCurrentSpeedInMilliHz(): int32 mHz 단위 → ÷1000 = steps/s
    // abs(INT32_MIN) UB 회피: int64로 확장한 뒤 std::abs.
    if (mCtx.state != MS_IDLE && mCtx.state != MS_REST) {
        int64_t mHz = (int64_t)stepper->getCurrentSpeedInMilliHz();
        mCtx.curRpm = stepsToRpm((float)std::abs(mHz) / 1000.0f);
    } else {
        mCtx.curRpm = 0.0f;
    }

    switch (mCtx.state) {
        case MS_IDLE: break;

        case MS_RUN_FWD:
            // FastAccelStepper: 하드웨어 ISR이 스텝 펄스 생성 — run() 호출 불필요
            if (!g_noCycle && el >= (uint32_t)mCtx.rotIntSec * 1000UL) {
                stepper->stopMove();  // 감속 후 정지 (AccelStepper의 stop() 동등)
                mCtx.state = MS_STOP_FWD;
            }
            break;

        case MS_STOP_FWD:
            if (!stepper->isRunning()) {
                disableMotor();           // 완전 정지 후 EN=HIGH (발열 억제)
                mCtx.stateMs = millis();
                mCtx.state   = MS_REST;
            }
            break;

        case MS_REST:
            // EN=HIGH 상태로 1초 대기 후 반대 방향 재구동
            if (el >= REST_MS) {
                beginMotorRun(mCtx.targetRpm, !mCtx.isFwd);
            }
            break;

        case MS_RUN_REV:
            if (!g_noCycle && el >= (uint32_t)mCtx.rotIntSec * 1000UL) {
                stepper->stopMove();
                mCtx.state = MS_STOP_REV;
            }
            break;

        case MS_STOP_REV:
            if (!stepper->isRunning()) {
                disableMotor();
                mCtx.stateMs = millis();
                mCtx.state   = MS_REST;
            }
            break;

        case MS_STOP_RECIPE:
            // 레시피 단계 종료: 감속 완료 후 waitConfirm 플래그 설정
            if (!stepper->isRunning()) {
                disableMotor();
                mCtx.state   = MS_IDLE;
                mCtx.curRpm  = 0.0f;
                rCtx.waitConfirm = true;
                Serial.printf("[Recipe] Step %d 완료 - 확인 대기\n", rCtx.stepIdx + 1);
            }
            break;

        case MS_STOP_SAFE:
            // 안전 정지: 가속도 곡선 따라 감속 완료 후 IDLE (다음 사이클 진입 X)
            if (!stepper->isRunning()) {
                disableMotor();
                mCtx.state   = MS_IDLE;
                mCtx.curRpm  = 0.0f;
                Serial.println("[Motor] Safe stop complete");
            }
            break;
    }
}

// ──────────────────────────────────────────────────────────────
// 레시피 제어
// ──────────────────────────────────────────────────────────────
// startStep / recipeUpdate 모두 Core 1(loop)에서만 호출되지만,
// rCtx.steps 는 buildStatus(Core 0)이 동시에 읽으므로 스냅샷은 g_rCtxMux 안에서 만든다.
void startStep(int idx) {
    StepInfo s;
    int total = 0;
    if (xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(50)) == pdTRUE) {
        total = (int)rCtx.steps.size();
        if (idx < total) s = rCtx.steps[idx];
        xSemaphoreGive(g_rCtxMux);
    }
    if (idx >= total) {
        emergencyStop();
        Serial.println("[Recipe] 모든 단계 완료");
        return;
    }
    rCtx.stepIdx    = idx;
    rCtx.stepStartMs= millis();
    rCtx.pausedMs   = 0;
    rCtx.waitConfirm= false;
    rCtx.paused     = false;
    g_noCycle       = false;
    mCtx.rotIntSec  = s.rotIntSec;
    stepper->setAcceleration((uint32_t)ACCEL);
    Serial.printf("[Recipe] Step %d/%d: %s\n", idx+1, total, s.name.c_str());
    beginMotorRun(s.speedRpm, true);
}

void recipeUpdate() {
    if (!rCtx.running || rCtx.paused || rCtx.waitConfirm) return;
    if (mCtx.state == MS_STOP_RECIPE) return;  // motorUpdate가 waitConfirm 처리

    int durSec = 0;
    bool valid = false;
    if (xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        if (!rCtx.steps.empty() && rCtx.stepIdx < (int)rCtx.steps.size()) {
            durSec = rCtx.steps[rCtx.stepIdx].durSec;
            valid  = true;
        }
        xSemaphoreGive(g_rCtxMux);
    }
    if (!valid) return;

    uint32_t el    = millis() - rCtx.stepStartMs;
    uint32_t durMs = (uint32_t)durSec * 1000UL;
    if (el >= durMs) {
        stepper->stopMove();      // 부드러운 감속 시작
        mCtx.state = MS_STOP_RECIPE;
    }
}

// ──────────────────────────────────────────────────────────────
// 명령 핸들러 — loop(Core 1)에서만 호출됨 (큐 dispatch 경유)
// ──────────────────────────────────────────────────────────────
void pauseToggle() {
    if (!rCtx.running || rCtx.waitConfirm) return;

    if (!rCtx.paused) {
        rCtx.pausedMs = millis() - rCtx.stepStartMs;
        rCtx.paused   = true;
        stepperHalt();
        disableMotor();
        mCtx.state  = MS_IDLE;
        mCtx.curRpm = 0.0f;
        return;
    }
    // 재개: rCtx.steps 안전 스냅샷
    int rpm = 0;
    if (xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(50)) == pdTRUE) {
        if (rCtx.stepIdx < (int)rCtx.steps.size()) {
            rpm = rCtx.steps[rCtx.stepIdx].speedRpm;
        }
        xSemaphoreGive(g_rCtxMux);
    }
    if (rpm <= 0) return;
    rCtx.stepStartMs = millis() - rCtx.pausedMs;
    rCtx.paused      = false;
    stepper->setAcceleration((uint32_t)ACCEL);
    beginMotorRun(rpm, true);
}

void confirmAndAdvance() {
    if (rCtx.running && rCtx.waitConfirm) {
        rCtx.waitConfirm = false;
        startStep(rCtx.stepIdx + 1);
    }
}

// 안전 정지 — 가속도 곡선 따라 감속 후 IDLE
// emergencyStop()과 달리 forceStop()을 호출하지 않아 탈조/관성 충격 없음.
// 레시피가 실행 중이라면 함께 종료한다.
void safeStop() {
    // 레시피 컨텍스트 정리 — emergencyStop과 동일한 보호 패턴
    if (rCtx.running || rCtx.paused || rCtx.waitConfirm) {
        rCtx.running     = false;
        rCtx.paused      = false;
        rCtx.waitConfirm = false;
        if (g_rCtxMux && xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(10)) == pdTRUE) {
            rCtx.steps.clear();
            xSemaphoreGive(g_rCtxMux);
        } else {
            rCtx.steps.clear();
        }
        rCtx.name = "";
    }
    g_manualMode = false;
    g_noCycle    = false;

    // 모터: 회전 중이면 감속, 이미 정지면 즉시 IDLE
    if (stepper && stepper->isRunning()) {
        stepper->stopMove();      // 가속도 곡선 따라 감속 시작
        mCtx.state = MS_STOP_SAFE;
        Serial.println("[Motor] Safe stop requested (decelerating)");
    } else {
        disableMotor();
        mCtx.state  = MS_IDLE;
        mCtx.curRpm = 0.0f;
    }
}

void manualStart(int rpm, bool fwd, bool cycle, int rotSec) {
    emergencyStop();
    g_manualMode   = true;
    g_noCycle      = !cycle;
    mCtx.rotIntSec = rotSec;
    stepper->setAcceleration((uint32_t)ACCEL);
    beginMotorRun(rpm, fwd);
    Serial.printf("[Manual] %dRPM %s cycle=%d rot=%ds\n",
                  rpm, fwd ? "FWD" : "REV", cycle, rotSec);
}

void drainCmdQueue() {
    if (!g_cmdQueue) return;
    Cmd c;
    while (xQueueReceive(g_cmdQueue, &c, 0) == pdTRUE) {
        switch (c.type) {
            case CMD_STOP:         emergencyStop(); break;
            case CMD_PAUSE_TOGGLE: pauseToggle();   break;
            case CMD_CONFIRM:      confirmAndAdvance(); break;
            case CMD_MANUAL:       manualStart(c.rpm, c.fwd, c.cycle, c.rotSec); break;
            case CMD_SAFE_STOP:    safeStop(); break;
        }
    }
}

// 웹 핸들러에서 안전하게 명령 enqueue (실패 시 false)
bool enqueueCmd(const Cmd& c) {
    if (!g_cmdQueue) return false;
    return xQueueSend(g_cmdQueue, &c, 0) == pdTRUE;
}

// WiFi STA 자동 재연결 — 공유기 재부팅 / 일시 단절 대응
void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t /*info*/) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED:
            Serial.println("[STA] 연결 끊김 — 재연결 시도");
            WiFi.reconnect();
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            Serial.printf("[STA] 연결 복구 IP: %s\n", WiFi.localIP().toString().c_str());
            break;
        default: break;
    }
}

// ──────────────────────────────────────────────────────────────
// 온도 측정 태스크 — Core 0에서 실행 (1초 주기)
// MAX31865 소프트웨어 SPI가 Core 1의 FastAccelStepper ISR 펄스 생성에 영향 없음
// ──────────────────────────────────────────────────────────────
void tempTask(void* /*param*/) {
    // 본 태스크도 워치독에 등록 — MAX31865 SPI 행 발생 시 시스템 리셋
    esp_task_wdt_add(nullptr);
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1000));   // 1초 대기 (다른 태스크 양보)
        esp_task_wdt_reset();

        float   t = tempSensor.temperature(RNOMINAL, RREF);
        uint8_t f = tempSensor.readFault();

        // fault는 즉시 clear 하지 않는다. 연속 N회 누적된 경우에만 clear 시도
        // → 단선/접촉 불량을 UI에서 끊김 없이 관찰 가능
        bool shouldClear = false;
        uint16_t cnt = 0;
        if (xSemaphoreTake(g_tempMux, pdMS_TO_TICKS(10)) == pdTRUE) {
            g_temperature = t;
            g_tempFault   = f;
            if (f) {
                if (++g_tempFaultCount >= TEMP_FAULT_CLEAR_AFTER) {
                    shouldClear = true;
                    g_tempFaultCount = 0;   // 재계수
                }
            } else {
                g_tempFaultCount = 0;
            }
            cnt = g_tempFaultCount;
            xSemaphoreGive(g_tempMux);
        }
        if (shouldClear) tempSensor.clearFault();

        if (f) Serial.printf("[온도 오류] 0x%02X (연속 %u회)\n", f, cnt);
        else   Serial.printf("[온도] %.2f °C\n", t);
    }
}

// ──────────────────────────────────────────────────────────────
// 상태 JSON 생성
// ──────────────────────────────────────────────────────────────
String buildStatus() {
    StaticJsonDocument<896> doc;
    const char* dir = "IDLE";
    switch (mCtx.state) {
        case MS_RUN_FWD: case MS_STOP_FWD: dir="FWD"; break;
        case MS_RUN_REV: case MS_STOP_REV: dir="REV"; break;
        case MS_REST:                       dir="REST"; break;
        case MS_STOP_RECIPE:                dir="FWD"; break;  // 감속 중 (FWD 표시)
        case MS_STOP_SAFE:                  dir="STOP"; break; // 안전 감속 중
        default: break;
    }
    // motorRpm: 실제 출력축 RPM (가속/감속 중 실시간 반영)
    // 바 표시를 위해 0~100 RPM = 0~100% 그대로 사용
    doc["motorRpm"]    = (int)round(mCtx.curRpm);
    doc["motorDir"]    = dir;
    doc["recipeRun"]   = rCtx.running;
    doc["recipePause"] = rCtx.paused;
    doc["waitConfirm"] = rCtx.waitConfirm;
    doc["recipeName"]  = rCtx.name;
    doc["stepIdx"]     = rCtx.stepIdx;
    doc["manualMode"]  = g_manualMode;

    // ── g_rCtxMux: rCtx.steps 스냅샷 (Core 1이 steps를 수정할 수 있으므로 보호) ──
    {
        String  sName="", sNext="";
        int     sDur=0, sRem=0, sTotal=0;
        if (xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(5)) == pdTRUE) {
            sTotal = (int)rCtx.steps.size();
            if (rCtx.running && !rCtx.steps.empty()) {
                int idx = rCtx.stepIdx;
                if (idx < sTotal) {
                    const StepInfo& cur = rCtx.steps[idx];
                    sName = cur.name;
                    sDur  = cur.durSec;
                    if (idx+1 < sTotal) sNext = rCtx.steps[idx+1].name;
                    uint32_t el2 = rCtx.paused ? rCtx.pausedMs : (millis() - rCtx.stepStartMs);
                    // int64로 계산 — el2가 UINT32_MAX 근방이라도 음수로 깨지지 않음
                    int64_t rem = (int64_t)cur.durSec * 1000LL - (int64_t)el2;
                    if (rem < 0) rem = 0;
                    sRem = (int)(rem / 1000LL);
                }
            }
            xSemaphoreGive(g_rCtxMux);
        }
        doc["totalSteps"]  = sTotal;
        doc["stepName"]    = sName;
        doc["stepNext"]    = sNext;
        doc["stepDurSec"]  = sDur;
        doc["stepRemSec"]  = sRem;
    }

    // g_tempMux: 온도 스냅샷 (tempTask(Core 0) 쓰기와의 경쟁 방지)
    float   tempSnap  = -999.0f;
    uint8_t faultSnap = 0;
    if (xSemaphoreTake(g_tempMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        tempSnap  = g_temperature;
        faultSnap = g_tempFault;
        xSemaphoreGive(g_tempMux);
    }
    doc["temperature"] = (faultSnap == 0) ? tempSnap : -999.0f;
    doc["tempFault"]   = (faultSnap != 0);

    bool staOK = (WiFi.status() == WL_CONNECTED);
    doc["staConn"] = staOK;
    doc["staIP"]   = staOK ? WiFi.localIP().toString() : String("");
    // String reserve로 realloc 회피 — 매초 호출되므로 힙 단편화 핵심 포인트
    String out; out.reserve(1024);
    serializeJson(doc, out);
    return out;
}

// ──────────────────────────────────────────────────────────────
// 웹 UI (인라인 HTML)
// ──────────────────────────────────────────────────────────────
const char INDEX_HTML[] PROGMEM = R"HTML(<!DOCTYPE html>
<html lang="ko">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1.0,viewport-fit=cover">
<meta name="apple-mobile-web-app-capable" content="yes">
<meta name="apple-mobile-web-app-status-bar-style" content="black-translucent">
<meta name="mobile-web-app-capable" content="yes">
<title>Film Processor</title>
<style>
*,*::before,*::after{box-sizing:border-box;margin:0;padding:0;}
:root{
  --bg:#F5F3EF;--surface:#FFF;--border:#E8E4DC;
  --accent:#D97706;--accent-lt:#FEF3C7;
  --text:#1C1917;--muted:#78716C;
  --danger:#DC2626;--success:#16A34A;--info:#2563EB;
  --radius:12px;--shadow:0 1px 3px rgba(0,0,0,.10),0 1px 2px rgba(0,0,0,.06);
  --sat:env(safe-area-inset-top,0px);--sab:env(safe-area-inset-bottom,0px);
  --sal:env(safe-area-inset-left,0px);--sar:env(safe-area-inset-right,0px);
  --nav-h:56px;
}
html{height:100%;}
body{height:100dvh;overflow:hidden;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;background:var(--bg);color:var(--text);}
#app{display:flex;flex-direction:column;height:100dvh;}
.bnav{display:flex;position:fixed;bottom:0;left:0;right:0;height:calc(var(--nav-h) + var(--sab));padding-bottom:var(--sab);padding-left:var(--sal);padding-right:var(--sar);background:var(--surface);border-top:1px solid var(--border);z-index:100;}
.bnav-btn{flex:1;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:2px;border:none;background:none;cursor:pointer;color:var(--muted);font-size:10px;font-weight:600;transition:color .2s;-webkit-tap-highlight-color:transparent;padding-top:6px;}
.bnav-btn.active{color:var(--accent);}
.bnav-icon{font-size:20px;line-height:1;}
.page{flex:1;overflow-y:auto;padding:calc(12px + var(--sat)) 14px calc(var(--nav-h) + var(--sab) + 12px);display:none;}
.page.active{display:block;}
.card{background:var(--surface);border-radius:var(--radius);padding:14px;margin-bottom:10px;box-shadow:var(--shadow);border:1px solid var(--border);}
.card-title{font-size:10px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.08em;margin-bottom:10px;}
.cat-tabs{display:flex;gap:6px;margin-bottom:10px;flex-wrap:wrap;}
.cat-tab{padding:5px 13px;border-radius:20px;border:1.5px solid var(--border);background:var(--surface);cursor:pointer;font-size:13px;font-weight:600;color:var(--muted);transition:all .15s;}
.cat-tab.active{background:var(--accent);border-color:var(--accent);color:#fff;}
.recipe-list{list-style:none;}
.recipe-item{display:flex;align-items:center;padding:10px 8px;border-radius:8px;cursor:pointer;border:1.5px solid transparent;gap:8px;transition:background .1s;}
.recipe-item.selected{background:var(--accent-lt);border-color:var(--accent);}
.recipe-item-name{flex:1;font-weight:500;font-size:14px;}
.recipe-item-info{font-size:11px;color:var(--muted);}
.step-card{background:var(--bg);border-radius:8px;padding:11px;margin-bottom:7px;border:1px solid var(--border);}
.step-hdr{display:flex;align-items:center;gap:7px;margin-bottom:9px;}
.step-num{width:24px;height:24px;border-radius:50%;background:var(--accent);color:#fff;font-size:11px;font-weight:700;display:flex;align-items:center;justify-content:center;flex-shrink:0;}
.step-name-in{flex:1;border:1px solid var(--border);border-radius:6px;padding:6px 9px;font-size:13px;background:var(--surface);}
.step-params{display:grid;grid-template-columns:1fr 1fr 1fr;gap:7px;}
.sp label{display:block;font-size:9px;color:var(--muted);margin-bottom:3px;font-weight:700;text-transform:uppercase;}
.sp input{width:100%;border:1px solid var(--border);border-radius:6px;padding:6px 5px;font-size:14px;text-align:center;background:var(--surface);font-weight:600;}
.btn{padding:8px 14px;border-radius:8px;border:none;cursor:pointer;font-size:13px;font-weight:600;transition:all .15s;display:inline-flex;align-items:center;justify-content:center;gap:5px;-webkit-tap-highlight-color:transparent;}
.btn:active{transform:scale(.96);}
.btn-primary{background:var(--accent);color:#fff;}
.btn-danger{background:var(--danger);color:#fff;}
.btn-success{background:var(--success);color:#fff;}
.btn-info{background:var(--info);color:#fff;}
.btn-outline{background:none;border:1.5px solid var(--border);color:var(--text);}
.btn-sm{padding:4px 10px;font-size:11px;}
.btn-icon{padding:5px 7px;}
.btn-lg{width:100%;padding:15px;font-size:16px;border-radius:12px;}
.motor-row{display:flex;align-items:center;justify-content:space-between;padding:12px;background:var(--bg);border-radius:10px;margin-bottom:8px;}
.motor-pct{font-size:38px;font-weight:800;line-height:1;}
.motor-pct span{font-size:18px;font-weight:400;}
.badge{padding:4px 12px;border-radius:20px;font-size:11px;font-weight:700;}
.b-idle{background:#E5E7EB;color:#6B7280;}
.b-fwd{background:#D1FAE5;color:#065F46;}
.b-rev{background:#DBEAFE;color:#1E40AF;}
.b-rest{background:#FEF3C7;color:#92400E;}
.pwr-bar{height:7px;background:var(--border);border-radius:4px;overflow:hidden;}
.pwr-fill{height:100%;background:var(--accent);border-radius:4px;transition:width .3s;}
.timer-lbl{text-align:center;font-size:11px;color:var(--muted);margin-bottom:1px;}
.timer-big{text-align:center;font-size:64px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-3px;line-height:1.05;}
.timeline{display:flex;height:34px;border-radius:8px;overflow:hidden;gap:2px;}
.t-seg{display:flex;align-items:center;justify-content:center;font-size:9px;font-weight:700;color:rgba(255,255,255,.9);overflow:hidden;white-space:nowrap;padding:0 3px;transition:opacity .3s;min-width:6px;}
.t-seg.done{opacity:.3;}.t-seg.current{box-shadow:inset 0 0 0 2px rgba(0,0,0,.3);}.t-seg.pending{opacity:.6;}
.sc0{background:#D97706;}.sc1{background:#2563EB;}.sc2{background:#16A34A;}.sc3{background:#9333EA;}.sc4{background:#DC2626;}.sc5{background:#0891B2;}.sc6{background:#EA580C;}.sc7{background:#65A30D;}
.ctrl-bar{display:flex;gap:7px;}
.ctrl-bar .btn{flex:1;padding:13px 6px;font-size:13px;}
.sw-disp{text-align:center;font-size:36px;font-weight:600;font-variant-numeric:tabular-nums;letter-spacing:-1px;color:var(--muted);}
.sw-row{display:flex;gap:7px;justify-content:center;margin-top:8px;}
.pwr-slider-val{text-align:center;font-size:52px;font-weight:800;font-variant-numeric:tabular-nums;letter-spacing:-2px;color:var(--accent);line-height:1;margin:6px 0;}
.pwr-slider-val span{font-size:24px;font-weight:400;color:var(--muted);}
input[type=range]{-webkit-appearance:none;width:100%;height:6px;border-radius:3px;background:var(--border);outline:none;margin:10px 0;}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:28px;height:28px;border-radius:50%;background:var(--accent);cursor:pointer;box-shadow:0 2px 6px rgba(0,0,0,.25);}
.manual-btns{display:grid;grid-template-columns:1fr 1fr 1fr;gap:8px;}
.manual-btns .btn{padding:16px 8px;font-size:14px;flex-direction:column;gap:3px;}
.toggle-row{display:flex;align-items:center;justify-content:space-between;padding:2px 0;}
.toggle{position:relative;width:48px;height:26px;flex-shrink:0;}
.toggle input{opacity:0;width:0;height:0;}
.slider-sw{position:absolute;inset:0;background:var(--border);border-radius:13px;cursor:pointer;transition:background .2s;}
.slider-sw::before{content:'';position:absolute;width:20px;height:20px;background:#fff;border-radius:50%;left:3px;top:3px;transition:transform .2s;box-shadow:0 1px 3px rgba(0,0,0,.3);}
.toggle input:checked+.slider-sw{background:var(--accent);}
.toggle input:checked+.slider-sw::before{transform:translateX(22px);}
.manual-live{display:flex;align-items:center;justify-content:center;gap:10px;padding:10px;background:var(--bg);border-radius:8px;}
.form-grp{margin-bottom:12px;}
.form-lbl{display:block;font-size:11px;font-weight:700;color:var(--muted);margin-bottom:5px;text-transform:uppercase;letter-spacing:.05em;}
.form-in{width:100%;padding:10px 13px;border-radius:8px;border:1.5px solid var(--border);font-size:15px;background:var(--surface);transition:border-color .2s;}
.form-in:focus{outline:none;border-color:var(--accent);}
.sect-lbl{font-size:11px;font-weight:700;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin:14px 0 8px;padding-bottom:4px;border-bottom:1px solid var(--border);display:block;}
.chip{display:inline-flex;align-items:center;gap:4px;padding:3px 10px;border-radius:20px;font-size:11px;font-weight:700;}
.chip-ok{background:#D1FAE5;color:#065F46;}.chip-off{background:#E5E7EB;color:#6B7280;}
.info-row{display:flex;justify-content:space-between;padding:7px 0;border-bottom:1px solid var(--border);font-size:13px;}
.info-row:last-child{border-bottom:none;}
.info-val{font-weight:600;font-size:13px;}
.empty-state{text-align:center;padding:36px 16px;color:var(--muted);}
.empty-icon{font-size:44px;margin-bottom:6px;}
.row-act{display:flex;gap:7px;flex-wrap:wrap;margin-top:10px;}
h2{font-size:19px;font-weight:800;margin-bottom:12px;}
.temp-big{font-size:38px;font-weight:800;line-height:1;}
.temp-big span{font-size:18px;font-weight:400;}
.temp-row{display:flex;align-items:center;justify-content:space-between;padding:12px;background:var(--bg);border-radius:10px;}
.confirm-overlay{position:fixed;inset:0;z-index:300;background:rgba(0,0,0,.65);display:none;align-items:center;justify-content:center;padding:24px var(--sal) max(24px,var(--sab)) var(--sar);}
.confirm-overlay.show{display:flex;}
.confirm-card{background:var(--surface);border-radius:20px;padding:32px 24px 28px;text-align:center;max-width:360px;width:100%;animation:popIn .2s cubic-bezier(.34,1.56,.64,1);}
@keyframes popIn{from{transform:scale(.85);opacity:0;}to{transform:scale(1);opacity:1;}}
.co-icon{font-size:56px;margin-bottom:10px;}
.co-done{font-size:17px;font-weight:800;margin-bottom:6px;}
.co-msg{font-size:13px;color:var(--muted);margin-bottom:4px;line-height:1.5;}
.co-next{display:inline-block;background:var(--accent-lt);color:var(--accent);padding:5px 14px;border-radius:20px;font-size:13px;font-weight:700;margin:10px 0 16px;}
</style>
</head>
<body>
<div id="app">

<div id="page-recipe" class="page active">
  <h2 data-i18n="pg_recipe"></h2>
  <div class="cat-tabs" id="cat-tabs"></div>
  <div class="card">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;">
      <span class="card-title" data-i18n="recipes"></span>
      <button class="btn btn-primary btn-sm" onclick="addRecipe()" data-i18n="add_recipe"></button>
    </div>
    <ul class="recipe-list" id="recipe-list"></ul>
    <div id="recipe-empty" class="empty-state" style="display:none;">
      <div class="empty-icon">📷</div><div data-i18n="no_recipes"></div>
    </div>
  </div>
  <div class="card" id="step-editor" style="display:none;">
    <div style="display:flex;align-items:center;justify-content:space-between;margin-bottom:10px;">
      <span class="card-title" id="step-editor-title"></span>
      <button class="btn btn-danger btn-sm" onclick="deleteSelectedRecipe()" data-i18n="del_recipe"></button>
    </div>
    <div id="step-list"></div>
    <div class="row-act">
      <button class="btn btn-outline btn-sm" onclick="addStep()" data-i18n="add_step"></button>
      <button class="btn btn-success" onclick="runSelectedRecipe()" data-i18n="run_recipe"></button>
    </div>
  </div>
</div>

<div id="page-status" class="page">
  <h2 data-i18n="pg_status"></h2>
  <div class="card">
    <div class="card-title" data-i18n="motor_status"></div>
    <div class="motor-row">
      <div>
        <div style="font-size:11px;color:var(--muted);margin-bottom:1px;" data-i18n="speed_pct"></div>
        <div class="motor-pct" id="s-pct">0<span>RPM</span></div>
      </div>
      <span class="badge b-idle" id="s-dir">IDLE</span>
    </div>
    <div class="pwr-bar"><div class="pwr-fill" id="s-pwr" style="width:0%"></div></div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="temp_title"></div>
    <div class="temp-row">
      <div class="temp-big" id="s-temp">--.-<span>°C</span></div>
      <span class="badge b-idle" id="s-temp-fault" style="display:none;" data-i18n="temp_err"></span>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="recipe_info"></div>
    <div id="s-recipe-name" style="font-size:16px;font-weight:700;margin-bottom:8px;">—</div>
    <div class="info-row"><span data-i18n="current_step"></span><span class="info-val" id="s-cur">—</span></div>
    <div class="info-row"><span data-i18n="next_step"></span><span class="info-val" id="s-nxt">—</span></div>
  </div>
  <div class="card">
    <div class="timer-lbl" data-i18n="step_timer"></div>
    <div class="timer-big" id="s-countdown">--:--</div>
    <div class="pwr-bar" style="margin-top:7px;"><div class="pwr-fill" id="s-sprog" style="width:0%;background:var(--info);"></div></div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="timeline"></div>
    <div class="timeline" id="s-timeline"><div style="flex:1;background:var(--border);border-radius:8px;"></div></div>
    <div id="s-tl-hint" style="font-size:10px;color:var(--muted);margin-top:3px;"></div>
  </div>
  <div class="card">
    <div class="ctrl-bar">
      <button class="btn btn-success" onclick="startLastRecipe()" data-i18n="btn_start"></button>
      <button class="btn btn-outline" id="btn-pause" onclick="togglePause()" data-i18n="btn_pause"></button>
      <button class="btn btn-danger"  onclick="doStop()" data-i18n="btn_stop"></button>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="stopwatch"></div>
    <div class="sw-disp" id="sw-disp">00:00.0</div>
    <div class="sw-row">
      <button class="btn btn-outline" onclick="swToggle()" id="sw-start" data-i18n="sw_start"></button>
      <button class="btn btn-outline" onclick="swReset()" data-i18n="sw_reset"></button>
    </div>
  </div>
</div>

<div id="page-manual" class="page">
  <h2 data-i18n="pg_manual"></h2>
  <div class="card">
    <div class="manual-live">
      <span class="badge b-idle" id="m-dir-badge">IDLE</span>
      <span id="m-pct-live" style="font-size:15px;font-weight:700;">0 RPM</span>
      <span id="m-mode-tag" style="font-size:11px;color:var(--muted);"></span>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="manual_power"></div>
    <div class="pwr-slider-val"><span id="m-pct-val">50</span><span>RPM</span></div>
    <input type="range" id="m-slider" min="1" max="80" value="50"
           oninput="document.getElementById('m-pct-val').textContent=this.value">
  </div>
  <div class="card">
    <div class="card-title" data-i18n="manual_mode_title"></div>
    <div class="toggle-row" style="margin-bottom:10px;">
      <span style="font-size:14px;" data-i18n="auto_cycle"></span>
      <label class="toggle">
        <input type="checkbox" id="m-cycle-chk" checked onchange="onCycleToggle()">
        <span class="slider-sw"></span>
      </label>
    </div>
    <div id="m-rot-grp">
      <div class="form-grp" style="margin-bottom:0;">
        <label class="form-lbl" data-i18n="rot_interval"></label>
        <input class="form-in" id="m-rot-in" type="number" min="5" value="30">
      </div>
    </div>
  </div>
  <div class="card">
    <div class="manual-btns">
      <button class="btn btn-success" onclick="manualStart(true)">
        <span style="font-size:20px;">▶</span><span data-i18n="dir_fwd_short"></span>
      </button>
      <button class="btn btn-danger" onclick="doStop()">
        <span style="font-size:20px;">⏹</span><span data-i18n="btn_stop"></span>
      </button>
      <button class="btn btn-info" onclick="manualStart(false)">
        <span style="font-size:20px;">◀</span><span data-i18n="dir_rev_short"></span>
      </button>
    </div>
  </div>
</div>

<div id="page-settings" class="page">
  <h2 data-i18n="pg_settings"></h2>
  <div class="card">
    <span class="sect-lbl" data-i18n="ap_section"></span>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="wifi_ssid"></label>
      <input class="form-in" id="set-ap-ssid" type="text" maxlength="32" autocomplete="off">
    </div>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="wifi_pass"></label>
      <input class="form-in" id="set-ap-pass" type="password" maxlength="64" autocomplete="new-password" placeholder="(변경 시에만 입력)">
    </div>
    <span class="sect-lbl" data-i18n="sta_section"></span>
    <div class="form-grp">
      <label class="form-lbl" data-i18n="sta_ssid"></label>
      <input class="form-in" id="set-sta-ssid" type="text" maxlength="32" autocomplete="off">
    </div>
    <div class="form-grp" style="margin-bottom:14px;">
      <label class="form-lbl" data-i18n="sta_pass"></label>
      <input class="form-in" id="set-sta-pass" type="password" maxlength="64" autocomplete="new-password">
    </div>
    <div class="form-grp" style="margin-bottom:14px;display:flex;align-items:center;gap:10px;">
      <label style="margin:0;font-weight:600;" data-i18n="static_ip"></label>
      <label style="position:relative;display:inline-block;width:42px;height:24px;margin:0;">
        <input type="checkbox" id="set-sta-static" onchange="onStaticToggle()" style="opacity:0;width:0;height:0;">
        <span onclick="document.getElementById('set-sta-static').click()" style="position:absolute;cursor:pointer;inset:0;background:#ccc;border-radius:24px;transition:.3s;"></span>
      </label>
    </div>
    <div id="static-ip-fields" style="display:none;">
      <div class="form-grp" style="margin-bottom:10px;">
        <label class="form-lbl" data-i18n="lbl_ip"></label>
        <input class="form-in" id="set-sta-ip" type="text" maxlength="15" placeholder="192.168.1.100">
      </div>
      <div class="form-grp" style="margin-bottom:10px;">
        <label class="form-lbl" data-i18n="lbl_gw"></label>
        <input class="form-in" id="set-sta-gw" type="text" maxlength="15" placeholder="192.168.1.1">
      </div>
      <div class="form-grp" style="margin-bottom:10px;">
        <label class="form-lbl" data-i18n="lbl_sn"></label>
        <input class="form-in" id="set-sta-sn" type="text" maxlength="15" placeholder="255.255.255.0">
      </div>
      <div class="form-grp" style="margin-bottom:14px;">
        <label class="form-lbl" data-i18n="lbl_dns"></label>
        <input class="form-in" id="set-sta-dns" type="text" maxlength="15" placeholder="8.8.8.8">
      </div>
    </div>
    <button class="btn btn-primary" style="width:100%;" onclick="saveSettings()" data-i18n="save_restart"></button>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="net_status"></div>
    <div class="info-row"><span data-i18n="ap_ip"></span><span class="info-val">192.168.4.1</span></div>
    <div class="info-row">
      <span data-i18n="home_wifi"></span>
      <span id="sta-chip" class="chip chip-off" data-i18n="sta_disconn"></span>
    </div>
    <div class="info-row" id="sta-ip-row" style="display:none;">
      <span data-i18n="home_ip"></span><span class="info-val" id="sta-ip-val">—</span>
    </div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="language"></div>
    <div class="toggle-row">
      <span style="font-size:14px;">English</span>
      <label class="toggle">
        <input type="checkbox" id="lang-chk" onchange="toggleLang()">
        <span class="slider-sw"></span>
      </label>
    </div>
    <div style="margin-top:7px;font-size:12px;color:var(--muted);" id="lang-hint"></div>
  </div>
  <div class="card">
    <div class="card-title" data-i18n="about"></div>
    <div class="info-row"><span>Firmware</span><span class="info-val">v2.2</span></div>
    <div class="info-row"><span>MCU</span><span class="info-val">ESP32-S3</span></div>
    <div class="info-row"><span>Driver</span><span class="info-val">TMC2209 (Stepper)</span></div>
    <div class="info-row"><span>Temp</span><span class="info-val">MAX31865 + PT100</span></div>
  </div>
</div>

<nav class="bnav">
  <button class="bnav-btn active" onclick="switchTab('recipe',this)">
    <span class="bnav-icon">📋</span><span data-i18n="tab_recipe"></span>
  </button>
  <button class="bnav-btn" onclick="switchTab('status',this)">
    <span class="bnav-icon">▶️</span><span data-i18n="tab_status"></span>
  </button>
  <button class="bnav-btn" onclick="switchTab('manual',this)">
    <span class="bnav-icon">🎛️</span><span data-i18n="tab_manual"></span>
  </button>
  <button class="bnav-btn" onclick="switchTab('settings',this)">
    <span class="bnav-icon">⚙️</span><span data-i18n="tab_settings"></span>
  </button>
</nav>

<div class="confirm-overlay" id="confirm-overlay">
  <div class="confirm-card">
    <div class="co-icon" id="co-icon">⚗️</div>
    <div class="co-done" id="co-done"></div>
    <div class="co-msg"  id="co-msg"></div>
    <div class="co-next" id="co-next" style="display:none;"></div>
    <button class="btn btn-primary btn-lg" onclick="doConfirm()" id="co-btn"></button>
  </div>
</div>
</div>

<script>
'use strict';
const STR={
  ko:{
    pg_recipe:'레시피 관리',pg_status:'현재 상태',pg_manual:'수동 제어',pg_settings:'설정',
    tab_recipe:'레시피',tab_status:'상태',tab_manual:'수동',tab_settings:'설정',
    recipes:'레시피 목록',add_recipe:'+ 추가',no_recipes:'레시피가 없습니다. 추가해주세요.',
    del_recipe:'삭제',add_step:'+ 단계 추가',run_recipe:'▶ 실행',
    motor_status:'모터 상태',speed_pct:'출력축 속도',
    temp_title:'약품 온도',temp_err:'센서 오류',
    recipe_info:'레시피 진행',current_step:'현재 단계',next_step:'다음 단계',
    step_timer:'현재 단계 남은 시간',timeline:'단계 타임라인',
    btn_start:'▶ 시작',btn_pause:'⏸ 일시정지',btn_resume:'▶ 재개',btn_stop:'⏹ 정지',
    stopwatch:'수동 스톱워치',sw_start:'시작',sw_stop:'정지',sw_reset:'초기화',
    manual_power:'속도 설정',manual_mode_title:'작동 모드',
    auto_cycle:'자동 방향 전환',rot_interval:'방향당 운전 시간 (초)',
    dir_fwd_short:'순방향',dir_rev_short:'역방향',
    ap_section:'자체 AP 설정 (직접 접속)',wifi_ssid:'AP SSID',wifi_pass:'AP 비밀번호',
    sta_section:'홈 WiFi 연결 (같은 네트워크 접속)',sta_ssid:'홈 WiFi SSID',sta_pass:'홈 WiFi 비밀번호',
    static_ip:'고정 IP 사용',lbl_ip:'IP 주소',lbl_gw:'게이트웨이',lbl_sn:'서브넷 마스크',lbl_dns:'DNS 서버',
    save_restart:'저장 및 재시작',
    net_status:'네트워크 상태',ap_ip:'AP IP (직접 접속)',
    home_wifi:'홈 WiFi',sta_conn:'연결됨 ✓',sta_disconn:'미연결',home_ip:'홈 WiFi IP',
    language:'언어 설정',lang_hint:'현재: 한국어',about:'디바이스 정보',
    ph_step_name:'단계 이름',lbl_power:'속도(RPM)',lbl_dur:'시간(초)',lbl_rot:'방향전환(초)',
    confirm_del:'이 레시피를 삭제하시겠습니까?',
    confirm_run:'레시피를 실행합니다. 현재 동작이 중단됩니다.',
    prompt_name:'레시피 이름:',default_name:'새 레시피',
    warn_no_steps:'단계를 먼저 추가해주세요.',warn_no_sel:'레시피를 먼저 선택해주세요.',
    warn_no_last:'레시피 탭에서 먼저 실행해주세요.',
    saved_ok:'저장됨. 재시작 중... 새 네트워크에 접속하세요.',err_ssid:'SSID를 입력해주세요.',
    dir_fwd:'순방향 ▶',dir_rev:'◀ 역방향',dir_rest:'방향전환',dir_idle:'정지',
    co_done_last:'🎉 레시피 완료!',co_done_step:'✅ 단계 완료',
    co_msg_last:'모든 단계가 완료되었습니다.',co_msg_step:'약품을 교체하고 다음 단계를 진행하세요.',
    co_next_pre:'다음 단계: ',co_btn_last:'확인',co_btn_next:'다음 단계 ▶',
    manual_tag:'수동 모드',
  },
  en:{
    pg_recipe:'Recipes',pg_status:'Status',pg_manual:'Manual',pg_settings:'Settings',
    tab_recipe:'Recipe',tab_status:'Status',tab_manual:'Manual',tab_settings:'Settings',
    recipes:'Recipes',add_recipe:'+ Add',no_recipes:'No recipes. Press + Add.',
    del_recipe:'Delete',add_step:'+ Add Step',run_recipe:'▶ Run',
    motor_status:'Motor Status',speed_pct:'Output Shaft',
    temp_title:'Solution Temp',temp_err:'Sensor Error',
    recipe_info:'Recipe Progress',current_step:'Current Step',next_step:'Next Step',
    step_timer:'Step Remaining',timeline:'Step Timeline',
    btn_start:'▶ Start',btn_pause:'⏸ Pause',btn_resume:'▶ Resume',btn_stop:'⏹ Stop',
    stopwatch:'Manual Stopwatch',sw_start:'Start',sw_stop:'Stop',sw_reset:'Reset',
    manual_power:'Speed Setting',manual_mode_title:'Operation Mode',
    auto_cycle:'Auto Direction Cycle',rot_interval:'Per-direction Time (sec)',
    dir_fwd_short:'Forward',dir_rev_short:'Reverse',
    ap_section:'AP Settings (Direct Connect)',wifi_ssid:'AP SSID',wifi_pass:'AP Password',
    sta_section:'Home WiFi (Same Network Access)',sta_ssid:'Home WiFi SSID',sta_pass:'Home WiFi Password',
    static_ip:'Use Static IP',lbl_ip:'IP Address',lbl_gw:'Gateway',lbl_sn:'Subnet Mask',lbl_dns:'DNS Server',
    save_restart:'Save & Restart',
    net_status:'Network Status',ap_ip:'AP IP (Direct)',
    home_wifi:'Home WiFi',sta_conn:'Connected ✓',sta_disconn:'Not connected',home_ip:'Home WiFi IP',
    language:'Language',lang_hint:'Current: English',about:'Device Info',
    ph_step_name:'Step name',lbl_power:'Speed(RPM)',lbl_dur:'Duration(s)',lbl_rot:'Rotation(s)',
    confirm_del:'Delete this recipe?',
    confirm_run:'Run recipe? Current operation will stop.',
    prompt_name:'Recipe name:',default_name:'New Recipe',
    warn_no_steps:'Add steps first.',warn_no_sel:'Select a recipe first.',
    warn_no_last:'Select and run a recipe from the Recipe tab.',
    saved_ok:'Saved. Restarting...',err_ssid:'Please enter an SSID.',
    dir_fwd:'Forward ▶',dir_rev:'◀ Reverse',dir_rest:'Switching',dir_idle:'Idle',
    co_done_last:'🎉 Recipe Complete!',co_done_step:'✅ Step Done',
    co_msg_last:'All steps completed.',co_msg_step:'Replace chemicals, then continue.',
    co_next_pre:'Next: ',co_btn_last:'Done',co_btn_next:'Next Step ▶',
    manual_tag:'Manual',
  }
};
let lang=localStorage.getItem('lang')||'ko';
function t(k){return STR[lang][k]||k;}
function applyLang(){
  document.querySelectorAll('[data-i18n]').forEach(el=>el.textContent=t(el.dataset.i18n));
  document.getElementById('lang-chk').checked=(lang==='en');
  document.getElementById('lang-hint').textContent=t('lang_hint');
  renderCatTabs();renderRecipeList();renderStepEditor();
}
function toggleLang(){lang=(lang==='ko')?'en':'ko';localStorage.setItem('lang',lang);applyLang();}

const CATS=['B&W','C-41','ECN-2','E-6'];
function mkE(){const r={};CATS.forEach(c=>r[c]=[]);return r;}
async function loadR(){
  try{
    const r=await fetch('/api/recipes/load');
    if(!r.ok)return mkE();
    const d=await r.json();
    CATS.forEach(c=>{if(!d[c])d[c]=[];});
    return d;
  }catch{return mkE();}
}
let _saveTimer=null;
function saveR(d){
  clearTimeout(_saveTimer);
  // 800ms 디바운스: 빠른 타이핑 중 불필요한 플래시 쓰기 방지
  _saveTimer=setTimeout(()=>postJ('/api/recipes/save',d),800);
}
let recipes=mkE(),activeCat=CATS[0],selIdx=-1,lastRun=null;

function renderCatTabs(){
  document.getElementById('cat-tabs').innerHTML=CATS.map(c=>`<button class="cat-tab ${c===activeCat?'active':''}" onclick="setCat('${c}')">${c}</button>`).join('');
}
function setCat(c){activeCat=c;selIdx=-1;renderCatTabs();renderRecipeList();renderStepEditor();}
function renderRecipeList(){
  const list=document.getElementById('recipe-list'),empty=document.getElementById('recipe-empty'),items=recipes[activeCat]||[];
  if(!items.length){list.innerHTML='';empty.style.display='';document.getElementById('step-editor').style.display='none';return;}
  empty.style.display='none';
  list.innerHTML=items.map((r,i)=>`<li class="recipe-item ${i===selIdx?'selected':''}" onclick="selRecipe(${i})"><span class="recipe-item-name">${esc(r.name)}</span><span class="recipe-item-info">${r.steps.length} steps</span></li>`).join('');
}
function selRecipe(i){selIdx=i;renderRecipeList();renderStepEditor();}
function addRecipe(){const name=prompt(t('prompt_name'),t('default_name'));if(!name)return;recipes[activeCat].push({name:name.trim(),steps:[]});saveR(recipes);selIdx=recipes[activeCat].length-1;renderRecipeList();renderStepEditor();}
function deleteSelectedRecipe(){if(selIdx<0||!confirm(t('confirm_del')))return;recipes[activeCat].splice(selIdx,1);saveR(recipes);selIdx=-1;renderRecipeList();renderStepEditor();}
function renderStepEditor(){
  const ed=document.getElementById('step-editor');
  if(selIdx<0||!recipes[activeCat][selIdx]){ed.style.display='none';return;}
  ed.style.display='';
  const rec=recipes[activeCat][selIdx];
  document.getElementById('step-editor-title').textContent=rec.name;
  document.getElementById('step-list').innerHTML=rec.steps.map((s,i)=>`
    <div class="step-card"><div class="step-hdr">
      <div class="step-num">${i+1}</div>
      <input class="step-name-in" placeholder="${t('ph_step_name')}" value="${esc(s.name)}" oninput="updS(${i},'name',this.value)">
      <button class="btn btn-outline btn-icon btn-sm" onclick="mvS(${i},-1)">↑</button>
      <button class="btn btn-outline btn-icon btn-sm" onclick="mvS(${i},1)">↓</button>
      <button class="btn btn-danger btn-icon btn-sm" onclick="rmS(${i})">✕</button>
    </div>
    <div class="step-params">
      <div class="sp"><label>${t('lbl_power')}</label><input type="number" min="1" max="80" value="${s.speedRpm}" oninput="updS(${i},'speedRpm',+this.value)"></div>
      <div class="sp"><label>${t('lbl_dur')}</label><input type="number" min="1" value="${s.durSec}" oninput="updS(${i},'durSec',+this.value)"></div>
      <div class="sp"><label>${t('lbl_rot')}</label><input type="number" min="5" value="${s.rotIntSec}" oninput="updS(${i},'rotIntSec',+this.value)"></div>
    </div></div>`).join('');
}
function addStep(){if(selIdx<0)return;const ss=recipes[activeCat][selIdx].steps;ss.push({name:'Step '+(ss.length+1),speedRpm:50,durSec:60,rotIntSec:30});saveR(recipes);renderStepEditor();}
function rmS(i){recipes[activeCat][selIdx].steps.splice(i,1);saveR(recipes);renderStepEditor();}
function mvS(i,d){const ss=recipes[activeCat][selIdx].steps,j=i+d;if(j<0||j>=ss.length)return;[ss[i],ss[j]]=[ss[j],ss[i]];saveR(recipes);renderStepEditor();}
function updS(i,k,v){recipes[activeCat][selIdx].steps[i][k]=v;saveR(recipes);}

async function runSelectedRecipe(){
  if(selIdx<0){alert(t('warn_no_sel'));return;}
  const rec=recipes[activeCat][selIdx];
  if(!rec.steps.length){alert(t('warn_no_steps'));return;}
  if(!confirm(t('confirm_run')))return;
  lastRun={recipeName:rec.name,steps:rec.steps.map(s=>({name:s.name,speedRpm:clamp(s.speedRpm||50,1,80),durationSec:Math.max(1,s.durSec),rotIntSec:Math.max(5,s.rotIntSec)}))};
  await postJ('/api/start',lastRun);
  switchTab('status',document.querySelectorAll('.bnav-btn')[1]);
}
async function startLastRecipe(){if(!lastRun){alert(t('warn_no_last'));return;}await postJ('/api/start',lastRun);}

let sd={motorRpm:0,motorDir:'IDLE',recipeRun:false,recipePause:false,waitConfirm:false,recipeName:'',stepName:'',stepNext:'',totalSteps:0,stepIdx:0,stepDurSec:0,stepRemSec:0,manualMode:false,staConn:false,staIP:'',temperature:-999,tempFault:false};
setInterval(fetchStatus,500);
async function fetchStatus(){try{const r=await fetch('/api/status');sd=await r.json();updateStatusUI();}catch(e){}}

function updateStatusUI(){
  document.getElementById('s-pct').innerHTML=sd.motorRpm+'<span>RPM</span>';
  document.getElementById('s-pwr').style.width=Math.min(100,sd.motorRpm*100/80)+'%';  // 최대 80RPM = 100%
  const dm={FWD:['b-fwd',t('dir_fwd')],REV:['b-rev',t('dir_rev')],REST:['b-rest',t('dir_rest')],IDLE:['b-idle',t('dir_idle')]};
  const [cls,txt]=dm[sd.motorDir]||['b-idle',t('dir_idle')];
  const b=document.getElementById('s-dir');b.className='badge '+cls;b.textContent=txt;

  // 온도 업데이트
  const te=document.getElementById('s-temp'),tf=document.getElementById('s-temp-fault');
  if(sd.tempFault){te.innerHTML='--.-<span>°C</span>';tf.style.display='';}
  else if(sd.temperature<=-100){te.innerHTML='--.-<span>°C</span>';tf.style.display='none';}
  else{te.innerHTML=sd.temperature.toFixed(1)+'<span>°C</span>';tf.style.display='none';}

  document.getElementById('s-recipe-name').textContent=sd.recipeName||'—';
  document.getElementById('s-cur').textContent=sd.stepName||'—';
  document.getElementById('s-nxt').textContent=sd.stepNext||'—';
  document.getElementById('s-countdown').textContent=sd.recipeRun?fmtSec(sd.stepRemSec):'--:--';
  const dur=sd.stepDurSec||1;
  document.getElementById('s-sprog').style.width=(sd.recipeRun?((dur-sd.stepRemSec)/dur*100).toFixed(1):0)+'%';
  renderTimeline(sd);
  const pb=document.getElementById('btn-pause');
  if(sd.recipePause){pb.textContent=t('btn_resume');pb.className='btn btn-success';}
  else{pb.textContent=t('btn_pause');pb.className='btn btn-outline';}
  const ov=document.getElementById('confirm-overlay');
  if(sd.waitConfirm){
    ov.classList.add('show');
    const isLast=!sd.stepNext;
    document.getElementById('co-icon').textContent=isLast?'🎉':'⚗️';
    document.getElementById('co-done').textContent=isLast?t('co_done_last'):t('co_done_step')+': '+sd.stepName;
    document.getElementById('co-msg').textContent=isLast?t('co_msg_last'):t('co_msg_step');
    const nx=document.getElementById('co-next');
    if(!isLast){nx.style.display='';nx.textContent=t('co_next_pre')+sd.stepNext;}else{nx.style.display='none';}
    document.getElementById('co-btn').textContent=isLast?t('co_btn_last'):t('co_btn_next');
  } else { ov.classList.remove('show'); }
  const mb=document.getElementById('m-dir-badge');mb.className='badge '+cls;mb.textContent=txt;
  document.getElementById('m-pct-live').textContent=sd.motorRpm+' RPM';
  document.getElementById('m-mode-tag').textContent=sd.manualMode?t('manual_tag'):'';
  const chip=document.getElementById('sta-chip'),ipRow=document.getElementById('sta-ip-row');
  if(sd.staConn){chip.className='chip chip-ok';chip.textContent=t('sta_conn');ipRow.style.display='';document.getElementById('sta-ip-val').textContent=sd.staIP||'—';}
  else{chip.className='chip chip-off';chip.textContent=t('sta_disconn');ipRow.style.display='none';}
}

function renderTimeline(d){
  const tl=document.getElementById('s-timeline'),hint=document.getElementById('s-tl-hint');
  if(!d.recipeRun||!d.totalSteps){tl.innerHTML='<div style="flex:1;background:var(--border);border-radius:8px;"></div>';hint.textContent='';return;}
  const steps=(lastRun&&lastRun.steps.length===d.totalSteps)?lastRun.steps:null;
  const total=steps?steps.reduce((a,s)=>a+s.durationSec,0):d.totalSteps;
  tl.innerHTML=Array.from({length:d.totalSteps},(_,i)=>{
    const cls=i<d.stepIdx?'done':i===d.stepIdx?'current':'pending';
    const w=steps?(steps[i].durationSec/total*100).toFixed(1)+'%':(100/d.totalSteps).toFixed(1)+'%';
    const lbl=steps?(steps[i].name.length>7?steps[i].name.slice(0,6)+'…':steps[i].name):(i+1);
    return `<div class="t-seg sc${i%8} ${cls}" style="flex:0 0 ${w}">${lbl}</div>`;
  }).join('');
  if(steps&&d.stepIdx<steps.length) hint.textContent=`${d.stepIdx+1}/${d.totalSteps}: ${steps[d.stepIdx].name}`;
}

async function togglePause(){await fetch('/api/pause',{method:'POST'});fetchStatus();}
async function doStop(){await fetch('/api/stop',{method:'POST'});fetchStatus();}
async function doConfirm(){await fetch('/api/confirm',{method:'POST'});fetchStatus();}

function onCycleToggle(){document.getElementById('m-rot-grp').style.display=document.getElementById('m-cycle-chk').checked?'':'none';}
async function manualStart(fwd){
  const pct=+document.getElementById('m-slider').value;
  const cycle=document.getElementById('m-cycle-chk').checked;
  const rotSec=Math.max(5,+document.getElementById('m-rot-in').value||30);
  await postJ('/api/manual',{speedRpm:pct,fwd:fwd,cycle:cycle,rotIntSec:rotSec});
  fetchStatus();
}

let swOn=false,swBase=0,swRef=0,swTmr=null;
function swToggle(){
  if(!swOn){swRef=Date.now();swOn=true;document.getElementById('sw-start').textContent=t('sw_stop');swTmr=setInterval(swTick,100);}
  else{clearInterval(swTmr);swBase+=Date.now()-swRef;swOn=false;document.getElementById('sw-start').textContent=t('sw_start');swTick();}
}
function swReset(){clearInterval(swTmr);swOn=false;swBase=0;document.getElementById('sw-start').textContent=t('sw_start');document.getElementById('sw-disp').textContent='00:00.0';}
function swTick(){const ms=swBase+(swOn?Date.now()-swRef:0),s=Math.floor(ms/1000),ds=Math.floor((ms%1000)/100);document.getElementById('sw-disp').textContent=pad2(Math.floor(s/60))+':'+pad2(s%60)+'.'+ds;}

function onStaticToggle(){
  const on=document.getElementById('set-sta-static').checked;
  document.getElementById('static-ip-fields').style.display=on?'block':'none';
  // 토글 스위치 색상 업데이트
  const span=document.querySelector('#set-sta-static+span');
  if(span)span.style.background=on?'#4CAF50':'#ccc';
}
async function loadSettings(){
  try{
    const r=await fetch('/api/settings');const d=await r.json();
    document.getElementById('set-ap-ssid').value=d.apSSID||'';
    document.getElementById('set-sta-ssid').value=d.staSSID||'';
    const st=d.staStatic||false;
    document.getElementById('set-sta-static').checked=st;
    document.getElementById('set-sta-ip').value=d.staStaticIP||'192.168.1.100';
    document.getElementById('set-sta-gw').value=d.staGW||'192.168.1.1';
    document.getElementById('set-sta-sn').value=d.staSN||'255.255.255.0';
    document.getElementById('set-sta-dns').value=d.staDNS||'8.8.8.8';
    onStaticToggle();
  }catch(e){}
}
async function saveSettings(){
  const apSSID=document.getElementById('set-ap-ssid').value.trim();
  if(!apSSID){alert(t('err_ssid'));return;}
  const staStatic=document.getElementById('set-sta-static').checked;
  await postJ('/api/settings',{
    apSSID,apPass:document.getElementById('set-ap-pass').value,
    staSSID:document.getElementById('set-sta-ssid').value.trim(),
    staPass:document.getElementById('set-sta-pass').value,
    staStatic,
    staStaticIP:document.getElementById('set-sta-ip').value.trim(),
    staGW:document.getElementById('set-sta-gw').value.trim(),
    staSN:document.getElementById('set-sta-sn').value.trim(),
    staDNS:document.getElementById('set-sta-dns').value.trim()
  });
  alert(t('saved_ok'));
}

function switchTab(name,btn){
  document.querySelectorAll('.page').forEach(p=>p.classList.remove('active'));
  document.querySelectorAll('.bnav-btn').forEach(b=>b.classList.remove('active'));
  document.getElementById('page-'+name).classList.add('active');
  btn.classList.add('active');
}

function fmtSec(s){s=Math.max(0,Math.floor(s));return pad2(Math.floor(s/60))+':'+pad2(s%60);}
function pad2(n){return String(n).padStart(2,'0');}
function clamp(v,lo,hi){return Math.max(lo,Math.min(hi,v));}
function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');}
async function postJ(url,d){return fetch(url,{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(d)});}

window.addEventListener('DOMContentLoaded',async ()=>{
  applyLang();
  recipes=await loadR();   // 보드 플래시에서 레시피 로드
  renderCatTabs();renderRecipeList();loadSettings();fetchStatus();onCycleToggle();
});
</script>
</body>
</html>
)HTML";

// ──────────────────────────────────────────────────────────────
// HTTP 라우트 설정
// ──────────────────────────────────────────────────────────────
void setupRoutes() {
    g_server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send_P(200, "text/html", INDEX_HTML);
    });
    g_server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req){
        req->send(200, "application/json", buildStatus());
    });
    // ── /api/start: 청크 분할 수신 + g_stagedMux 양방향 보호 ──────────────
    // ESPAsyncWebServer는 JSON이 ~1.5KB(MTU)를 초과하면 콜백을 여러 번 호출함.
    // index==0 이면 버퍼 초기화 + reserve(힙 단편화 억제), 완료 시 파싱.
    // g_staged 쓰기는 g_stagedMux 안에서 수행 → loop()의 std::move와 절대 겹치지 않음.
    // 실제 rCtx 변경은 Core 1의 loop() 첫머리에서 안전하게 수행됨.
    g_server.on("/api/start", HTTP_POST,
        [](AsyncWebServerRequest* req){}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total){
            // 청크 조립 — index==0 에서 total 크기로 미리 reserve (String realloc 방지)
            if (index == 0) {
                g_startBuf = "";
                g_startBuf.reserve(total + 1);
            }
            g_startBuf.concat(reinterpret_cast<const char*>(data), len);
            if (index + len < total) return;  // 아직 수신 중

            // 완전 수신 — 파싱
            DynamicJsonDocument doc(4096);
            if (deserializeJson(doc, g_startBuf)) {
                g_startBuf = "";
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            g_startBuf = "";

            // 스테이징 버퍼에 기록 — g_stagedMux 보호 (Core 1 loop의 std::move와 직렬화)
            if (xSemaphoreTake(g_stagedMux, pdMS_TO_TICKS(100)) != pdTRUE) {
                req->send(503, "application/json", "{\"ok\":false}"); return;
            }
            g_staged.name = doc["recipeName"] | String("Unknown");
            g_staged.steps.clear();
            for (JsonObject s : doc["steps"].as<JsonArray>()) {
                StepInfo si;
                si.name      = s["name"]       | String("Step");
                si.speedRpm  = constrain((int)(s["speedRpm"]   | 50), 1, (int)MAX_OUTPUT_RPM);
                si.durSec    = max((int)(s["durationSec"] | 60), 1);
                si.rotIntSec = max((int)(s["rotIntSec"]   | 30), 5);
                g_staged.steps.push_back(si);
            }
            bool empty = g_staged.steps.empty();
            if (!empty) g_staged.ready = true;  // 마지막에 플래그 set
            xSemaphoreGive(g_stagedMux);

            if (empty) { req->send(400, "application/json", "{\"ok\":false}"); return; }
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );
    // 이하 모터/레시피 조작 핸들러는 명령 큐로 전달만 한다.
    // 실제 상태 변경은 Core 1 loop()의 drainCmdQueue()에서 수행.
    g_server.on("/api/stop", HTTP_POST, [](AsyncWebServerRequest* req){
        // 정지는 안전 명령 → 큐(깊이 제한)를 우회해 플래그로 직접 전달.
        // loop()이 매 사이클 최우선으로 확인하므로 큐가 가득 차도 유실 없음.
        g_estop = true;
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/pause", HTTP_POST, [](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CMD_PAUSE_TOGGLE;
        enqueueCmd(c);
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/confirm", HTTP_POST, [](AsyncWebServerRequest* req){
        Cmd c{}; c.type = CMD_CONFIRM;
        enqueueCmd(c);
        req->send(200, "application/json", "{\"ok\":true}");
    });
    g_server.on("/api/manual", HTTP_POST,
        [](AsyncWebServerRequest* req){}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            StaticJsonDocument<128> doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            Cmd c{};
            c.type   = CMD_MANUAL;
            c.rpm    = constrain((int)(doc["speedRpm"]  | 50), 1, (int)MAX_OUTPUT_RPM);
            c.fwd    = doc["fwd"]   | true;
            c.cycle  = doc["cycle"] | true;
            c.rotSec = max((int)(doc["rotIntSec"] | 30), 5);
            enqueueCmd(c);
            req->send(200, "application/json", "{\"ok\":true}");
        }
    );
    g_server.on("/api/settings", HTTP_GET, [](AsyncWebServerRequest* req){
        StaticJsonDocument<512> doc;
        doc["apSSID"]      = g_apSSID;
        doc["staSSID"]     = g_staSSID;
        doc["staConn"]     = (WiFi.status() == WL_CONNECTED);
        doc["staIP"]       = (WiFi.status() == WL_CONNECTED) ? WiFi.localIP().toString() : String("");
        doc["staStatic"]   = g_staStatic;
        doc["staStaticIP"] = g_staStaticIP;
        doc["staGW"]       = g_staGW;
        doc["staSN"]       = g_staSN;
        doc["staDNS"]      = g_staDNS;
        String out; serializeJson(doc, out); req->send(200, "application/json", out);
    });
    g_server.on("/api/settings", HTTP_POST,
        [](AsyncWebServerRequest* req){}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t, size_t){
            StaticJsonDocument<768> doc;
            if (deserializeJson(doc, data, len)) {
                req->send(400, "application/json", "{\"ok\":false}"); return;
            }
            String newApSSID      = doc["apSSID"]      | g_apSSID;
            String newApPass      = doc["apPass"]      | String("");
            String newStaSSID     = doc["staSSID"]     | String("");
            String newStaPass     = doc["staPass"]     | String("");
            bool   newStaStatic   = doc["staStatic"]   | false;
            String newStaticIP    = doc["staStaticIP"] | String("192.168.1.100");
            String newGW          = doc["staGW"]       | String("192.168.1.1");
            String newSN          = doc["staSN"]       | String("255.255.255.0");
            String newDNS         = doc["staDNS"]      | String("8.8.8.8");
            g_prefs.begin("wifi", false);
            g_prefs.putString("ap_ssid",    newApSSID);
            if (newApPass.length() >= 8)  g_prefs.putString("ap_pass", newApPass);
            g_prefs.putString("sta_ssid",   newStaSSID);
            if (newStaPass.length() >= 8) g_prefs.putString("sta_pass", newStaPass);
            else if (newStaPass.length() == 0 && newStaSSID.length() > 0)
                g_prefs.putString("sta_pass", g_staPass);
            g_prefs.putBool  ("sta_static", newStaStatic);
            g_prefs.putString("sta_ip",     newStaticIP);
            g_prefs.putString("sta_gw",     newGW);
            g_prefs.putString("sta_sn",     newSN);
            g_prefs.putString("sta_dns",    newDNS);
            g_prefs.end();
            req->send(200, "application/json", "{\"ok\":true}");
            delay(200); ESP.restart();
        }
    );
    // ── 레시피 불러오기 ─────────────────────────────────────────
    g_server.on("/api/recipes/load", HTTP_GET, [](AsyncWebServerRequest* req){
        if (!LittleFS.exists("/recipes.json")) {
            // 파일 없음 = 첫 실행 → 빈 구조 반환
            req->send(200, "application/json",
                      "{\"B&W\":[],\"C-41\":[],\"ECN-2\":[],\"E-6\":[]}");
            return;
        }
        req->send(LittleFS, "/recipes.json", "application/json");
    });

    // ── 레시피 저장 ──────────────────────────────────────────────
    // 청크 수신: index==0이면 파일 새로 생성, 이후는 이어 쓰기
    // 완료 시 rename 단독으로 원자적 교체 (LittleFS rename은 기존 타깃을 덮어씀).
    // 기존의 remove() 호출은 정전 시 두 파일 모두 사라지는 짧은 윈도우를 만들었으므로 제거.
    g_server.on("/api/recipes/save", HTTP_POST,
        [](AsyncWebServerRequest* req){}, nullptr,
        [](AsyncWebServerRequest* req, uint8_t* data, size_t len,
           size_t index, size_t total){
            File f = LittleFS.open("/recipes.tmp",
                                   index == 0 ? "w" : "a");
            if (f) { f.write(data, len); f.close(); }

            if (index + len >= total) {
                // 전체 수신 완료 → 원자적 교체
                bool ok = LittleFS.rename("/recipes.tmp", "/recipes.json");
                if (!ok) {
                    // 일부 구버전 wrapper가 덮어쓰기를 지원하지 않는 경우 대비
                    LittleFS.remove("/recipes.json");
                    ok = LittleFS.rename("/recipes.tmp", "/recipes.json");
                }
                req->send(200, "application/json", ok ? "{\"ok\":true}" : "{\"ok\":false}");
            }
        }
    );

    // CORS preflight: 브라우저가 실제 요청 전에 보내는 OPTIONS를 200으로 응답
    g_server.onNotFound([](AsyncWebServerRequest* req){
        if (req->method() == HTTP_OPTIONS) {
            req->send(200);
        } else {
            req->send(404, "text/plain", "Not Found");
        }
    });
}

// ──────────────────────────────────────────────────────────────
// setup()
// ──────────────────────────────────────────────────────────────
void setup() {
    // 0) 모터 드라이버 EN 즉시 차단 (★ 물리 손상 방지 — 무조건 가장 먼저)
    //    TMC2209 EN은 active-low. 리셋~setup 도달 구간(+크래시/WDT 리셋)에
    //    PIN_EN이 floating LOW면 드라이버 enable → 코일 통전 발열/소손.
    //    (HW EN→3.3V 10K 풀업과 병행하면 플래싱·무전원 구간까지 안전)
    pinMode(PIN_EN, OUTPUT);
    digitalWrite(PIN_EN, HIGH);   // = disableMotor() : 코일 전류 차단

    Serial.begin(115200);
    delay(500);
    Serial.println("\n[Film Processor v2.2] 초기화 시작");

    // PIN_EN은 setup() 진입 즉시 차단 완료(위 0번). 여기선 재확인만.
    disableMotor();

    // MAX31865 CS 핀을 명시적으로 HIGH로 고정 — 부팅 직후 부유 상태 차단
    pinMode(PIN_CS, OUTPUT);
    digitalWrite(PIN_CS, HIGH);

    // /api/start 청크 조립 버퍼 사전 할당 (힙 단편화 억제)
    g_startBuf.reserve(4096);

    // FastAccelStepper 초기화 — 하드웨어 타이머 ISR 기반, WiFi 병목 영향 없음
    engine.init();
    stepper = engine.stepperConnectToPin(PIN_STEP);
    stepper->setDirectionPin(PIN_DIR);
    stepper->setAcceleration((uint32_t)ACCEL);
    stepper->setSpeedInHz((uint32_t)MAX_SPEED);  // 기본 최고속 설정
    Serial.printf("[Stepper] MAX_SPEED=%.0f steps/s (출력축 최대 %.0f RPM)\n",
                  MAX_SPEED, (float)MAX_OUTPUT_RPM);

    // MAX31865 초기화 — 3선식 PT100
    // ※ 보드 RREF는 실측값 412.0Ω (RREF/RNOMINAL define 참조)
    // ※ 4선식으로 변경 시: MAX31865_4WIRE + 보드 솔더점퍼도 함께 변경
    tempSensor.begin(MAX31865_3WIRE);
    Serial.println("[MAX31865] 초기화 완료 (3WIRE, RREF=412.0)");

    // Preferences에서 WiFi 설정 로드
    g_prefs.begin("wifi", true);
    g_apSSID      = g_prefs.getString("ap_ssid",    "FilmProcessor");
    g_apPass      = g_prefs.getString("ap_pass",     "12345678");
    g_staSSID     = g_prefs.getString("sta_ssid",    "");
    g_staPass     = g_prefs.getString("sta_pass",    "");
    g_staStatic   = g_prefs.getBool  ("sta_static",  false);
    g_staStaticIP = g_prefs.getString("sta_ip",      "192.168.1.100");
    g_staGW       = g_prefs.getString("sta_gw",      "192.168.1.1");
    g_staSN       = g_prefs.getString("sta_sn",      "255.255.255.0");
    g_staDNS      = g_prefs.getString("sta_dns",     "8.8.8.8");
    g_prefs.end();

    // WiFi: AP + STA 동시 모드
    WiFi.mode(WIFI_AP_STA);
    // STA 자동 재연결 + 끊김/연결 이벤트 핸들러 — 공유기 재부팅·일시 단절 대응
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.onEvent(onWiFiEvent);
    WiFi.softAPConfig(AP_IP, AP_GW, AP_SUB);
    WiFi.softAP(g_apSSID.c_str(), g_apPass.c_str());
    Serial.printf("[AP] SSID: %s  http://192.168.4.1\n", g_apSSID.c_str());

    // AP 연결 기기용 DNS 서버 — europrocessor.local → 192.168.4.1 응답
    // AP의 DHCP가 게이트웨이(192.168.4.1)를 DNS로 알려주므로 쿼리가 여기로 옴
    g_dnsServer.start(53, "europrocessor.local", AP_IP);
    Serial.println("[DNS] AP DNS 서버 시작 (europrocessor.local → 192.168.4.1)");

    if (g_staSSID.length() > 0) {
        // 고정 IP 설정 (DHCP 대신 지정 주소 사용)
        if (g_staStatic) {
            IPAddress ip, gw, sn, dns;
            if (ip.fromString(g_staStaticIP) && gw.fromString(g_staGW) && sn.fromString(g_staSN)) {
                if (!dns.fromString(g_staDNS)) dns = gw;
                WiFi.config(ip, gw, sn, dns);
                Serial.printf("[STA] 고정 IP: %s / GW: %s\n", g_staStaticIP.c_str(), g_staGW.c_str());
            } else {
                Serial.println("[STA] 고정 IP 주소 오류 — DHCP로 대체");
            }
        }
        WiFi.begin(g_staSSID.c_str(), g_staPass.c_str());
        Serial.printf("[STA] 연결 시도: %s (%s)\n", g_staSSID.c_str(), g_staStatic ? "고정 IP" : "DHCP");
    } else {
        Serial.println("[STA] 미설정 (설정 페이지에서 홈 WiFi 입력 가능)");
    }

    // LittleFS 초기화 (레시피 영구 저장)
    // ※ Arduino IDE: Tools → Partition Scheme → "8MB Flash (3MB APP/1.5MB SPIFFS)" 선택 필요
    // ※ formatOnFail=false: 마운트 실패 시 자동 포맷 금지 → 데이터 보호
    if (!LittleFS.begin(false)) {
        Serial.println("[LittleFS] 마운트 실패 — 레시피 저장 불가 (수동 포맷 필요 시 begin(true) 1회 실행)");
    } else {
        Serial.printf("[LittleFS] 마운트 성공 (여유 공간: %u KB)\n",
                      LittleFS.totalBytes() / 1024);
    }

    // 전역 CORS 헤더 — file:// 또는 외부 오리진(Recipe Editor)에서의 API 요청 허용
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    setupRoutes();
    g_server.begin();
    Serial.println("[HTTP] 서버 시작 완료");

    // mDNS 시작 — http://europrocessor.local 로 접속 가능
    if (MDNS.begin("europrocessor")) {
        MDNS.addService("http", "tcp", 80);
        Serial.println("[mDNS] http://europrocessor.local 등록 완료");
    } else {
        Serial.println("[mDNS] 시작 실패");
    }

    // 동기화 객체 생성
    // Core 0: WiFi 스택 + AsyncWebServer + tempTask (비시간-크리티컬)
    // Core 1: loop() → motorUpdate() / recipeUpdate() (시간-크리티컬)
    g_tempMux   = xSemaphoreCreateMutex();
    g_rCtxMux   = xSemaphoreCreateMutex();   // rCtx.steps 크로스코어 보호
    g_stagedMux = xSemaphoreCreateMutex();   // g_staged 양방향 보호

    // 명령 큐 — 웹 핸들러(Core 0)에서 loop(Core 1)로 전달
    // 깊이 8: 사용자가 빠르게 버튼을 눌러도 손실 없이 직렬화
    g_cmdQueue = xQueueCreate(8, sizeof(Cmd));

    // 태스크 워치독 — loop 또는 tempTask가 6초 이상 응답 없으면 시스템 리셋
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    // arduino-esp32 v3.x: config 구조체 기반
    esp_task_wdt_config_t wdtCfg = {
        .timeout_ms     = 6000,
        .idle_core_mask = 0,
        .trigger_panic  = true,
    };
    esp_task_wdt_init(&wdtCfg);
#else
    // arduino-esp32 v2.x: (timeout_sec, panic)
    esp_task_wdt_init(6, true);
#endif
    esp_task_wdt_add(nullptr);   // 현재 setup/loop 태스크 등록

    xTaskCreatePinnedToCore(
        tempTask,       // 태스크 함수
        "TempTask",     // 태스크 이름
        3072,           // 스택 크기 (bytes)
        nullptr,        // 파라미터
        1,              // 우선순위 (낮음 — 모터보다 덜 중요)
        nullptr,        // 핸들 (불필요)
        0               // Core 0 고정
    );
    Serial.println("[Core] tempTask → Core 0 시작");

    // [무디스플레이 변종] DisplayTask 없음 — 제어는 웹 UI 전용
    Serial.println("[Core] Headless build — control via web UI only\n");
}

// ──────────────────────────────────────────────────────────────
// loop()  — Core 1 전용, 모터 제어만 담당
// 온도 측정은 Core 0의 tempTask로 이동 → SPI 지연 없음
// FastAccelStepper 하드웨어 ISR이 스텝 펄스를 독립 처리 — loop() 부하 무관
// ──────────────────────────────────────────────────────────────
void loop() {
    // 워치독 reset — 6초 이상 loop 멈추면 시스템 자동 리셋
    esp_task_wdt_reset();

    // ── 0) 비상정지 최우선 처리 (큐 우회 — 절대 유실 금지) ──────────────────
    if (g_estop) { g_estop = false; emergencyStop(); }

    // ── 1) 웹 명령 큐 비우기 (모든 모터/레시피 변경은 여기서만) ──────────────
    drainCmdQueue();

    // ── 2) 스테이징 레시피 적용 ──────────────────────────────────────────
    // g_staged 쓰기는 핸들러가 g_stagedMux 안에서 ready=true 까지 완료한 뒤이므로,
    // 여기서 mutex 안에서 ready를 확인하고 즉시 move 하면 Core 0의 다음 쓰기와도 안전.
    if (g_staged.ready) {
        std::vector<StepInfo> steps;
        String                name;
        bool                  got = false;
        if (xSemaphoreTake(g_stagedMux, pdMS_TO_TICKS(20)) == pdTRUE) {
            if (g_staged.ready) {
                g_staged.ready = false;
                name  = g_staged.name;
                steps = std::move(g_staged.steps);  // 이후 g_staged.steps는 빈 상태
                got   = true;
            }
            xSemaphoreGive(g_stagedMux);
        }
        if (got) {
            emergencyStop();   // 현재 레시피/모터 즉시 정지 후 rCtx 초기화
            // rCtx.steps 교체는 g_rCtxMux로 buildStatus(Core 0)와 직렬화
            if (xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(10)) == pdTRUE) {
                rCtx.name  = name;
                rCtx.steps = std::move(steps);
                xSemaphoreGive(g_rCtxMux);
            }
            if (!rCtx.steps.empty()) {
                rCtx.running = true;
                startStep(0);
            }
        }
    }

    g_dnsServer.processNextRequest();  // AP DNS 쿼리 처리 (비차단, 수 μs 이내)
    motorUpdate();    // 상태 머신 처리 (스텝 펄스는 FastAccelStepper ISR이 독립 처리)
    recipeUpdate();   // 레시피 단계 시간 체크
}
