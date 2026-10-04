// ================================================================
//  PinMap.h — 보드별 핀 배치 (★배선이 다른 보드는 여기만 수정★)
//  ----------------------------------------------------------------
//  펌웨어 하나로 모든 바디 지원. 부팅 시 칩 MAC으로 BOARDS[]에서 배치를 고르고,
//  등록되지 않은 MAC이면 DEFAULT_PINS 사용. (OTA로 같은 .bin을 어느 보드에 올려도 안전)
//
//  [새 보드 등록]
//   1) 보드 MAC 확인: 웹 설정 → 디바이스 정보 "MAC", 또는 TFT System info, 시리얼 [Pins] 로그
//   2) BOARDS[]에 { "MAC", "이름", 배치 } 한 줄 추가 (DEFAULT_PINS 복사 후 다른 핀만 수정)
//   3) 빌드 — 금지 핀/중복 핀/인코더 핀(<32) 위반이면 컴파일 에러로 알려줌
//
//  [금지 GPIO] strapping 0/3/45/46, USB 19/20, Flash 26~32, OPI PSRAM 33~37,
//              UART0 43/44 (크래시 로그용 TTL), RGB LED 48, 존재하지 않는 >48
//
//  [기본 배치 — 터미널 보드 실측]
//    · A변: 3v3 [g12 g11 g10 g9] g46 g3 g20 g19 [g8 g18 g17 g16 g15 g7] g6 g5 g4 en 3v3 gnd
//           → TFT(12·11·10·9·8·18) + EC11(17·16·15) + KO(7)
//    · B변: v5in [g13 g14 g21 g47] g48 g45 g0 g35 g36 g37 g38 g39 [g40 g41 g42] rx tx g2 g1 gnd
//           → MAX31865(13·14·21·47) + TMC2209(42·41·40)
// ================================================================
#pragma once
#include <Arduino.h>

struct PinMap {
    // TMC2209 스테퍼
    uint8_t STEP, DIR, EN;                       // EN: LOW=코일 활성, HIGH=차단
    // MAX31865 PT100 (소프트웨어 SPI)
    uint8_t MAX_CS, MAX_MOSI, MAX_MISO, MAX_CLK;
    // ST7789 TFT (하드웨어 SPI, GPIO 매트릭스라 임의 핀 가능)
    uint8_t TFT_SCK, TFT_MOSI, TFT_CS, TFT_DC, TFT_RST, TFT_BL;
    // EC11 인코더 + 버튼 (ENC_A/B는 GPIO 0~31만 — ISR이 GPIO_IN_REG 직접 읽음)
    uint8_t ENC_A, ENC_B, ENC_PUSH, KEY_OK;
};

//                              STEP DIR EN | CS MOSI MISO CLK | SCK MOSI CS DC RST BL | ENC_A ENC_B PUSH OK
constexpr PinMap DEFAULT_PINS = { 42, 41, 40,  13,  14,  21,  47,   12,  11,  10, 9,  8, 18,    17,   16,  15,  7 };

struct BoardPins {
    const char* mac;         // "AA:BB:CC:DD:EE:FF" (대문자)
    const char* label;       // 로그/웹 표시용
    PinMap      pins;
    bool        hasDisplay;  // false = TFT·인코더·버튼 없음 → 해당 핀 미사용(구동 안 함), 화면 태스크 미기동
    uint8_t     rtdWires;    // MAX31865 PT100 결선: 0 = 센서 없음/격리(미초기화), 2/3/4 = 결선 (★보드 솔더점퍼와 일치★)
    // ── 하드웨어 감시 (배선했을 때만, 0 = 없음) ──
    uint8_t     diagPin;     // TMC2209 DIAG (HIGH = 과열·단락 등 드라이버 고장) — 외부 10k 풀다운 권장
    uint8_t     vmPin;       // 모터 전원 VM 분압 ADC 핀 (ADC1: GPIO1~10만)
    float       vmRatio;     // 분압비 (예: 100k/10k → 11.0) — VM = 핀전압 × vmRatio
};

// ★보드별 배치 — 배선이 기본과 다른 보드만 추가★
//   무디스플레이 보드는 앞 7개(STEP DIR EN | CS MOSI MISO CLK)만 쓰고 나머지는 생략(0) + hasDisplay=false
constexpr BoardPins BOARDS[] = {
    { "A0:F2:62:E5:D9:B0", "바디1 (기본 배선)", DEFAULT_PINS, true, 0 },   // MAX31865 보드 불량 → 격리
    // 구버전 무디스플레이 바디 (v2.2~v3.1.3 rotary_processor_nodisplay.ino 핀 정의)
    { "D0:CF:13:59:14:78", "바디2 (무디스플레이)", { 5, 6, 7,  2, 38, 39, 40 }, false, 2 },   // 임시 2선 모드: 3선 결선(A)이지만 보드 3선 점퍼 구조 확인 전까지 (리드선만큼 ~1°C 높게 읽힘)
};

// 부팅 시 선택된 배치 (MAC 매칭, 없으면 DEFAULT_PINS). 정적 초기화 중 호출해도 안전.
const PinMap& pins();
const char*   pinProfileLabel();
const char*   boardMac();          // "AA:BB:CC:DD:EE:FF"
bool          boardHasDisplay();   // 미등록 보드는 true (기본 배선 = 디스플레이 버전)
uint8_t       boardRtdWires();     // 0 = 온도센서 미사용. 미등록 보드는 0 (안전)
uint8_t       boardDiagPin();      // 0 = 미배선
uint8_t       boardVmPin();        // 0 = 미배선
float         boardVmRatio();

// ── 컴파일 타임 검증 (C++11 constexpr — 재귀) ─────────────────────
namespace pinmap_check {
    constexpr int N = 17;
    constexpr uint8_t at(const PinMap& m, int i) {
        return i == 0 ? m.STEP : i == 1 ? m.DIR : i == 2 ? m.EN :
               i == 3 ? m.MAX_CS : i == 4 ? m.MAX_MOSI : i == 5 ? m.MAX_MISO : i == 6 ? m.MAX_CLK :
               i == 7 ? m.TFT_SCK : i == 8 ? m.TFT_MOSI : i == 9 ? m.TFT_CS : i == 10 ? m.TFT_DC :
               i == 11 ? m.TFT_RST : i == 12 ? m.TFT_BL :
               i == 13 ? m.ENC_A : i == 14 ? m.ENC_B : i == 15 ? m.ENC_PUSH : m.KEY_OK;
    }
    constexpr bool forbidden(uint8_t p) {
        return p == 0 || p == 3 || p == 45 || p == 46 || p == 19 || p == 20 ||
               (p >= 26 && p <= 37) || p == 43 || p == 44 || p >= 48;
    }
    constexpr int N_CORE = 7;   // 무디스플레이: STEP DIR EN + MAX31865 4핀만 검사
    constexpr bool anyForbidden(const PinMap& m, int n, int i = 0) {
        return i >= n ? false : (forbidden(at(m, i)) || anyForbidden(m, n, i + 1));
    }
    constexpr bool dupFrom(const PinMap& m, int n, int i, int j) {
        return j >= n ? false : (at(m, i) == at(m, j) || dupFrom(m, n, i, j + 1));
    }
    constexpr bool anyDup(const PinMap& m, int n, int i = 0) {
        return i >= n ? false : (dupFrom(m, n, i, i + 1) || anyDup(m, n, i + 1));
    }
    constexpr bool valid(const PinMap& m, bool hasDisplay = true) {
        return hasDisplay ? (!anyForbidden(m, N) && !anyDup(m, N) && m.ENC_A < 32 && m.ENC_B < 32)
                          : (!anyForbidden(m, N_CORE) && !anyDup(m, N_CORE));
    }
    constexpr int COUNT = sizeof(BOARDS) / sizeof(BOARDS[0]);
    constexpr bool wiresOk(uint8_t w) { return w == 0 || w == 2 || w == 3 || w == 4; }
    constexpr bool inPins(const PinMap& m, int n, uint8_t p, int i = 0) {
        return i >= n ? false : (at(m, i) == p || inPins(m, n, p, i + 1));
    }
    // 감시 핀: 0(없음) 또는 금지 GPIO 아님 + 기존 핀과 안 겹침. VM은 ADC1(1~10) + 분압비 > 1, DIAG와도 안 겹침
    constexpr bool extraOk(const BoardPins& b) {
        return (b.diagPin == 0 || (!forbidden(b.diagPin) && !inPins(b.pins, b.hasDisplay ? N : N_CORE, b.diagPin)))
            && (b.vmPin == 0 || (b.vmPin >= 1 && b.vmPin <= 10 && b.vmRatio > 1.0f && b.vmPin != b.diagPin
                                 && !inPins(b.pins, b.hasDisplay ? N : N_CORE, b.vmPin)));
    }
    constexpr bool allValid(int i = 0) {
        return i >= COUNT ? true : (valid(BOARDS[i].pins, BOARDS[i].hasDisplay) && wiresOk(BOARDS[i].rtdWires)
                                    && extraOk(BOARDS[i]) && allValid(i + 1));
    }
}
static_assert(pinmap_check::valid(DEFAULT_PINS),
              "DEFAULT_PINS: 금지 GPIO / 중복 핀 / 인코더 핀(>=32) 확인");
static_assert(pinmap_check::allValid(),
              "BOARDS[]: 금지 GPIO(0,3,19,20,26~37,43~48) / 중복 핀 / 인코더 핀(>=32) 확인");
