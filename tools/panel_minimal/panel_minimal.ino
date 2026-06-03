/*
 * ================================================================
 *  panel_minimal.ino — ST7789 panel 응답 진단 전용 minimal 스케치
 *
 *  목적: panel/SPI/배선이 정상인지 격리해서 확인.
 *        화면을 RED/GREEN/BLUE/WHITE/BLACK 으로 1초씩 순환 표시.
 *
 *  결과 해석:
 *    - 색이 정상 순환  → panel/SPI/배선 모두 정상
 *    - 흰 화면 고정    → SPI 명령이 panel에 안 닿음 (배선/모듈 문제)
 *    - 검정 화면 고정  → 픽셀 데이터 미수신
 *    - 색 부분만 / 노이즈 → SPI 신뢰성 문제
 *
 *  필요 라이브러리: Adafruit GFX, Adafruit ST7789
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

// 예비 후보 핀 — 디스플레이 분리 상태에서 건강한 GPIO 선별용
//   (DC 이설 대상 GPIO13 + 대안 GPIO8/4/1 동시 측정)
static const int SPARE_PINS[] = { 13, 8, 4, 1 };
static const int SPARE_COUNT  = sizeof(SPARE_PINS) / sizeof(SPARE_PINS[0]);

Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("\n[PanelTest] minimal 진단 시작");

    // ── 진단 모드: GPIO 핀 강제 토글 (panel 라이브러리 우회) ──
    //   5초간 DC/CS/RST/SCK/MOSI/BL 모두 0.5초씩 LOW/HIGH 토글.
    //   각 panel 핀에 멀티미터 대고 0V ↔ 3.3V 왕복하는지 확인 가능.
    //   토글 안 되는 핀 = 그 GPIO 또는 와이어 끊김.
    // ── 측정 모드: 각 레벨을 8초씩 정지 유지 ──
    //   멀티미터가 안정될 시간 확보 (0.5Hz 토글은 DMM이 못 따라감).
    //   HIGH 8초 / LOW 8초를 3사이클 → 각 핀에 프로브 대고 천천히 읽기.
    //   정상: HIGH=3.3V, LOW=0V.  손상 핀: HIGH가 1~2V대에 머무름.
    Serial.println("[PanelTest] === 핀 레벨 측정 모드 (HIGH 8s / LOW 8s × 3) ===");
    pinMode(PIN_TFT_BL, OUTPUT);
    pinMode(PIN_TFT_RST, OUTPUT);
    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_TFT_DC, OUTPUT);
    pinMode(PIN_TFT_SCK, OUTPUT);
    pinMode(PIN_TFT_MOSI, OUTPUT);
    for (int i = 0; i < SPARE_COUNT; i++) pinMode(SPARE_PINS[i], OUTPUT);
    Serial.printf("[PanelTest] 측정 핀: TFT(DC=%d CS=%d RST=%d SCK=%d MOSI=%d BL=%d)"
                  " + 예비(13/8/4/1)\n",
                  PIN_TFT_DC, PIN_TFT_CS, PIN_TFT_RST, PIN_TFT_SCK, PIN_TFT_MOSI, PIN_TFT_BL);
    for (int i = 0; i < 6; i++) {
        int v = (i & 1) ? LOW : HIGH;   // HIGH로 시작
        digitalWrite(PIN_TFT_BL,   v);
        digitalWrite(PIN_TFT_RST,  v);
        digitalWrite(PIN_TFT_CS,   v);
        digitalWrite(PIN_TFT_DC,   v);
        digitalWrite(PIN_TFT_SCK,  v);
        digitalWrite(PIN_TFT_MOSI, v);
        for (int j = 0; j < SPARE_COUNT; j++) digitalWrite(SPARE_PINS[j], v);
        Serial.printf("[PanelTest] >>> 지금 모든 핀 = %s (8초 유지, 측정하세요) <<<\n",
                      v ? "HIGH=3.3V 기대" : "LOW=0V 기대");
        delay(8000);
    }
    Serial.println("[PanelTest] === 측정 완료, panel init 시작 ===");

    digitalWrite(PIN_TFT_BL, LOW);
    Serial.println("[PanelTest] step1 BL=LOW");

    digitalWrite(PIN_TFT_RST, HIGH); delay(50);
    digitalWrite(PIN_TFT_RST, LOW);  delay(100);
    digitalWrite(PIN_TFT_RST, HIGH); delay(300);
    Serial.println("[PanelTest] step2 RST pulse done");

    SPI.begin(PIN_TFT_SCK, -1, PIN_TFT_MOSI);
    Serial.println("[PanelTest] step3 SPI.begin");

    tft.init(240, 320);
    tft.setSPISpeed(5000000);
    tft.setRotation(1);
    Serial.println("[PanelTest] step4 tft.init done @5MHz");

    digitalWrite(PIN_TFT_BL, HIGH);
    Serial.println("[PanelTest] step5 BL=HIGH, 색상 순환 시작");
}

void loop() {
    static const uint16_t cols[]  = { 0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000 };
    static const char*    names[] = { "RED",   "GREEN", "BLUE",  "WHITE", "BLACK" };
    static int idx = 0;

    tft.fillScreen(cols[idx]);
    // 좌상단 마커 — 색 변화 + 텍스트로 panel 응답 가시화
    tft.fillRect(0, 0, 110, 28, 0xFFFF);
    tft.setTextColor(0x0000);
    tft.setTextSize(2);
    tft.setCursor(4, 6);
    tft.print(names[idx]);

    Serial.printf("[PanelTest] %s\n", names[idx]);
    idx = (idx + 1) % 5;
    delay(1000);
}
