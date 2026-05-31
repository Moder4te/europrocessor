/*
 * ================================================================
 *  display_test_standalone.ino
 *  ─────────────────────────────────────────────────────────────
 *  디스플레이 모듈 단독 테스트 스케치
 *  (TMC2209 / MAX31865 / WiFi 없이 TFT + EC11 + KO 버튼만으로 동작)
 *
 *  [필요 라이브러리]
 *    - Adafruit GFX        (Library Manager)
 *    - Adafruit ST7789     (Library Manager)
 *
 *  [핀 배정 — rotary_processor.ino 와 동일]
 *    TFT ST7789  SCK=12  MOSI=11  CS=10  DC=9  RST=14  BL=21
 *    EC11        A=15    B=16     PUSH=17
 *    KO 버튼     OK=18
 *
 *  [테스트 내용]
 *    - 부팅 시 스플래시 → 메인 메뉴
 *    - 인코더 회전: 커서 이동
 *    - PUSH: 편집 화면 진입 / 토글 / 정보 진입 / 취소
 *    - KO: 확정 / 저장 / Start(화면에만 표시) / Stop
 *    - Serial: 모든 입력 이벤트 로그
 * ================================================================
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SPI.h>

// ──────────────────────────────────────────────────────────────
// 핀 정의
// ──────────────────────────────────────────────────────────────
#define PIN_TFT_SCK   12
#define PIN_TFT_MOSI  11
#define PIN_TFT_CS    10
#define PIN_TFT_DC     9
#define PIN_TFT_RST   14
#define PIN_TFT_BL    21

#define PIN_ENC_A     15
#define PIN_ENC_B     16
#define PIN_ENC_PUSH  17
#define PIN_KEY_OK    18

// ──────────────────────────────────────────────────────────────
// 색상 (RGB565)
// ──────────────────────────────────────────────────────────────
#define COL_BG      0x0000
#define COL_FG      0xFFFF
#define COL_AMBER   0xFD00
#define COL_GREEN   0x07E0
#define COL_BLUE    0x041F
#define COL_RED     0xF800
#define COL_GRAY    0x8410
#define COL_DGRAY   0x2104

// ──────────────────────────────────────────────────────────────
// 모터 파라미터 (실제 구동 없음 — 화면 표시 전용)
// ──────────────────────────────────────────────────────────────
#define MIN_OUTPUT_RPM  5
#define MAX_OUTPUT_RPM  100

// ──────────────────────────────────────────────────────────────
// TFT 객체
// ──────────────────────────────────────────────────────────────
static Adafruit_ST7789 tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// ──────────────────────────────────────────────────────────────
// 로터리 인코더 — 인터럽트 기반 quadrature 디코딩
// ──────────────────────────────────────────────────────────────
static volatile int16_t g_encAccum = 0;
static volatile uint8_t g_encState = 0;
static const int8_t ENC_TABLE[16] = {
     0,-1, 1, 0,
     1, 0, 0,-1,
    -1, 0, 0, 1,
     0, 1,-1, 0
};

static void IRAM_ATTR encISR() {
    uint8_t a = digitalRead(PIN_ENC_A);
    uint8_t b = digitalRead(PIN_ENC_B);
    g_encState = ((g_encState << 2) & 0x0F) | ((a << 1) | b);
    g_encAccum += ENC_TABLE[g_encState];
}

static int8_t popEncoderSteps() {
    noInterrupts();
    int16_t acc   = g_encAccum;
    int16_t steps = acc / 4;
    g_encAccum    = acc - (steps * 4);
    interrupts();
    if (steps >  127) steps =  127;
    if (steps < -128) steps = -128;
    return (int8_t)steps;
}

// ──────────────────────────────────────────────────────────────
// 버튼 디바운싱 (폴링)
// ──────────────────────────────────────────────────────────────
struct Btn {
    uint8_t  pin;
    uint8_t  state;
    uint8_t  lastRead;
    uint32_t lastEdgeMs;
    bool     pressedEvt;
};
static Btn btnPush = {PIN_ENC_PUSH, HIGH, HIGH, 0, false};
static Btn btnOk   = {PIN_KEY_OK,   HIGH, HIGH, 0, false};
static const uint16_t BTN_DEBOUNCE_MS = 25;

static void btnPoll(Btn& b) {
    uint8_t  r   = digitalRead(b.pin);
    uint32_t now = millis();
    if (r != b.lastRead) { b.lastEdgeMs = now; b.lastRead = r; }
    if ((now - b.lastEdgeMs) >= BTN_DEBOUNCE_MS && r != b.state) {
        b.state = r;
        if (b.state == LOW) b.pressedEvt = true;
    }
}
static bool btnConsume(Btn& b) {
    if (b.pressedEvt) { b.pressedEvt = false; return true; }
    return false;
}

// ──────────────────────────────────────────────────────────────
// UI 상태 — 실제 fw와 동일 구조 유지
// ──────────────────────────────────────────────────────────────
enum UiPage : uint8_t {
    PAGE_MAIN_MENU,
    PAGE_EDIT_SPEED,
    PAGE_EDIT_ROTSEC,
    PAGE_INFO,
    PAGE_RUNNING,   // Start 실행 후 더미 상태창
};

struct UiSettings {
    int  rpm    = 50;
    int  rotSec = 30;
    bool fwd    = true;
    bool cycle  = true;
} g_uiSet;

struct UiCtx {
    UiPage  page        = PAGE_MAIN_MENU;
    int8_t  cursor      = 0;
    int     editValue   = 0;
    int     editMin     = 0;
    int     editMax     = 0;
    bool    dirty       = true;
    bool    pageChanged = true;
    bool    motorOn     = false;   // 더미 모터 상태
} g_ui;

enum MainIdx { IDX_START=0, IDX_STOP, IDX_SPEED, IDX_PERIOD, IDX_DIR, IDX_CYCLE, IDX_INFO };
static const uint8_t MAIN_ITEM_COUNT = 7;

// ──────────────────────────────────────────────────────────────
// 렌더 헬퍼
// ──────────────────────────────────────────────────────────────
static inline void clearArea(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COL_BG);
}

static void drawHeader(const char* title, uint16_t bg) {
    tft.fillRect(0, 0, 320, 32, bg);
    tft.setTextColor(COL_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 8);
    tft.print(title);
}

static void drawFooter(const char* hint, uint16_t bg) {
    tft.fillRect(0, 212, 320, 28, bg);
    tft.setTextColor(COL_FG);
    tft.setTextSize(1);
    tft.setCursor(8, 220);
    tft.print(hint);
}

static int wrapIndex(int v, int n) {
    while (v < 0) v += n;
    return v % n;
}

// ──────────────────────────────────────────────────────────────
// 스플래시 화면
// ──────────────────────────────────────────────────────────────
static void renderSplash() {
    tft.fillScreen(COL_BG);
    drawHeader("DISPLAY TEST", COL_AMBER);

    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(20, 60);  tft.print("ST7789 320x240");
    tft.setCursor(20, 84);  tft.print("EC11 Encoder");
    tft.setCursor(20, 108); tft.print("KO Button");

    tft.setTextSize(1);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setCursor(20, 148); tft.print("No TMC2209 / MAX31865 required");
    tft.setCursor(20, 164); tft.print("All display logic test only");

    tft.setTextColor(COL_AMBER, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(60, 190); tft.print("Starting in 2s...");
    delay(2000);
}

// ──────────────────────────────────────────────────────────────
// 메인 메뉴
// ──────────────────────────────────────────────────────────────
static void renderMenuChrome() {
    tft.fillScreen(COL_BG);
    drawHeader("OFFLINE  /  MANUAL", COL_AMBER);
    drawFooter("Turn:Move  PUSH:Enter  KO:Confirm", COL_DGRAY);
}

static void renderMenuItems() {
    char buf[40];
    for (uint8_t i = 0; i < MAIN_ITEM_COUNT; ++i) {
        int  y   = 40 + i * 24;
        bool sel = (i == g_ui.cursor);
        uint16_t bg = sel ? COL_AMBER : COL_BG;
        uint16_t fg = sel ? COL_BG    : COL_FG;
        tft.fillRect(0, y, 320, 24, bg);
        tft.setTextColor(fg, bg);
        tft.setTextSize(2);
        tft.setCursor(8, y + 4);
        tft.print(sel ? "> " : "  ");
        switch (i) {
            case IDX_START:  tft.print("Start"); break;
            case IDX_STOP:   tft.print("Stop");  break;
            case IDX_SPEED:
                snprintf(buf, sizeof(buf), "Speed     %3d RPM", g_uiSet.rpm);
                tft.print(buf); break;
            case IDX_PERIOD:
                snprintf(buf, sizeof(buf), "Period    %3d s", g_uiSet.rotSec);
                tft.print(buf); break;
            case IDX_DIR:
                snprintf(buf, sizeof(buf), "Dir       %s", g_uiSet.fwd ? "FWD" : "REV");
                tft.print(buf); break;
            case IDX_CYCLE:
                snprintf(buf, sizeof(buf), "Cycle     %s", g_uiSet.cycle ? "ON " : "OFF");
                tft.print(buf); break;
            case IDX_INFO:   tft.print("Info"); break;
        }
    }
}

// ──────────────────────────────────────────────────────────────
// 편집 화면
// ──────────────────────────────────────────────────────────────
static void renderEditChrome(const char* title) {
    tft.fillScreen(COL_BG);
    drawHeader(title, COL_BLUE);
    drawFooter("Turn:Change   KO:Save   PUSH:Cancel", COL_DGRAY);
}

static void renderEditValue(const char* unit) {
    char buf[16];
    clearArea(0, 60, 320, 130);
    snprintf(buf, sizeof(buf), "%d", g_ui.editValue);
    tft.setTextColor(COL_AMBER, COL_BG);
    tft.setTextSize(8);
    int textPx = (int)strlen(buf) * 8 * 6;
    tft.setCursor((320 - textPx) / 2, 88);
    tft.print(buf);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(3);
    int unitPx = (int)strlen(unit) * 3 * 6;
    tft.setCursor((320 - unitPx) / 2, 168);
    tft.print(unit);
}

// ──────────────────────────────────────────────────────────────
// 정보 화면
// ──────────────────────────────────────────────────────────────
static void renderInfoFull() {
    tft.fillScreen(COL_BG);
    drawHeader("SYSTEM INFO", COL_BLUE);
    drawFooter("KO or PUSH: Back", COL_DGRAY);

    char buf[64];
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(1);
    int y = 44;
    auto line = [&](const char* s){ tft.setCursor(8, y); tft.print(s); y += 14; };

    line("Firmware    : v2.2 [TEST MODE]");
    line("MCU         : ESP32-S3");
    line("Motor       : NEMA17 + TMC2209 (NOT connected)");
    line("Temp        : MAX31865 + PT100  (NOT connected)");
    line("Display     : ST7789 320x240 @ 40MHz");
    snprintf(buf, sizeof(buf), "Free heap   : %u B", (unsigned)ESP.getFreeHeap());
    line(buf);
    snprintf(buf, sizeof(buf), "Uptime      : %lu s", millis() / 1000UL);
    line(buf);
}

// ──────────────────────────────────────────────────────────────
// 더미 실행 상태창
// ──────────────────────────────────────────────────────────────
static void renderRunningChrome() {
    tft.fillScreen(COL_BG);
    drawHeader("RUNNING (TEST)", COL_GREEN);
    drawFooter("KO:Stop  [Motor not actually moving]", COL_DGRAY);

    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(8, 40);  tft.print("Speed:");
    tft.setCursor(8, 120); tft.print("Dir:");
    tft.setCursor(8, 160); tft.print("Period:");
    tft.setCursor(8, 180); tft.print("Cycle:");
}

static void renderRunningLive() {
    char buf[32];
    clearArea(60, 36,  220, 70);
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(6);
    snprintf(buf, sizeof(buf), "%3d", g_uiSet.rpm);
    tft.setCursor(60, 50);
    tft.print(buf);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(240, 70);
    tft.print("RPM");

    clearArea(40, 116, 180, 20);
    tft.setTextColor(g_uiSet.fwd ? COL_GREEN : COL_BLUE, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(40, 118);
    tft.print(g_uiSet.fwd ? "FWD" : "REV");

    clearArea(80, 156, 160, 40);
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(80, 160);
    snprintf(buf, sizeof(buf), "%d s", g_uiSet.rotSec);
    tft.print(buf);
    tft.setCursor(80, 180);
    tft.print(g_uiSet.cycle ? "ON" : "OFF");
}

// ──────────────────────────────────────────────────────────────
// 페이지 전환
// ──────────────────────────────────────────────────────────────
static void changePage(UiPage next) {
    g_ui.page        = next;
    g_ui.dirty       = true;
    g_ui.pageChanged = true;
}

static void enterEditPage(UiPage p, int v, int lo, int hi) {
    g_ui.editValue = v;
    g_ui.editMin   = lo;
    g_ui.editMax   = hi;
    changePage(p);
}

// ──────────────────────────────────────────────────────────────
// 입력 처리
// ──────────────────────────────────────────────────────────────
static void onMainPush() {
    switch (g_ui.cursor) {
        case IDX_SPEED:
            enterEditPage(PAGE_EDIT_SPEED, g_uiSet.rpm, MIN_OUTPUT_RPM, MAX_OUTPUT_RPM);
            break;
        case IDX_PERIOD:
            enterEditPage(PAGE_EDIT_ROTSEC, g_uiSet.rotSec, 5, 600);
            break;
        case IDX_DIR:
            g_uiSet.fwd = !g_uiSet.fwd;
            g_ui.dirty  = true;
            Serial.printf("[TEST] Dir toggled: %s\n", g_uiSet.fwd ? "FWD" : "REV");
            break;
        case IDX_CYCLE:
            g_uiSet.cycle = !g_uiSet.cycle;
            g_ui.dirty    = true;
            Serial.printf("[TEST] Cycle toggled: %s\n", g_uiSet.cycle ? "ON" : "OFF");
            break;
        case IDX_INFO:
            changePage(PAGE_INFO);
            break;
        default: break;
    }
}

static void onMainOk() {
    switch (g_ui.cursor) {
        case IDX_START:
            g_ui.motorOn = true;
            changePage(PAGE_RUNNING);
            Serial.printf("[TEST] Start: %dRPM %s cycle=%d period=%ds\n",
                          g_uiSet.rpm, g_uiSet.fwd ? "FWD" : "REV",
                          g_uiSet.cycle, g_uiSet.rotSec);
            break;
        case IDX_STOP:
            g_ui.motorOn = false;
            Serial.println("[TEST] Stop");
            g_ui.dirty = true;
            break;
        case IDX_DIR:
            g_uiSet.fwd = !g_uiSet.fwd;
            g_ui.dirty  = true;
            Serial.printf("[TEST] Dir: %s\n", g_uiSet.fwd ? "FWD" : "REV");
            break;
        case IDX_CYCLE:
            g_uiSet.cycle = !g_uiSet.cycle;
            g_ui.dirty    = true;
            Serial.printf("[TEST] Cycle: %s\n", g_uiSet.cycle ? "ON" : "OFF");
            break;
        default: break;
    }
}

static void onEditOk() {
    switch (g_ui.page) {
        case PAGE_EDIT_SPEED:
            g_uiSet.rpm = g_ui.editValue;
            Serial.printf("[TEST] Speed saved: %d RPM\n", g_uiSet.rpm);
            break;
        case PAGE_EDIT_ROTSEC:
            g_uiSet.rotSec = g_ui.editValue;
            Serial.printf("[TEST] Period saved: %d s\n", g_uiSet.rotSec);
            break;
        default: break;
    }
    changePage(PAGE_MAIN_MENU);
}

// ──────────────────────────────────────────────────────────────
// setup / loop
// ──────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    Serial.println("\n[Display Test] 초기화 시작");

    // TFT
    pinMode(PIN_TFT_BL, OUTPUT);
    digitalWrite(PIN_TFT_BL, HIGH);
    SPI.begin(PIN_TFT_SCK, /*MISO*/ -1, PIN_TFT_MOSI);
    tft.init(240, 320);
    tft.setSPISpeed(40000000);
    tft.setRotation(1);   // 가로 320x240
    tft.fillScreen(COL_BG);
    Serial.println("[Display Test] TFT OK");

    // 인코더 / 버튼
    pinMode(PIN_ENC_A,    INPUT_PULLUP);
    pinMode(PIN_ENC_B,    INPUT_PULLUP);
    pinMode(PIN_ENC_PUSH, INPUT_PULLUP);
    pinMode(PIN_KEY_OK,   INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);
    Serial.println("[Display Test] Encoder/Button OK");

    renderSplash();
    Serial.println("[Display Test] 메인 메뉴 진입");
}

void loop() {
    // 40ms 루프 (25 Hz) — delay 대신 millis 기반
    static uint32_t lastMs = 0;
    uint32_t now = millis();
    if (now - lastMs < 40) return;
    lastMs = now;

    // 입력
    btnPoll(btnPush);
    btnPoll(btnOk);
    int8_t delta  = popEncoderSteps();
    bool   pushed = btnConsume(btnPush);
    bool   oked   = btnConsume(btnOk);

    // 로그: 입력 이벤트 있을 때만
    if (delta != 0) Serial.printf("[ENC] delta=%+d\n", delta);
    if (pushed)     Serial.println("[BTN] PUSH");
    if (oked)       Serial.println("[BTN] KO");

    // 입력 처리
    switch (g_ui.page) {
        case PAGE_MAIN_MENU:
            if (delta != 0) {
                g_ui.cursor = (int8_t)wrapIndex(g_ui.cursor + delta, MAIN_ITEM_COUNT);
                g_ui.dirty  = true;
            }
            if (pushed) onMainPush();
            if (oked)   onMainOk();
            break;

        case PAGE_EDIT_SPEED:
        case PAGE_EDIT_ROTSEC:
            if (delta != 0) {
                g_ui.editValue = constrain(g_ui.editValue + delta,
                                           g_ui.editMin, g_ui.editMax);
                g_ui.dirty = true;
            }
            if (oked)   onEditOk();
            if (pushed) changePage(PAGE_MAIN_MENU);
            break;

        case PAGE_INFO:
            if (oked || pushed) changePage(PAGE_MAIN_MENU);
            break;

        case PAGE_RUNNING:
            if (oked) {
                g_ui.motorOn = false;
                changePage(PAGE_MAIN_MENU);
                Serial.println("[TEST] Stop (from running page)");
            }
            break;
    }

    // 페이지 전환 → chrome 재그리기
    if (g_ui.pageChanged) {
        switch (g_ui.page) {
            case PAGE_MAIN_MENU:   renderMenuChrome();          break;
            case PAGE_EDIT_SPEED:  renderEditChrome("Speed");   break;
            case PAGE_EDIT_ROTSEC: renderEditChrome("Period");  break;
            case PAGE_INFO:        renderInfoFull();            break;
            case PAGE_RUNNING:     renderRunningChrome();       break;
        }
        g_ui.pageChanged = false;
        g_ui.dirty       = true;
    }

    // 내용 부분 갱신
    if (g_ui.dirty) {
        switch (g_ui.page) {
            case PAGE_MAIN_MENU:   renderMenuItems();           break;
            case PAGE_EDIT_SPEED:  renderEditValue("RPM");      break;
            case PAGE_EDIT_ROTSEC: renderEditValue("sec");      break;
            case PAGE_INFO:        /* renderInfoFull이 그림 */  break;
            case PAGE_RUNNING:     renderRunningLive();         break;
        }
        g_ui.dirty = false;
    }
}
