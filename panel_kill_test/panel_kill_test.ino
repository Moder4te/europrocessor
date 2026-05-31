/*
 * ================================================================
 *  panel_kill_test.ino — ST7789 panel 사망 100% 확정 진단
 *
 *  필요 도구: 멀티미터 (mA 측정 모드)
 *  배선: VCC 와이어를 끊고 사이에 멀티미터(mA 모드) 직렬 연결
 *        + 기존 SCK/MOSI/CS/DC/RST/BL/GND 배선 유지
 *        (MISO 핀 필요 없음)
 *
 *  진단 원리:
 *    Stage A: 모든 신호 LOW + RST LOW 유지 → panel deep reset 상태
 *      → 정상 panel은 1-5mA만 흘러야 함 (컨트롤러 idle, BL OFF)
 *      → 100mA+ 이면 panel 다이 내부 단락 = 사망 확정
 *
 *    Stage B: BL만 HIGH → 백라이트 LED 단독 점등 확인
 *      → 시각으로 panel 모듈 빛 새어 나오는지 확인
 *      → 켜지지 않으면 BL 회로/LED 사망
 *
 *    Stage C: panel reset 해제 후 정상 init 시도
 *      → 색상이 바뀌면 panel 살아있음 (다른 문제)
 *      → 흰 화면 그대로 + 전류 높음 = 컨트롤러 사망
 *
 *  각 stage는 10초 유지 — 멀티미터 측정 + 시각 확인 시간 확보
 * ================================================================
 */
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

#define PIN_TFT_SCK   12
#define PIN_TFT_MOSI  11
#define PIN_TFT_CS    10
#define PIN_TFT_DC     9
#define PIN_TFT_RST   14
#define PIN_TFT_BL    21

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

static void stageBanner(const char* name, const char* expect, int waitSec) {
    Serial.println();
    Serial.println("====================================================");
    Serial.printf("  STAGE: %s\n", name);
    Serial.printf("  측정/확인 항목: %s\n", expect);
    Serial.printf("  %d초간 이 상태 유지 — 멀티미터로 측정하세요\n", waitSec);
    Serial.println("====================================================");
    for (int i = waitSec; i > 0; i--) {
        Serial.printf("  ... %d초 남음\n", i);
        delay(1000);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n[KillTest] ST7789 panel 사망 확정 진단 시작");
    Serial.println("[KillTest] VCC 라인에 멀티미터(mA 모드) 직렬 연결 필요");

    // 모든 핀 OUTPUT으로 설정 (panel 보호 차원)
    pinMode(PIN_TFT_BL,   OUTPUT);
    pinMode(PIN_TFT_CS,   OUTPUT);
    pinMode(PIN_TFT_DC,   OUTPUT);
    pinMode(PIN_TFT_RST,  OUTPUT);
    pinMode(PIN_TFT_SCK,  OUTPUT);
    pinMode(PIN_TFT_MOSI, OUTPUT);

    // ──────────────────────────────────────────────────────────────
    // STAGE A — Deep reset: 모든 신호 LOW + RST LOW (panel 완전 정지)
    // ──────────────────────────────────────────────────────────────
    digitalWrite(PIN_TFT_BL,   LOW);
    digitalWrite(PIN_TFT_CS,   LOW);
    digitalWrite(PIN_TFT_DC,   LOW);
    digitalWrite(PIN_TFT_RST,  LOW);   // ★ panel deep reset
    digitalWrite(PIN_TFT_SCK,  LOW);
    digitalWrite(PIN_TFT_MOSI, LOW);

    stageBanner("A - Deep Reset (panel 컨트롤러 정지 상태)",
                "VCC 전류 측정 → 1-5mA이면 정상, 50mA+ 이면 다이 내부 단락 = 사망 확정",
                10);

    // ──────────────────────────────────────────────────────────────
    // STAGE B — BL만 ON: 백라이트 LED 단독 점등
    // ──────────────────────────────────────────────────────────────
    digitalWrite(PIN_TFT_BL, HIGH);
    stageBanner("B - Backlight only (RST는 여전히 LOW)",
                "panel 모듈에서 백색 빛이 새어나오는지 시각 확인. + 전류 증가량 측정",
                10);

    // ──────────────────────────────────────────────────────────────
    // STAGE C — RST 해제 + idle (panel 깨어나기만 함, init 안 함)
    // ──────────────────────────────────────────────────────────────
    digitalWrite(PIN_TFT_RST, HIGH);
    delay(150);
    digitalWrite(PIN_TFT_CS, HIGH);    // CS idle
    digitalWrite(PIN_TFT_DC, HIGH);    // data 모드 idle
    stageBanner("C - RST 해제 + idle (init 안 함)",
                "정상 panel은 컨트롤러가 깨어나 5-20mA. 흰색 또는 random 픽셀 보임",
                10);

    // ──────────────────────────────────────────────────────────────
    // STAGE D — 정상 init 시도
    // ──────────────────────────────────────────────────────────────
    Serial.println("\n[KillTest] STAGE D - 정상 init + 색상 패턴");
    digitalWrite(PIN_TFT_RST, LOW);  delay(100);
    digitalWrite(PIN_TFT_RST, HIGH); delay(300);
    SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI);
    tft.init(240, 320);
    tft.setSPISpeed(10000000);
    tft.setRotation(1);
    Serial.println("[KillTest] init done — 색상 5종 표시");
}

void loop() {
    // 색상 사이클로 panel 응답 확인
    static const uint16_t cols[]  = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
    static const char*    names[] = { "RED",   "GREEN", "BLUE",  "WHITE", "BLACK" };
    static int idx = 0;

    tft.fillScreen(cols[idx]);
    tft.fillRect(0, 0, 110, 28, 0xFFFF);
    tft.setTextColor(0x0000);
    tft.setTextSize(2);
    tft.setCursor(4, 6);
    tft.print(names[idx]);
    Serial.printf("[KillTest D] %s\n", names[idx]);
    idx = (idx + 1) % 5;
    delay(2000);
}
