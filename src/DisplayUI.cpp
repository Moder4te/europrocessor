// ================================================================
//  DisplayUI.cpp — TFT UI v2 (미니멀 다크 테마 + 2버튼 일관 조작계)
//  ----------------------------------------------------------------
//  조작 규칙 (모든 페이지 공통):
//    TURN = 커서 이동 / 값 조절
//    PUSH = 선택 / 확정 (인코더 누름)
//    OK   = 뒤로 / 취소. 단 STATUS(홈)에선 상황별 퀵액션:
//           확인대기→다음 단계, 레시피 중→일시정지/재개,
//           수동 회전 중→안전정지, 대기 중→수동 퀵스타트
//
//  페이지 흐름:
//    STATUS ─PUSH→ MENU ─→ Run recipe → 카테고리 → 목록 → 미리보기→시작
//      │              ├─→ Manual control (Start/Speed/Period/Dir/Cycle/Stop)
//      │              └─→ Settings (Saver/Info)
//      └(레시피 실행 중) PUSH→ RUN CONTROL (Pause·Skip·Stop)
//
//  웹에서 저장한 /recipes.json(LittleFS)을 직접 탐색·실행.
//  실행 트리거는 RecipeStage 경유 (Core 1 loop이 consume).
// ================================================================
#include "DisplayUI.h"
#include "Config.h"
#include "nyan_frames.h"   // 임베드 Nyan Cat RGB565 프레임 (런타임 GIF 디코드 우회)

#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_task_wdt.h>
#include <vector>

// ──────────────────────────────────────────────────────────────
// 주입된 의존성 (begin에서 설정)
// ──────────────────────────────────────────────────────────────
static MotionController*  s_motion = nullptr;
static RecipeRunner*      s_recipe = nullptr;
static TemperatureSensor* s_temp   = nullptr;
static CommandQueue*      s_cmd    = nullptr;
static RecipeStage*       s_stage  = nullptr;

// 색상 (RGB565) — 다크 베이스 + 앰버 단일 액센트
#define COL_BG      0x0000
#define COL_FG      0xFFFF
#define COL_AMBER   0xFD00
#define COL_GREEN   0x07E0
#define COL_BLUE    0x041F
#define COL_RED     0xF800
#define COL_GRAY    0x8410
#define COL_DGRAY   0x2104

// TFT + 한글 폰트 엔진 (파일스코프 — C 콜백 접근용)
static Adafruit_ST7789       tft(Pin::TFT_CS, Pin::TFT_DC, Pin::TFT_RST);
static U8G2_FOR_ADAFRUIT_GFX u8g2;
static const int K_FONT_ASCENT = 13;

static void drawKText(int x, int y_top, const String& s, uint16_t fg, uint16_t bg) {
    if (!s.length()) return;
    u8g2.setFont(u8g2_font_unifont_t_korean1);
    u8g2.setFontMode(0);
    u8g2.setForegroundColor(fg);
    u8g2.setBackgroundColor(bg);
    u8g2.setCursor(x, y_top + K_FONT_ASCENT);
    u8g2.print(s.c_str());
}

// ──────────────────────────────────────────────────────────────
// 로터리 인코더 — 인터럽트 디코딩
// ──────────────────────────────────────────────────────────────
static volatile int16_t g_encAccum = 0;
static volatile uint8_t g_encState = 0;
static const int8_t ENC_TABLE[16] = {
     0,-1, 1, 0,  1, 0, 0,-1, -1, 0, 0, 1,  0, 1,-1, 0
};
static void IRAM_ATTR encISR() {
    uint8_t a = digitalRead(Pin::ENC_A);
    uint8_t b = digitalRead(Pin::ENC_B);
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
// 버튼 디바운싱
// ──────────────────────────────────────────────────────────────
struct Btn { uint8_t pin, state, lastRead; uint32_t lastEdgeMs; bool pressedEvt; };
static Btn btnPush = {Pin::ENC_PUSH, HIGH, HIGH, 0, false};
static Btn btnOk   = {Pin::KEY_OK,   HIGH, HIGH, 0, false};
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
// UI 상태
// ──────────────────────────────────────────────────────────────
enum UiPage : uint8_t {
    PAGE_STATUS, PAGE_MENU, PAGE_RUNCTL,
    PAGE_REC_CAT, PAGE_REC_LIST, PAGE_REC_CONFIRM,
    PAGE_MANUAL, PAGE_SETTINGS, PAGE_EDIT,
    PAGE_INFO, PAGE_SCREENSAVER
};

struct UiSettings {
    int  rpm      = 50;
    int  rotSec   = 30;
    bool fwd      = true;
    bool cycle    = true;
    bool saverOn  = true;
    int  saverSec = 60;
} g_uiSet;

static uint32_t g_lastInputMs = 0;
static inline void touchInput() { g_lastInputMs = millis(); }

static void loadSaverPrefs() {
    Preferences p;
    if (p.begin("ui", true)) {
        g_uiSet.saverOn  = p.getBool("svOn",  true);
        g_uiSet.saverSec = p.getInt ("svSec", 60);
        p.end();
    }
}
static void saveSaverPrefs() {
    Preferences p;
    if (p.begin("ui", false)) {
        p.putBool("svOn",  g_uiSet.saverOn);
        p.putInt ("svSec", g_uiSet.saverSec);
        p.end();
    }
}

struct UiCtx {
    UiPage   page              = PAGE_STATUS;
    int8_t   cursor            = 0;
    int8_t   listTop           = 0;     // 리스트 스크롤 오프셋
    bool     dirty             = true;
    bool     pageChanged       = true;
    uint32_t lastLiveRefreshMs = 0;
} g_ui;

static void changePage(UiPage next) {
    g_ui.page = next; g_ui.dirty = true; g_ui.pageChanged = true;
}
// 새 페이지 진입 (커서 초기화). 뒤로 복귀 시엔 changePage로 커서 보존.
static void gotoPage(UiPage next) {
    g_ui.cursor = 0; g_ui.listTop = 0; changePage(next);
}

static int wrapIndex(int v, int n) { while (v < 0) v += n; return v % n; }

// ── 숫자 편집 컨텍스트 (Speed/Period/SaverSec 공용) ──
enum : uint8_t { EDIT_SPEED, EDIT_PERIOD, EDIT_SAVER };
struct EditCtx {
    uint8_t     target = EDIT_SPEED;
    const char* title  = "";
    const char* unit   = "";
    int         val = 0, lo = 0, hi = 0, step = 1;
    UiPage      back = PAGE_MENU;
} g_edit;

static void openEdit(uint8_t target, const char* title, const char* unit,
                     int val, int lo, int hi, int step, UiPage back) {
    g_edit.target = target; g_edit.title = title; g_edit.unit = unit;
    g_edit.val = val; g_edit.lo = lo; g_edit.hi = hi; g_edit.step = step;
    g_edit.back = back;
    changePage(PAGE_EDIT);
}

// ── 레시피 탐색 상태 (/recipes.json) ──
static const char* REC_CATS[4] = { "B&W", "C-41", "ECN-2", "E-6" };
struct RecipeMeta { String name; int steps; long totalSec; };
static std::vector<RecipeMeta> g_recList;     // 현재 카테고리 레시피 목록
static int  g_catCount[4] = {0, 0, 0, 0};
static int  g_recCat = 0;
// 시작 확인 페이지용 — 선택 레시피 전체 단계
static String                g_cfName;
static std::vector<StepInfo> g_cfSteps;

// ──────────────────────────────────────────────────────────────
// 화면보호기 디코더
// ──────────────────────────────────────────────────────────────
static AnimatedGIF g_gif;
static File        g_gifFile;
static bool        g_gifActive   = false;
static uint8_t*    g_gifFrameBuf = nullptr;   // PSRAM 프레임버퍼 (COOKED: 디스포절/투명/최적화 처리)
static uint8_t*    g_gifData     = nullptr;   // PSRAM에 통째로 올린 GIF 원본 (RAM open — 파일콜백 우회)
static bool        g_gifCooked   = false;     // true=라이브러리가 변환한 RGB565 라인 직접 blit
static int         g_gifLineCount = 0;        // [진단] playFrame당 gifDraw 호출 수
// 화면보호기 애니메이션 상태 (임베드 Nyan 또는 업로드된 /saver.anim)
static int         g_nyanFrame   = 0;          // 현재 재생 프레임 인덱스 (양쪽 공용)
static uint32_t    g_nyanLastMs  = 0;
static bool        g_saverStatic = false;      // true=정적 JPEG (애니메이션 정지)
static bool        g_useAnim     = false;      // true=업로드 anim, false=임베드 Nyan
static uint8_t*    g_animData    = nullptr;    // PSRAM 적재된 /saver.anim 원본
static int         g_animW = 0, g_animH = 0, g_animFrames = 0, g_animDelay = 70;

static bool tjpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return false;
    tft.drawRGBBitmap(x, y, bitmap, w, h);
    return true;
}
static void* gifOpen(const char* fname, int32_t* pSize) {
    g_gifFile = LittleFS.open(fname, "r");
    if (!g_gifFile) return nullptr;
    *pSize = g_gifFile.size();
    return (void*)&g_gifFile;
}
static void gifClose(void*) { if (g_gifFile) g_gifFile.close(); }
static int32_t gifRead(GIFFILE* gif, uint8_t* p, int32_t len) {
    File* f = (File*)gif->fHandle;
    int32_t left = gif->iSize - gif->iPos;
    if (len > left) len = left - 1;   // ★AnimatedGIF 요구: EOF 직전 1바이트 남겨야 LZW 정상★
    if (len <= 0) return 0;
    int32_t n = f->read(p, len);
    gif->iPos = f->position();
    return n;
}
static int32_t gifSeek(GIFFILE* gif, int32_t pos) {
    File* f = (File*)gif->fHandle;
    f->seek(pos);
    gif->iPos = pos;
    return pos;
}
static void gifDraw(GIFDRAW* d) {
    g_gifLineCount++;                  // [진단] 콜백 호출 카운트
    int y = d->iY + d->y;
    if (y < 0 || y >= 240) return;
    int w = d->iWidth;
    if (w > 320) w = 320;

    if (g_gifCooked) {
        // COOKED: 라이브러리가 디스포절·투명·인터레이스 처리 후 변환한 RGB565 라인
        tft.drawRGBBitmap(d->iX, y, (uint16_t*)d->pPixels, w, 1);
        return;
    }
    // RAW 폴백 (프레임버퍼 할당 실패 시) — 팔레트 직접 변환
    uint16_t lineBuf[320];
    uint8_t* s = d->pPixels;
    uint16_t* pal = (uint16_t*)d->pPalette;
    if (d->ucHasTransparency) {
        uint8_t tcol = d->ucTransparent;
        for (int x = 0; x < w; ++x) { uint8_t c = s[x]; lineBuf[x] = (c == tcol) ? COL_BG : pal[c]; }
    } else {
        for (int x = 0; x < w; ++x) lineBuf[x] = pal[s[x]];
    }
    tft.drawRGBBitmap(d->iX, y, lineBuf, w, 1);
}

// ──────────────────────────────────────────────────────────────
// 렌더 프리미티브 — 미니멀 다크 스타일
// ──────────────────────────────────────────────────────────────
static const int FOOT_Y  = 218;
static const int LIST_Y0 = 38;
static const int ROW_H   = 32;
static const int ROW_VIS = 5;

static inline void clearArea(int x, int y, int w, int h) { tft.fillRect(x, y, w, h, COL_BG); }

// 전체 페이지 렌더 시작점: 화면 클리어 + 슬림 헤더 (제목 + 액센트 라인)
static void uiHeader(const char* title) {
    tft.fillScreen(COL_BG);
    tft.setTextSize(2);
    tft.setTextColor(COL_FG, COL_BG);
    tft.setCursor(10, 6);
    tft.print(title);
    tft.drawFastHLine(0, 27, 320, COL_DGRAY);
    tft.drawFastHLine(10, 27, 26, COL_AMBER);
}
static void uiFooter(const char* hint) {
    tft.fillRect(0, FOOT_Y, 320, 240 - FOOT_Y, COL_BG);
    tft.drawFastHLine(0, FOOT_Y, 320, COL_DGRAY);
    tft.setTextSize(1);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setCursor(10, FOOT_Y + 8);
    tft.print(hint);
}

// 선택 pill (라운드 사각 + 좌측 앰버 바)
static void rowPill(int y, bool sel) {
    if (sel) {
        tft.fillRoundRect(6, y, 306, ROW_H - 4, 6, COL_DGRAY);
        tft.fillRoundRect(6, y, 4, ROW_H - 4, 2, COL_AMBER);
    } else {
        tft.fillRect(6, y, 306, ROW_H - 4, COL_BG);
    }
}
static void drawRowAscii(int visIdx, const char* label, const char* value, bool sel) {
    int y = LIST_Y0 + visIdx * ROW_H;
    rowPill(y, sel);
    uint16_t bg = sel ? COL_DGRAY : COL_BG;
    tft.setTextSize(2);
    tft.setTextColor(sel ? COL_FG : COL_GRAY, bg);
    tft.setCursor(18, y + 6);
    tft.print(label);
    if (value && value[0]) {
        int vx = 304 - (int)strlen(value) * 12;
        tft.setTextColor(sel ? COL_AMBER : COL_GRAY, bg);
        tft.setCursor(vx, y + 6);
        tft.print(value);
    }
}
static void drawRowKorean(int visIdx, const String& label, const char* value, bool sel) {
    int y = LIST_Y0 + visIdx * ROW_H;
    rowPill(y, sel);
    uint16_t bg = sel ? COL_DGRAY : COL_BG;
    drawKText(18, y + 6, label, sel ? COL_FG : COL_GRAY, bg);
    if (value && value[0]) {
        int vx = 304 - (int)strlen(value) * 6;
        tft.setTextSize(1);
        tft.setTextColor(sel ? COL_AMBER : COL_GRAY, bg);
        tft.setCursor(vx, y + 10);
        tft.print(value);
    }
}
// 우측 미니 스크롤바 (목록이 화면을 넘을 때만)
static void drawScrollbar(int n) {
    int trackH = ROW_VIS * ROW_H - 4;
    tft.fillRect(315, LIST_Y0, 4, trackH, COL_BG);
    if (n <= ROW_VIS) return;
    tft.fillRect(317, LIST_Y0, 1, trackH, COL_DGRAY);
    int th = trackH * ROW_VIS / n;
    if (th < 10) th = 10;
    int ty = LIST_Y0 + (trackH - th) * g_ui.listTop / (n - ROW_VIS);
    tft.fillRect(316, ty, 3, th, COL_GRAY);
}

// 리스트 커서 이동 + 스크롤 추적
static void listNav(int n, int8_t delta) {
    if (n <= 0) return;
    int c = wrapIndex((int)g_ui.cursor + delta, n);
    g_ui.cursor = (int8_t)c;
    int maxTop = n > ROW_VIS ? n - ROW_VIS : 0;
    if (c < g_ui.listTop)              g_ui.listTop = (int8_t)c;
    if (c >= g_ui.listTop + ROW_VIS)   g_ui.listTop = (int8_t)(c - ROW_VIS + 1);
    if (g_ui.listTop > maxTop)         g_ui.listTop = (int8_t)maxTop;
    if (g_ui.listTop < 0)              g_ui.listTop = 0;
    g_ui.dirty = true;
}

static void fmtDur(long sec, char* out, size_t n) {
    if (sec >= 3600) snprintf(out, n, "%ld:%02ld:%02ld", sec / 3600, (sec / 60) % 60, sec % 60);
    else             snprintf(out, n, "%ld:%02ld", sec / 60, sec % 60);
}

// ──────────────────────────────────────────────────────────────
// 레시피 파일 로더 (/recipes.json — 웹 편집기와 동일 포맷)
// ──────────────────────────────────────────────────────────────
static void loadCatCounts() {
    for (int i = 0; i < 4; ++i) g_catCount[i] = 0;
    File f = LittleFS.open("/recipes.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        for (int i = 0; i < 4; ++i)
            g_catCount[i] = (int)doc[REC_CATS[i]].as<JsonArray>().size();
    }
    f.close();
}

static void loadRecipeList(int catIdx) {
    g_recList.clear();
    File f = LittleFS.open("/recipes.json", "r");
    if (!f) return;
    JsonDocument doc;
    if (deserializeJson(doc, f) == DeserializationError::Ok) {
        for (JsonObject r : doc[REC_CATS[catIdx]].as<JsonArray>()) {
            RecipeMeta m;
            m.name  = r["name"] | String("(no name)");
            m.steps = 0; m.totalSec = 0;
            for (JsonObject s : r["steps"].as<JsonArray>()) {
                m.steps++;
                int d = s["durSec"] | 0;
                if (d > 0) m.totalSec += d;
            }
            g_recList.push_back(m);
        }
    }
    f.close();
}

static bool loadRecipeSteps(int catIdx, int recIdx, String& name, std::vector<StepInfo>& steps) {
    steps.clear();
    File f = LittleFS.open("/recipes.json", "r");
    if (!f) return false;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return false;
    JsonObject r = doc[REC_CATS[catIdx]].as<JsonArray>()[recIdx];
    if (r.isNull()) return false;
    name = r["name"] | String("Recipe");
    for (JsonObject s : r["steps"].as<JsonArray>()) {
        StepInfo si;
        si.name      = s["name"]     | String("Step");
        si.speedRpm  = constrain((int)(s["speedRpm"] | 50), 1, (int)Cfg::MAX_OUTPUT_RPM);
        si.durSec    = max((int)(s["durSec"]    | 60), 1);
        si.rotIntSec = max((int)(s["rotIntSec"] | 30), 5);
        steps.push_back(si);
    }
    return !steps.empty();
}

// ──────────────────────────────────────────────────────────────
// STATUS(홈) — 캐시 기반 부분 렌더
// ──────────────────────────────────────────────────────────────
static struct StatusCache {
    bool        valid       = false;
    int         rpm         = -1;
    int         tgt         = -1;
    MotorState  motorState  = MotorState::IDLE;
    bool        msValid     = false;
    int         tempTenths  = INT32_MIN;
    bool        tempFault   = false;
    bool        running     = false;
    bool        paused      = false;
    bool        waitConf    = false;
    bool        manual      = false;
    bool        noCycleVal  = false;
    int         rotIntSec   = -1;
    int         stepIdx     = -1;
    int         stepTotal   = -1;
    int         stepDur     = -1;
    long        stepRem     = -1;
    String      recName;
    String      curName;
    String      nxtName;
    String      hint;
    const char* modeTxt     = nullptr;
} g_sCache;

static inline void invalidateStatusCache() {
    g_sCache.valid = false; g_sCache.modeTxt = nullptr; g_sCache.hint = "";
}

static void renderStatusChrome() {
    uiHeader("FILM PROCESSOR");
    // 고정 라벨 / 구분선
    tft.drawFastHLine(0, 110, 320, COL_DGRAY);
    tft.drawFastHLine(0, 174, 320, COL_DGRAY);
    tft.setTextSize(1);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setCursor(124, 80);  tft.print("RPM");
    tft.setCursor(10, 180);  tft.print("TEMP");
    tft.setCursor(170, 180); tft.print("STEP REMAINING");
    invalidateStatusCache();
}

static const char* statusHint(bool running, bool paused, bool waitConf) {
    if (waitConf) return "PUSH Controls    OK Next step";
    if (paused)   return "PUSH Controls    OK Resume";
    if (running)  return "PUSH Controls    OK Pause";
    if (s_motion->manualMode() || s_motion->state() != MotorState::IDLE)
                  return "PUSH Menu    OK Stop";
    return              "PUSH Menu    OK Quick start";
}

static void renderStatusLive() {
    char buf[48];

    int        rpm   = (int)round(s_motion->curRpm());
    int        tgt   = s_motion->targetRpm();
    MotorState state = s_motion->state();

    const char* dir = "IDLE";
    uint16_t    dc  = COL_DGRAY;
    switch (state) {
        case MotorState::RUN_FWD: case MotorState::STOP_FWD: dir = "FWD";  dc = COL_GREEN; break;
        case MotorState::RUN_REV: case MotorState::STOP_REV: dir = "REV";  dc = COL_BLUE;  break;
        case MotorState::REST:                                dir = "REST"; dc = COL_AMBER; break;
        case MotorState::STOP_RECIPE:                         dir = "STOP"; dc = COL_GRAY;  break;
        case MotorState::STOP_SAFE:                           dir = "STOP"; dc = COL_GRAY;  break;
        default: break;
    }

    bool manual = s_motion->manualMode();
    bool noCyc  = !s_motion->cycle();
    int  rotInt = s_motion->rotIntSec();

    RecipeStatus rs = s_recipe->snapshot();
    bool   running   = rs.running;
    bool   paused    = rs.paused;
    bool   waitConf  = rs.waitConfirm;
    int    stepIdx   = rs.stepIdx;
    int    stepTotal = rs.stepTotal;
    int    stepDur   = rs.stepDurSec;
    long   stepRem   = rs.stepRemSec;
    String recName   = rs.name;
    String curName   = rs.curName;
    String nxtName   = rs.nextName;

    float t = s_temp->temperature();
    const bool tf = (s_temp->fault() != 0);
    int tempTenths = tf ? -10000 : (t > -100.0f ? (int)round(t * 10.0f) : -9999);

    const char* modeTxt = "IDLE";
    uint16_t    modeBg  = COL_GRAY;
    if (waitConf)      { modeTxt = "CONFIRM"; modeBg = COL_AMBER; }
    else if (paused)   { modeTxt = "PAUSE";   modeBg = COL_AMBER; }
    else if (running)  { modeTxt = "RECIPE";  modeBg = COL_GREEN; }
    else if (manual)   { modeTxt = "MANUAL";  modeBg = COL_BLUE;  }

    String hint = statusHint(running, paused, waitConf);

    const bool force  = !g_sCache.valid;
    const bool dRpm   = force || g_sCache.rpm != rpm;
    const bool dTgt   = force || g_sCache.tgt != tgt;
    const bool dState = force || !g_sCache.msValid || g_sCache.motorState != state;
    const bool dMode  = force || g_sCache.modeTxt != modeTxt;
    const bool dHint  = force || g_sCache.hint != hint;
    const bool dTemp  = force || g_sCache.tempTenths != tempTenths || g_sCache.tempFault != tf;
    const bool dRecArea = force
        || g_sCache.running    != running  || g_sCache.paused    != paused
        || g_sCache.waitConf   != waitConf || g_sCache.manual    != manual
        || g_sCache.noCycleVal != noCyc    || g_sCache.rotIntSec != rotInt
        || g_sCache.stepIdx    != stepIdx  || g_sCache.stepTotal != stepTotal
        || g_sCache.recName    != recName  || g_sCache.curName   != curName
        || g_sCache.nxtName    != nxtName;
    const bool dRem = force
        || g_sCache.running != running || g_sCache.stepDur != stepDur || g_sCache.stepRem != stepRem;

    // 1) 헤더 우측 모드 칩
    if (dMode) {
        clearArea(226, 2, 90, 22);
        tft.fillRoundRect(228, 3, 86, 20, 10, modeBg);
        tft.setTextColor(COL_BG);
        tft.setTextSize(1);
        int mlen = (int)strlen(modeTxt) * 6;
        tft.setCursor(228 + (86 - mlen) / 2, 10);
        tft.print(modeTxt);
    }
    // 2) 푸터 힌트
    if (dHint) uiFooter(hint.c_str());
    // 3) 대형 RPM
    if (dRpm) {
        snprintf(buf, sizeof(buf), "%3d", rpm);
        tft.setTextColor(COL_FG, COL_BG);
        tft.setTextSize(6);
        tft.setCursor(10, 40);
        tft.print(buf);
    }
    // 4) 목표 RPM (RPM 라벨 위)
    if (dTgt) {
        clearArea(124, 44, 90, 10);
        if (tgt > 0) {
            tft.setTextSize(1);
            tft.setTextColor(COL_GRAY, COL_BG);
            snprintf(buf, sizeof(buf), "TARGET %d", tgt);
            tft.setCursor(124, 46);
            tft.print(buf);
        }
    }
    // 5) 방향 칩 (IDLE은 아웃라인만 — 미니멀)
    if (dState) {
        clearArea(216, 42, 100, 40);
        if (state == MotorState::IDLE) {
            tft.drawRoundRect(220, 44, 92, 34, 8, COL_DGRAY);
            tft.setTextColor(COL_GRAY, COL_BG);
        } else {
            tft.fillRoundRect(220, 44, 92, 34, 8, dc);
            tft.setTextColor(COL_BG);
        }
        tft.setTextSize(2);
        int dlen = (int)strlen(dir) * 12;
        tft.setCursor(220 + (92 - dlen) / 2, 54);
        tft.print(dir);
    }
    // 6) RPM 바 (얇은 트랙)
    if (dRpm) {
        int maxR = (int)Cfg::MAX_OUTPUT_RPM;
        int sat  = rpm < 0 ? 0 : (rpm > maxR ? maxR : rpm);
        int barW = sat * 300 / maxR;
        if (barW > 0)   tft.fillRect(10, 98, barW, 4, COL_AMBER);
        if (barW < 300) tft.fillRect(10 + barW, 98, 300 - barW, 4, COL_DGRAY);
    }
    // 7) 온도
    if (dTemp) {
        clearArea(10, 190, 150, 18);
        tft.setTextSize(2);
        tft.setCursor(10, 192);
        if (tf) {
            tft.setTextColor(COL_RED, COL_BG);
            tft.print("FAULT");
        } else if (t > -100.0f) {
            tft.setTextColor(COL_FG, COL_BG);
            snprintf(buf, sizeof(buf), "%.1f C", t);
            tft.print(buf);
        } else {
            tft.setTextColor(COL_GRAY, COL_BG);
            tft.print("--.-");
        }
    }
    // 8) 레시피 영역 (한글 3줄)
    if (dRecArea) {
        clearArea(10, 114, 304, 58);
        if (running) {
            String l1;
            if (recName.length())
                l1 = recName + "  [" + String(stepIdx + 1) + "/" + String(stepTotal) + "]";
            else
                l1 = "[" + String(stepIdx + 1) + "/" + String(stepTotal) + "]";
            drawKText(10, 114, l1, COL_AMBER, COL_BG);
            if (curName.length()) drawKText(10, 132, curName, COL_FG, COL_BG);
            if (waitConf)
                drawKText(10, 150, "Step done - OK for next", COL_AMBER, COL_BG);
            else if (nxtName.length())
                drawKText(10, 150, String("Next: ") + nxtName, COL_GRAY, COL_BG);
            else if (stepIdx + 1 >= stepTotal)
                drawKText(10, 150, "Next: (last step)", COL_GRAY, COL_BG);
        } else if (manual) {
            drawKText(10, 114, "Manual run", COL_BLUE, COL_BG);
            snprintf(buf, sizeof(buf), "Cycle %s   Period %ds", noCyc ? "OFF" : "ON", rotInt);
            drawKText(10, 132, buf, COL_GRAY, COL_BG);
        } else {
            drawKText(10, 114, "Ready", COL_GRAY, COL_BG);
            drawKText(10, 132, "Run recipes from menu or web UI", COL_DGRAY, COL_BG);
        }
    }
    // 9) 남은 시간 + 단계 진행바
    if (dRem) {
        clearArea(170, 190, 144, 18);
        if (running && stepDur > 0) {
            snprintf(buf, sizeof(buf), "%02ld:%02ld", stepRem / 60, stepRem % 60);
            tft.setTextColor(COL_FG, COL_BG);
            tft.setTextSize(2);
            tft.setCursor(170, 192);
            tft.print(buf);
            int prog = (int)(((int64_t)(stepDur - stepRem) * 140) / stepDur);
            if (prog < 0) prog = 0;
            if (prog > 140) prog = 140;
            if (prog > 0)   tft.fillRect(170, 211, prog, 3, COL_BLUE);
            if (prog < 140) tft.fillRect(170 + prog, 211, 140 - prog, 3, COL_DGRAY);
        } else {
            tft.setTextColor(COL_GRAY, COL_BG);
            tft.setTextSize(2);
            tft.setCursor(170, 192);
            tft.print("--:--");
            tft.fillRect(170, 211, 140, 3, COL_BG);
        }
    }

    // 캐시 갱신
    g_sCache.valid = true; g_sCache.rpm = rpm; g_sCache.tgt = tgt;
    g_sCache.motorState = state; g_sCache.msValid = true;
    g_sCache.tempTenths = tempTenths; g_sCache.tempFault = tf;
    g_sCache.running = running; g_sCache.paused = paused; g_sCache.waitConf = waitConf;
    g_sCache.manual = manual; g_sCache.noCycleVal = noCyc; g_sCache.rotIntSec = rotInt;
    g_sCache.stepIdx = stepIdx; g_sCache.stepTotal = stepTotal;
    g_sCache.stepDur = stepDur; g_sCache.stepRem = stepRem;
    g_sCache.recName = recName; g_sCache.curName = curName; g_sCache.nxtName = nxtName;
    g_sCache.modeTxt = modeTxt; g_sCache.hint = hint;
}

// ──────────────────────────────────────────────────────────────
// 메인 메뉴
// ──────────────────────────────────────────────────────────────
enum { MM_RECIPE = 0, MM_MANUAL, MM_SETTINGS, MM_BACK, MM_COUNT };

static void renderMenuFull() {
    uiHeader("MENU");
    uiFooter("TURN Move    PUSH Select    OK Back");
}
static void renderMenuItems() {
    static const char* L[MM_COUNT] = { "Run recipe", "Manual control", "Settings", "Back" };
    for (int i = 0; i < MM_COUNT; ++i)
        drawRowAscii(i, L[i], nullptr, i == g_ui.cursor);
}
static void onMenuPush() {
    switch (g_ui.cursor) {
        case MM_RECIPE:   gotoPage(PAGE_REC_CAT);  break;
        case MM_MANUAL:   gotoPage(PAGE_MANUAL);   break;
        case MM_SETTINGS: gotoPage(PAGE_SETTINGS); break;
        case MM_BACK:     changePage(PAGE_STATUS); break;
    }
}

// ──────────────────────────────────────────────────────────────
// 레시피 실행 제어 (레시피 활성 중 PUSH)
// ──────────────────────────────────────────────────────────────
enum { RC_PAUSE = 0, RC_SKIP, RC_STOP, RC_BACK, RC_COUNT };

static void renderRunCtlFull() {
    uiHeader("RUN CONTROL");
    uiFooter("TURN Move    PUSH Select    OK Back");
}
static void renderRunCtlItems() {
    const char* L[RC_COUNT] = {
        s_recipe->paused() ? "Resume" : "Pause",
        "Skip step", "Stop recipe", "Back"
    };
    for (int i = 0; i < RC_COUNT; ++i)
        drawRowAscii(i, L[i], nullptr, i == g_ui.cursor);
}
static void onRunCtlPush() {
    Cmd c{};
    switch (g_ui.cursor) {
        case RC_PAUSE: c.type = CmdType::PAUSE_TOGGLE; s_cmd->enqueue(c);
                       Serial.println("[UI] Pause toggle enqueued");      break;
        case RC_SKIP:  c.type = CmdType::SKIP_STEP;    s_cmd->enqueue(c);
                       Serial.println("[UI] Skip step enqueued");         break;
        case RC_STOP:  c.type = CmdType::SAFE_STOP;    s_cmd->enqueue(c);
                       Serial.println("[UI] Recipe stop enqueued");       break;
        default: break;
    }
    changePage(PAGE_STATUS);
}

// ──────────────────────────────────────────────────────────────
// 레시피 탐색 — 카테고리 → 목록 → 시작 확인
// ──────────────────────────────────────────────────────────────
static const int REC_CAT_N = 5;   // 4 카테고리 + Back

static void renderRecCatFull() {
    uiHeader("RECIPES");
    uiFooter("TURN Move    PUSH Select    OK Back");
}
static void renderRecCatItems() {
    char v[12];
    for (int i = 0; i < REC_CAT_N; ++i) {
        if (i < 4) {
            snprintf(v, sizeof(v), "%d", g_catCount[i]);
            drawRowAscii(i, REC_CATS[i], v, i == g_ui.cursor);
        } else {
            drawRowAscii(i, "Back", nullptr, i == g_ui.cursor);
        }
    }
}
static void onRecCatPush() {
    if (g_ui.cursor >= 4) { gotoPage(PAGE_MENU); return; }
    g_recCat = g_ui.cursor;
    loadRecipeList(g_recCat);
    gotoPage(PAGE_REC_LIST);
}

static void renderRecListFull() {
    char title[24];
    snprintf(title, sizeof(title), "RECIPES / %s", REC_CATS[g_recCat]);
    uiHeader(title);
    if (g_recList.empty()) {
        tft.setTextSize(2);
        tft.setTextColor(COL_GRAY, COL_BG);
        tft.setCursor(40, 100); tft.print("No recipes here");
        tft.setTextSize(1);
        tft.setCursor(40, 130); tft.print("Add recipes from the web UI");
        uiFooter("OK Back");
        return;
    }
    uiFooter("TURN Move    PUSH Select    OK Back");
}
static void renderRecListItems() {
    int n = (int)g_recList.size();
    if (n == 0) return;
    char v[16];
    for (int vis = 0; vis < ROW_VIS; ++vis) {
        int i = g_ui.listTop + vis;
        if (i >= n) { clearArea(6, LIST_Y0 + vis * ROW_H, 306, ROW_H - 4); continue; }
        fmtDur(g_recList[i].totalSec, v, sizeof(v));
        drawRowKorean(vis, g_recList[i].name, v, i == g_ui.cursor);
    }
    drawScrollbar(n);
}
static void onRecListPush() {
    int i = g_ui.cursor;
    if (i < 0 || i >= (int)g_recList.size()) return;
    if (!loadRecipeSteps(g_recCat, i, g_cfName, g_cfSteps)) {
        Serial.println("[UI] recipe load failed");
        return;
    }
    changePage(PAGE_REC_CONFIRM);
}

static void renderRecConfirmFull() {
    uiHeader("START RECIPE");
    drawKText(10, 38, g_cfName, COL_AMBER, COL_BG);

    long total = 0;
    for (const StepInfo& s : g_cfSteps) total += s.durSec;
    char d[16], b[48];
    fmtDur(total, d, sizeof(d));
    snprintf(b, sizeof(b), "%d steps   total %s", (int)g_cfSteps.size(), d);
    tft.setTextSize(1);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setCursor(10, 62);
    tft.print(b);
    tft.drawFastHLine(0, 76, 320, COL_DGRAY);

    // 단계 미리보기 (최대 4)
    int shown = (int)g_cfSteps.size() < 4 ? (int)g_cfSteps.size() : 4;
    for (int i = 0; i < shown; ++i) {
        int y = 84 + i * 20;
        drawKText(10, y, String(i + 1) + ". " + g_cfSteps[i].name, COL_FG, COL_BG);
        fmtDur(g_cfSteps[i].durSec, d, sizeof(d));
        snprintf(b, sizeof(b), "%s  %drpm", d, g_cfSteps[i].speedRpm);
        int vx = 310 - (int)strlen(b) * 6;
        tft.setTextSize(1);
        tft.setTextColor(COL_GRAY, COL_BG);
        tft.setCursor(vx, y + 4);
        tft.print(b);
    }
    if ((int)g_cfSteps.size() > 4) {
        char m[24];
        snprintf(m, sizeof(m), "+%d more steps", (int)g_cfSteps.size() - 4);
        tft.setTextSize(1);
        tft.setTextColor(COL_GRAY, COL_BG);
        tft.setCursor(10, 168);
        tft.print(m);
    }
    uiFooter("PUSH Start    OK Back");
}
static void onRecipeStart() {
    if (s_stage->stage(g_cfName, std::move(g_cfSteps))) {
        Serial.printf("[UI] Recipe staged: %s\n", g_cfName.c_str());
        changePage(PAGE_STATUS);
    } else {
        // 드묾(뮤텍스 타임아웃) — steps 보존되므로 재시도 가능
        uiFooter("Failed - PUSH to retry    OK Back");
    }
}

// ──────────────────────────────────────────────────────────────
// 수동 제어
// ──────────────────────────────────────────────────────────────
enum { MN_START = 0, MN_STOP, MN_SPEED, MN_PERIOD, MN_DIR, MN_CYCLE, MN_BACK, MN_COUNT };

static void renderManualFull() {
    uiHeader("MANUAL CONTROL");
    uiFooter("TURN Move    PUSH Select    OK Back");
}
static void renderManualItems() {
    static const char* L[MN_COUNT] = {
        "Start", "Stop motor", "Speed", "Period", "Direction", "Auto cycle", "Back"
    };
    char v[16];
    for (int vis = 0; vis < ROW_VIS; ++vis) {
        int i = g_ui.listTop + vis;
        if (i >= MN_COUNT) { clearArea(6, LIST_Y0 + vis * ROW_H, 306, ROW_H - 4); continue; }
        const char* val = nullptr;
        switch (i) {
            case MN_SPEED:  snprintf(v, sizeof(v), "%d RPM", g_uiSet.rpm);    val = v; break;
            case MN_PERIOD: snprintf(v, sizeof(v), "%d s",   g_uiSet.rotSec); val = v; break;
            case MN_DIR:    val = g_uiSet.fwd   ? "FWD" : "REV"; break;
            case MN_CYCLE:  val = g_uiSet.cycle ? "ON"  : "OFF"; break;
        }
        drawRowAscii(vis, L[i], val, i == g_ui.cursor);
    }
    drawScrollbar(MN_COUNT);
}
static void onManualPush() {
    Cmd c{};
    switch (g_ui.cursor) {
        case MN_START:
            c.type = CmdType::MANUAL;
            c.rpm = g_uiSet.rpm; c.fwd = g_uiSet.fwd;
            c.cycle = g_uiSet.cycle; c.rotSec = g_uiSet.rotSec;
            s_cmd->enqueue(c);
            Serial.printf("[UI] Manual start: %dRPM %s cyc=%d per=%ds\n",
                          c.rpm, c.fwd ? "FWD" : "REV", c.cycle, c.rotSec);
            changePage(PAGE_STATUS);
            break;
        case MN_STOP:
            c.type = CmdType::SAFE_STOP; s_cmd->enqueue(c);
            Serial.println("[UI] Safe stop enqueued");
            changePage(PAGE_STATUS);
            break;
        case MN_SPEED:
            openEdit(EDIT_SPEED, "SPEED", "RPM", g_uiSet.rpm,
                     (int)Cfg::MIN_OUTPUT_RPM, (int)Cfg::MAX_OUTPUT_RPM, 1, PAGE_MANUAL);
            break;
        case MN_PERIOD:
            openEdit(EDIT_PERIOD, "PERIOD", "sec", g_uiSet.rotSec, 5, 600, 5, PAGE_MANUAL);
            break;
        case MN_DIR:   g_uiSet.fwd   = !g_uiSet.fwd;   g_ui.dirty = true; break;
        case MN_CYCLE: g_uiSet.cycle = !g_uiSet.cycle; g_ui.dirty = true; break;
        case MN_BACK:  gotoPage(PAGE_MENU); break;
    }
}

// ──────────────────────────────────────────────────────────────
// 설정
// ──────────────────────────────────────────────────────────────
enum { ST_SAVER = 0, ST_SAVERSEC, ST_INFO, ST_BACK, ST_COUNT };

static void renderSettingsFull() {
    uiHeader("SETTINGS");
    uiFooter("TURN Move    PUSH Select    OK Back");
}
static void renderSettingsItems() {
    static const char* L[ST_COUNT] = { "Screensaver", "Saver delay", "System info", "Back" };
    char v[16];
    for (int i = 0; i < ST_COUNT; ++i) {
        const char* val = nullptr;
        switch (i) {
            case ST_SAVER:    val = g_uiSet.saverOn ? "ON" : "OFF"; break;
            case ST_SAVERSEC: snprintf(v, sizeof(v), "%d s", g_uiSet.saverSec); val = v; break;
        }
        drawRowAscii(i, L[i], val, i == g_ui.cursor);
    }
}
static void onSettingsPush() {
    switch (g_ui.cursor) {
        case ST_SAVER:
            g_uiSet.saverOn = !g_uiSet.saverOn; saveSaverPrefs(); g_ui.dirty = true; break;
        case ST_SAVERSEC:
            openEdit(EDIT_SAVER, "SAVER DELAY", "sec", g_uiSet.saverSec, 10, 3600, 10, PAGE_SETTINGS);
            break;
        case ST_INFO: changePage(PAGE_INFO); break;
        case ST_BACK: gotoPage(PAGE_MENU);   break;
    }
}

// ──────────────────────────────────────────────────────────────
// 숫자 편집
// ──────────────────────────────────────────────────────────────
static void renderEditFull() {
    uiHeader(g_edit.title);
    uiFooter("TURN Adjust    PUSH Save    OK Cancel");
}
static void renderEditValue() {
    char buf[12];
    clearArea(0, 50, 320, 140);
    snprintf(buf, sizeof(buf), "%d", g_edit.val);
    tft.setTextColor(COL_AMBER, COL_BG);
    tft.setTextSize(8);
    int textPx = (int)strlen(buf) * 8 * 6;
    tft.setCursor((320 - textPx) / 2, 70);
    tft.print(buf);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(2);
    int unitPx = (int)strlen(g_edit.unit) * 12;
    tft.setCursor((320 - unitPx) / 2, 150);
    tft.print(g_edit.unit);
}
static void onEditSave() {
    switch (g_edit.target) {
        case EDIT_SPEED:  g_uiSet.rpm      = g_edit.val; break;
        case EDIT_PERIOD: g_uiSet.rotSec   = g_edit.val; break;
        case EDIT_SAVER:  g_uiSet.saverSec = g_edit.val; saveSaverPrefs(); break;
    }
    changePage(g_edit.back);
}

// ──────────────────────────────────────────────────────────────
// 정보 / 화면보호기
// ──────────────────────────────────────────────────────────────
static void renderInfoFull() {
    uiHeader("SYSTEM INFO");
    uiFooter("PUSH / OK  Back");
    char buf[64];
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(1);
    int y = 40;
    auto line = [&](const char* s){ tft.setCursor(10, y); tft.print(s); y += 15; };
    line("Firmware    : v4.1 (C++/OOP)");
    line("MCU         : ESP32-S3");
    line("Motor       : NEMA17 + TMC2209 (1/8 microstep)");
    line("Temp        : MAX31865 + PT100 (RREF 412 ohm)");
    snprintf(buf, sizeof(buf), "AP IP       : %s", WiFi.softAPIP().toString().c_str()); line(buf);
    snprintf(buf, sizeof(buf), "STA         : %s",
             (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected"); line(buf);
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "STA IP      : %s", WiFi.localIP().toString().c_str()); line(buf);
    }
    snprintf(buf, sizeof(buf), "Free heap   : %u B", (unsigned)ESP.getFreeHeap()); line(buf);
}

// RGB565 프레임(W≤160,H≤120)을 2× 확대해 320×240 채움 (픽셀 2×2)
static void blitFrame2x(const uint16_t* fr, int w, int h) {
    uint16_t line[320];
    int ww = (w > 160) ? 160 : w, hh = (h > 120) ? 120 : h;
    for (int sy = 0; sy < hh; ++sy) {
        const uint16_t* srow = &fr[sy * w];
        for (int sx = 0; sx < ww; ++sx) { uint16_t c = srow[sx]; line[sx*2] = c; line[sx*2+1] = c; }
        int dy = sy * 2;
        tft.drawRGBBitmap(0, dy,     line, ww*2, 1);
        tft.drawRGBBitmap(0, dy + 1, line, ww*2, 1);
    }
}
// /saver.anim 을 PSRAM에 로드+검증. 포맷: "ANM1"+W,H,frames,delay(u16 LE)+frames×W×H×2 RGB565 LE
static bool loadAnim() {
    if (!LittleFS.exists("/saver.anim")) return false;
    File f = LittleFS.open("/saver.anim", "r");
    if (!f) return false;
    size_t sz = f.size();
    uint8_t* buf = (sz >= 12) ? (uint8_t*)ps_malloc(sz) : nullptr;
    size_t rd = buf ? f.read(buf, sz) : 0;
    f.close();
    if (!buf || rd != sz || buf[0]!='A'||buf[1]!='N'||buf[2]!='M'||buf[3]!='1') { if (buf) free(buf); return false; }
    int w = buf[4]|(buf[5]<<8), h = buf[6]|(buf[7]<<8);
    int fr = buf[8]|(buf[9]<<8), dl = buf[10]|(buf[11]<<8);
    if (w<1||w>160||h<1||h>120||fr<1 || (size_t)12 + (size_t)fr*w*h*2 != sz) { free(buf); return false; }
    g_animData = buf; g_animW = w; g_animH = h; g_animFrames = fr; g_animDelay = (dl<20?20:dl);
    return true;
}
// 현재 소스(업로드 anim 또는 임베드 Nyan)의 프레임 idx 렌더
static void renderCurrentFrame(int idx) {
    if (g_useAnim && g_animData) {
        size_t fb = (size_t)g_animW * g_animH * 2;
        blitFrame2x((const uint16_t*)(g_animData + 12 + (size_t)idx * fb), g_animW, g_animH);
    } else {
        blitFrame2x(NYAN[idx], NYAN_W, NYAN_H);
    }
}
static void enterScreensaver() {
    tft.fillScreen(COL_BG);
    g_saverStatic = false; g_useAnim = false;
    if (loadAnim()) {                              // ① 업로드된 RGB565 애니메이션
        g_useAnim = true;
        g_nyanFrame = 0; g_nyanLastMs = millis();
        renderCurrentFrame(0);
        Serial.printf("[Saver] ANIM %dx%d %df d=%dms\n", g_animW, g_animH, g_animFrames, g_animDelay);
        return;
    }
    if (LittleFS.exists("/saver.jpg")) {           // ② 사용자 정적 JPEG
        TJpgDec.drawFsJpg(0, 0, "/saver.jpg", LittleFS);
        g_saverStatic = true;
        Serial.println("[Saver] JPEG 정적");
        return;
    }
    g_nyanFrame = 0; g_nyanLastMs = millis();      // ③ 기본: 임베드 Nyan
    renderCurrentFrame(0);
    Serial.println("[Saver] Nyan 임베드");
}
static void exitScreensaver() {
    if (g_animData) { free(g_animData); g_animData = nullptr; }
    g_useAnim = false;
}

// ──────────────────────────────────────────────────────────────
// STATUS(홈) 입력 — OK 퀵액션
// ──────────────────────────────────────────────────────────────
static void onStatusOk() {
    bool running  = s_recipe->running();
    bool paused   = s_recipe->paused();
    bool waitConf = s_recipe->waitConfirm();

    Cmd c{};
    if (running || paused || waitConf) {
        if (waitConf) { c.type = CmdType::CONFIRM; Serial.println("[UI/Home] OK -> confirm next step"); }
        else          { c.type = CmdType::PAUSE_TOGGLE; Serial.printf("[UI/Home] OK -> recipe %s\n", paused ? "resume" : "pause"); }
        s_cmd->enqueue(c);
        return;
    }
    if (s_motion->manualMode() || s_motion->state() != MotorState::IDLE) {
        c.type = CmdType::SAFE_STOP;
        s_cmd->enqueue(c);
        Serial.println("[UI/Home] OK -> safe stop");
        return;
    }
    c.type   = CmdType::MANUAL;
    c.rpm    = g_uiSet.rpm;
    c.fwd    = g_uiSet.fwd;
    c.cycle  = g_uiSet.cycle;
    c.rotSec = g_uiSet.rotSec;
    s_cmd->enqueue(c);
    Serial.printf("[UI/Home] OK -> quick start: %dRPM %s cyc=%d per=%ds\n",
                  c.rpm, c.fwd ? "FWD" : "REV", c.cycle, c.rotSec);
}
static void onStatusPush() {
    if (s_recipe->active()) gotoPage(PAGE_RUNCTL);   // 레시피 중엔 실행 제어
    else                    gotoPage(PAGE_MENU);
}

// ──────────────────────────────────────────────────────────────
// 초기화
// ──────────────────────────────────────────────────────────────
static void initTft() {
    Serial.println("[TFT] step1: BL off");
    digitalWrite(Pin::TFT_BL, LOW);
    digitalWrite(Pin::TFT_RST, HIGH); delay(50);
    digitalWrite(Pin::TFT_RST, LOW);  delay(100);
    digitalWrite(Pin::TFT_RST, HIGH); delay(300);
    Serial.println("[TFT] step2: RST pulse done");
    SPI.begin(Pin::TFT_SCK, -1, Pin::TFT_MOSI);
    Serial.println("[TFT] step3: SPI.begin");
    tft.init(240, 320);
    Serial.println("[TFT] step4: tft.init OK");
    tft.setSPISpeed(26000000);
    tft.setRotation(3);
    tft.invertDisplay(false);   // 이 패널은 INVON이 반전이라 OFF로 정상화 (네거티브 수정)
    tft.fillScreen(COL_BG);
    Serial.println("[TFT] step5: panel cleared (26MHz, rot=3)");
    digitalWrite(Pin::TFT_BL, HIGH);
    Serial.println("[TFT] step6: BL on — ready");
    u8g2.begin(tft);
    u8g2.setFontDirection(0);
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tjpg_output);
}
static void initInputs() {
    pinMode(Pin::ENC_A,    INPUT_PULLUP);
    pinMode(Pin::ENC_B,    INPUT_PULLUP);
    pinMode(Pin::ENC_PUSH, INPUT_PULLUP);
    pinMode(Pin::KEY_OK,   INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(Pin::ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(Pin::ENC_B), encISR, CHANGE);
}

// ──────────────────────────────────────────────────────────────
// 메인 태스크
// ──────────────────────────────────────────────────────────────
static TaskHandle_t s_taskHandle = nullptr;

static void displayTask(void*) {
    esp_task_wdt_add(nullptr);
    initTft();
    initInputs();
    loadSaverPrefs();
    g_gif.begin(LITTLE_ENDIAN_PIXELS);
    touchInput();
    Serial.printf("[Display] init OK — saver %s, timeout %ds\n",
                  g_uiSet.saverOn ? "ON" : "OFF", g_uiSet.saverSec);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(40));
        esp_task_wdt_reset();

        static uint32_t lastStackLogMs = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastStackLogMs >= 10000) {
            UBaseType_t freeSt = uxTaskGetStackHighWaterMark(nullptr);
            Serial.printf("[DisplayTask] free stack: %u bytes%s\n",
                          (unsigned)freeSt, (freeSt < 1024) ? "  ⚠ 위험" : "");
            lastStackLogMs = nowMs;
        }

        btnPoll(btnPush);
        btnPoll(btnOk);
        int8_t delta = popEncoderSteps();
        bool pushed  = btnConsume(btnPush);
        bool oked    = btnConsume(btnOk);
        bool anyInput = (delta != 0) || pushed || oked;
        if (anyInput) touchInput();

        if (g_ui.page == PAGE_SCREENSAVER) {
            if (anyInput) { exitScreensaver(); changePage(PAGE_STATUS); continue; }
        } else if (g_uiSet.saverOn && g_ui.page != PAGE_RUNCTL &&
                   (millis() - g_lastInputMs) >= (uint32_t)g_uiSet.saverSec * 1000UL) {
            changePage(PAGE_SCREENSAVER);
        }

        // ── 입력 라우팅 (TURN/PUSH/OK 일관 규칙) ──
        switch (g_ui.page) {
            case PAGE_STATUS:
                if (pushed) onStatusPush();
                if (oked)   onStatusOk();
                break;
            case PAGE_MENU:
                if (delta)  listNav(MM_COUNT, delta);
                if (pushed) onMenuPush();
                if (oked)   changePage(PAGE_STATUS);
                break;
            case PAGE_RUNCTL:
                if (delta)  listNav(RC_COUNT, delta);
                if (pushed) onRunCtlPush();
                if (oked)   changePage(PAGE_STATUS);
                break;
            case PAGE_REC_CAT:
                if (delta)  listNav(REC_CAT_N, delta);
                if (pushed) onRecCatPush();
                if (oked)   gotoPage(PAGE_MENU);
                break;
            case PAGE_REC_LIST:
                if (delta)  listNav((int)g_recList.size(), delta);
                if (pushed) onRecListPush();
                if (oked) {  // 카테고리로 복귀 (해당 카테고리에 커서 유지)
                    g_ui.cursor = (int8_t)g_recCat; g_ui.listTop = 0;
                    changePage(PAGE_REC_CAT);
                }
                break;
            case PAGE_REC_CONFIRM:
                if (pushed) onRecipeStart();
                if (oked)   changePage(PAGE_REC_LIST);   // 목록 커서/스크롤 보존
                break;
            case PAGE_MANUAL:
                if (delta)  listNav(MN_COUNT, delta);
                if (pushed) onManualPush();
                if (oked)   gotoPage(PAGE_MENU);
                break;
            case PAGE_SETTINGS:
                if (delta)  listNav(ST_COUNT, delta);
                if (pushed) onSettingsPush();
                if (oked)   gotoPage(PAGE_MENU);
                break;
            case PAGE_EDIT:
                if (delta) {
                    g_edit.val = constrain(g_edit.val + (int)delta * g_edit.step, g_edit.lo, g_edit.hi);
                    g_ui.dirty = true;
                }
                if (pushed) onEditSave();
                if (oked)   changePage(g_edit.back);
                break;
            case PAGE_INFO:
                if (pushed || oked) changePage(PAGE_SETTINGS);
                break;
            case PAGE_SCREENSAVER: break;
        }

        // ── 페이지 전환 시 전체 렌더 ──
        if (g_ui.pageChanged) {
            switch (g_ui.page) {
                case PAGE_STATUS:      renderStatusChrome();               break;
                case PAGE_MENU:        renderMenuFull();                   break;
                case PAGE_RUNCTL:      renderRunCtlFull();                 break;
                case PAGE_REC_CAT:     loadCatCounts(); renderRecCatFull();break;
                case PAGE_REC_LIST:    renderRecListFull();                break;
                case PAGE_REC_CONFIRM: renderRecConfirmFull();             break;
                case PAGE_MANUAL:      renderManualFull();                 break;
                case PAGE_SETTINGS:    renderSettingsFull();               break;
                case PAGE_EDIT:        renderEditFull();                   break;
                case PAGE_INFO:        renderInfoFull();                   break;
                case PAGE_SCREENSAVER: enterScreensaver();                 break;
            }
            g_ui.pageChanged = false;
            g_ui.dirty       = true;
        }

        // ── 변경분 렌더 ──
        if (g_ui.dirty) {
            switch (g_ui.page) {
                case PAGE_STATUS:   renderStatusLive();    break;
                case PAGE_MENU:     renderMenuItems();     break;
                case PAGE_RUNCTL:   renderRunCtlItems();   break;
                case PAGE_REC_CAT:  renderRecCatItems();   break;
                case PAGE_REC_LIST: renderRecListItems();  break;
                case PAGE_MANUAL:   renderManualItems();   break;
                case PAGE_SETTINGS: renderSettingsItems(); break;
                case PAGE_EDIT:     renderEditValue();     break;
                default: break;
            }
            g_ui.dirty = false;
        }

        // STATUS 200ms 주기 라이브 갱신
        uint32_t now = millis();
        if (g_ui.page == PAGE_STATUS && (now - g_ui.lastLiveRefreshMs) >= 200) {
            renderStatusLive();
            g_ui.lastLiveRefreshMs = now;
        }

        // 화면보호기 애니메이션 진행 (정적 JPEG면 정지)
        if (g_ui.page == PAGE_SCREENSAVER && !g_saverStatic) {
            uint32_t t  = millis();
            int      fc = g_useAnim ? g_animFrames : NYAN_FRAMES;
            uint32_t dl = g_useAnim ? (uint32_t)g_animDelay : NYAN_DELAY_MS;
            if (t - g_nyanLastMs >= dl) {
                g_nyanFrame = (g_nyanFrame + 1) % fc;
                renderCurrentFrame(g_nyanFrame);
                g_nyanLastMs = t;
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────
// 공개 API
// ──────────────────────────────────────────────────────────────
void DisplayUI::begin(const Deps& deps) {
    s_motion = deps.motion;
    s_recipe = deps.recipe;
    s_temp   = deps.temp;
    s_cmd    = deps.cmd;
    s_stage  = deps.stage;
    loadSaverPrefs();
}

void DisplayUI::start() {
    // 스택 12288 — u8g2 한글 글리프 + ArduinoJson 레시피 파싱 마진
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 12288, nullptr, 2, &s_taskHandle, 0);
}

uint32_t DisplayUI::freeStack() const {
    if (!s_taskHandle) return 0;
    return uxTaskGetStackHighWaterMark(s_taskHandle);
}

// ── ISaver ──
bool DisplayUI::saverEnabled() const    { return g_uiSet.saverOn; }
int  DisplayUI::saverTimeoutSec() const { return g_uiSet.saverSec; }
void DisplayUI::setSaverEnabled(bool en){ g_uiSet.saverOn = en; saveSaverPrefs(); }
void DisplayUI::setSaverTimeoutSec(int s) {
    if (s < 10)   s = 10;
    if (s > 3600) s = 3600;
    g_uiSet.saverSec = s;
    saveSaverPrefs();
}
