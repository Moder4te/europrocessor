// ================================================================
//  DisplayUI.cpp — display_ui.ino 이관본
//  렌더/입력 내부는 파일스코프 static 유지 (C 콜백이 tft 접근).
//  데이터는 s_motion/s_recipe/s_temp 에서 읽고, 명령은 s_cmd 로 enqueue.
// ================================================================
#include "DisplayUI.h"
#include "Config.h"

#include <WiFi.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TJpg_Decoder.h>
#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

// ──────────────────────────────────────────────────────────────
// 주입된 의존성 (begin에서 설정)
// ──────────────────────────────────────────────────────────────
static MotionController*  s_motion = nullptr;
static RecipeRunner*      s_recipe = nullptr;
static TemperatureSensor* s_temp   = nullptr;
static CommandQueue*      s_cmd    = nullptr;

// 색상 (RGB565)
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
    PAGE_STATUS, PAGE_RECIPE_WARN, PAGE_MENU,
    PAGE_EDIT_SPEED, PAGE_EDIT_ROTSEC, PAGE_EDIT_SAVER_SEC,
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
    int      editValue         = 0;
    int      editMin           = 0;
    int      editMax           = 0;
    bool     dirty             = true;
    bool     pageChanged       = true;
    uint32_t lastLiveRefreshMs = 0;
} g_ui;

enum MenuIdx {
    MIDX_START = 0, MIDX_STOP, MIDX_SPEED, MIDX_PERIOD,
    MIDX_DIR, MIDX_CYCLE, MIDX_SAVER_ON, MIDX_SAVER_TIME,
    MIDX_INFO, MIDX_BACK
};
static const uint8_t MENU_ITEM_COUNT = 10;
static const int     MENU_ITEM_Y0    = 32;
static const int     MENU_ITEM_H     = 20;

// ──────────────────────────────────────────────────────────────
// 화면보호기 디코더
// ──────────────────────────────────────────────────────────────
static AnimatedGIF g_gif;
static File        g_gifFile;
static bool        g_gifActive = false;

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
    if (len > left) len = left;
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
    uint16_t lineBuf[320];
    uint8_t* s = d->pPixels;
    uint16_t* pal = (uint16_t*)d->pPalette;
    int w = d->iWidth;
    if (w > 320) w = 320;
    int y = d->iY + d->y;
    if (y < 0 || y >= 240) return;
    if (d->ucHasTransparency) {
        uint8_t tcol = d->ucTransparent;
        for (int x = 0; x < w; ++x) { uint8_t c = s[x]; lineBuf[x] = (c == tcol) ? COL_BG : pal[c]; }
    } else {
        for (int x = 0; x < w; ++x) lineBuf[x] = pal[s[x]];
    }
    tft.drawRGBBitmap(d->iX, y, lineBuf, w, 1);
}

// ──────────────────────────────────────────────────────────────
// 렌더 헬퍼
// ──────────────────────────────────────────────────────────────
static inline void clearArea(int x, int y, int w, int h) { tft.fillRect(x, y, w, h, COL_BG); }

static void drawHeader(const char* title, uint16_t bg) {
    tft.fillRect(0, 0, 320, 28, bg);
    tft.setTextColor(COL_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 6);
    tft.print(title);
}
static void drawFooter(const char* hint, uint16_t bg) {
    tft.fillRect(0, 216, 320, 24, bg);
    tft.setTextColor(COL_FG);
    tft.setTextSize(1);
    tft.setCursor(8, 222);
    tft.print(hint);
}
static int wrapIndex(int v, int n) { while (v < 0) v += n; return v % n; }

static void changePage(UiPage next) {
    g_ui.page = next; g_ui.dirty = true; g_ui.pageChanged = true;
}
static void enterEditPage(UiPage p, int v, int lo, int hi) {
    g_ui.editValue = v; g_ui.editMin = lo; g_ui.editMax = hi; changePage(p);
}

// ──────────────────────────────────────────────────────────────
// STATUS 페이지 — 캐시 기반 부분 렌더
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
    tft.fillScreen(COL_BG);
    drawHeader("STATUS", COL_GREEN);
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(8,   34); tft.print("RPM");
    tft.setCursor(8,  102); tft.print("Temp");
    tft.setCursor(8,  132); tft.print("Recipe");
    tft.setCursor(8,  178); tft.print("Remaining");
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(160, 56);
    tft.print("RPM");
    invalidateStatusCache();
}

static const char* statusKoHint(bool running, bool paused, bool waitConf) {
    if (waitConf)                       return "PUSH:Menu   KO:Next step";
    if (paused)                         return "PUSH:Menu   KO:Resume";
    if (running)                        return "PUSH:Menu   KO:Pause";
    if (s_motion->manualMode() || s_motion->state() != MotorState::IDLE)
                                        return "PUSH:Menu   KO:Safe stop";
    return                                     "PUSH:Menu   KO:Start manual";
}

static void renderStatusLive() {
    char buf[64];

    int        rpm   = (int)round(s_motion->curRpm());
    int        tgt   = s_motion->targetRpm();
    MotorState state = s_motion->state();

    const char* dir = "IDLE";
    uint16_t    dc  = COL_GRAY;
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

    float   t  = s_temp->temperature();
    uint8_t tf = s_temp->fault();
    int tempTenths = tf ? -10000 : (t > -100.0f ? (int)round(t * 10.0f) : -9999);

    const char* modeTxt = "IDLE";
    uint16_t    modeBg  = COL_GRAY;
    if (waitConf)      { modeTxt = "CONFIRM"; modeBg = COL_AMBER; }
    else if (paused)   { modeTxt = "PAUSE";   modeBg = COL_AMBER; }
    else if (running)  { modeTxt = "RECIPE";  modeBg = COL_GREEN; }
    else if (manual)   { modeTxt = "MANUAL";  modeBg = COL_BLUE;  }

    String hint = statusKoHint(running, paused, waitConf);

    const bool force = !g_sCache.valid;
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

    // 1) 헤더 모드 태그
    if (dMode) {
        tft.fillRect(222, 4, 92, 22, modeBg);
        tft.setTextColor(COL_BG);
        tft.setTextSize(1);
        int mlen = (int)strlen(modeTxt) * 6;
        tft.setCursor(222 + (92 - mlen)/2, 11);
        tft.print(modeTxt);
    }
    // 2) 푸터
    if (dHint) drawFooter(hint.c_str(), COL_DGRAY);
    // 3) 대형 RPM + 출력바
    if (dRpm) {
        snprintf(buf, sizeof(buf), "%3d", rpm);
        tft.setTextColor(COL_FG, COL_BG);
        tft.setTextSize(6);
        tft.setCursor(8, 44);
        tft.print(buf);
        int maxR = (int)Cfg::MAX_OUTPUT_RPM;
        int sat  = rpm < 0 ? 0 : (rpm > maxR ? maxR : rpm);
        int barW = sat * 198 / maxR;
        tft.drawRect(8, 96, 200, 6, COL_DGRAY);
        if (barW > 0)   tft.fillRect(9, 97, barW, 4, COL_AMBER);
        if (barW < 198) tft.fillRect(9 + barW, 97, 198 - barW, 4, COL_BG);
    }
    // 4) 목표 RPM
    if (dTgt) {
        clearArea(160, 78, 120, 10);
        if (tgt > 0) {
            tft.setTextSize(1);
            tft.setTextColor(COL_GRAY, COL_BG);
            snprintf(buf, sizeof(buf), "target %d", tgt);
            tft.setCursor(160, 80);
            tft.print(buf);
        }
    }
    // 5) 방향 배지
    if (dState) {
        tft.fillRect(232, 44, 84, 32, dc);
        tft.setTextColor(COL_BG);
        tft.setTextSize(3);
        int dlen = (int)strlen(dir) * 18;
        tft.setCursor(232 + (84 - dlen)/2, 50);
        tft.print(dir);
    }
    // 6) 온도
    if (dTemp) {
        clearArea(40, 110, 200, 18);
        tft.setTextSize(2);
        tft.setCursor(40, 110);
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
    // 7) 레시피 영역 (한글)
    if (dRecArea) {
        clearArea(60, 130, 256, 50);
        if (running) {
            String l1;
            if (recName.length())
                l1 = recName + "  [" + String(stepIdx + 1) + "/" + String(stepTotal) + "]";
            else
                l1 = "[" + String(stepIdx + 1) + "/" + String(stepTotal) + "]";
            drawKText(60, 130, l1, COL_AMBER, COL_BG);
            if (curName.length()) drawKText(60, 148, curName, COL_FG, COL_BG);
            if (nxtName.length())  drawKText(60, 166, String("Next: ") + nxtName, COL_GRAY, COL_BG);
            else if (stepIdx + 1 >= stepTotal) drawKText(60, 166, "Next: (last step)", COL_GRAY, COL_BG);
        } else if (manual) {
            drawKText(60, 130, "Manual operation", COL_BLUE, COL_BG);
            snprintf(buf, sizeof(buf), "Cycle: %s   Period: %ds", noCyc ? "OFF" : "ON", rotInt);
            drawKText(60, 148, buf, COL_GRAY, COL_BG);
        } else {
            drawKText(60, 140, "Idle", COL_GRAY, COL_BG);
        }
    }
    // 8) 남은 시간 + 단계바
    if (dRem) {
        if (running && stepDur > 0) {
            snprintf(buf, sizeof(buf), "%02ld:%02ld", stepRem / 60, stepRem % 60);
            tft.setTextColor(COL_FG, COL_BG);
            tft.setTextSize(3);
            tft.setCursor(64, 184);
            tft.print(buf);
            int prog = stepDur > 0 ? (int)(((int64_t)(stepDur - stepRem) * 128) / stepDur) : 0;
            if (prog < 0) prog = 0;
            if (prog > 128) prog = 128;
            tft.drawRect(180, 192, 130, 8, COL_DGRAY);
            if (prog > 0)   tft.fillRect(181, 193, prog, 6, COL_BLUE);
            if (prog < 128) tft.fillRect(181 + prog, 193, 128 - prog, 6, COL_BG);
        } else {
            clearArea(64, 184, 256, 28);
            tft.setTextColor(COL_GRAY, COL_BG);
            tft.setTextSize(2);
            tft.setCursor(64, 188);
            tft.print("--:--");
        }
    }

    // 캐시 갱신
    g_sCache.valid=true; g_sCache.rpm=rpm; g_sCache.tgt=tgt;
    g_sCache.motorState=state; g_sCache.msValid=true;
    g_sCache.tempTenths=tempTenths; g_sCache.tempFault=tf;
    g_sCache.running=running; g_sCache.paused=paused; g_sCache.waitConf=waitConf;
    g_sCache.manual=manual; g_sCache.noCycleVal=noCyc; g_sCache.rotIntSec=rotInt;
    g_sCache.stepIdx=stepIdx; g_sCache.stepTotal=stepTotal;
    g_sCache.stepDur=stepDur; g_sCache.stepRem=stepRem;
    g_sCache.recName=recName; g_sCache.curName=curName; g_sCache.nxtName=nxtName;
    g_sCache.modeTxt=modeTxt; g_sCache.hint=hint;
}

// ──────────────────────────────────────────────────────────────
// 기타 페이지
// ──────────────────────────────────────────────────────────────
static void renderRecipeWarnFull() {
    tft.fillScreen(COL_BG);
    drawHeader("RECIPE RUNNING", COL_RED);
    drawFooter("KO:Stop & enter   PUSH:Cancel", COL_DGRAY);
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(20, 64);  tft.print("A recipe is running.");
    tft.setTextColor(COL_AMBER, COL_BG);
    tft.setCursor(20, 100); tft.print("Stop it and enter");
    tft.setCursor(20, 124); tft.print("manual mode?");
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(20, 168); tft.print("KO   = confirm (recipe will be stopped)");
    tft.setCursor(20, 184); tft.print("PUSH = cancel  (back to status)");
}

static void renderMenuChrome() {
    tft.fillScreen(COL_BG);
    drawHeader("MANUAL CONTROL", COL_AMBER);
    drawFooter("Turn:Move  PUSH:Enter  KO:Confirm", COL_DGRAY);
}
static void renderMenuItems() {
    char buf[48];
    for (uint8_t i = 0; i < MENU_ITEM_COUNT; ++i) {
        int  y   = MENU_ITEM_Y0 + i * MENU_ITEM_H;
        bool sel = (i == g_ui.cursor);
        uint16_t bg = sel ? COL_AMBER : COL_BG;
        uint16_t fg = sel ? COL_BG    : COL_FG;
        tft.fillRect(0, y, 320, MENU_ITEM_H, bg);
        tft.setTextColor(fg, bg);
        tft.setTextSize(2);
        tft.setCursor(8, y + 2);
        tft.print(sel ? "> " : "  ");
        switch (i) {
            case MIDX_START: tft.print("Start"); break;
            case MIDX_STOP:  tft.print("Stop");  break;
            case MIDX_SPEED:  snprintf(buf, sizeof(buf), "Speed     %3d RPM", g_uiSet.rpm);    tft.print(buf); break;
            case MIDX_PERIOD: snprintf(buf, sizeof(buf), "Period    %3d s",   g_uiSet.rotSec); tft.print(buf); break;
            case MIDX_DIR:    snprintf(buf, sizeof(buf), "Dir       %s", g_uiSet.fwd ? "FWD" : "REV"); tft.print(buf); break;
            case MIDX_CYCLE:  snprintf(buf, sizeof(buf), "Cycle     %s", g_uiSet.cycle ? "ON " : "OFF"); tft.print(buf); break;
            case MIDX_SAVER_ON:   snprintf(buf, sizeof(buf), "Saver     %s", g_uiSet.saverOn ? "ON " : "OFF"); tft.print(buf); break;
            case MIDX_SAVER_TIME: snprintf(buf, sizeof(buf), "SaverTime %3d s", g_uiSet.saverSec); tft.print(buf); break;
            case MIDX_INFO:  tft.print("Info"); break;
            case MIDX_BACK:  tft.print("Back to Status"); break;
        }
    }
}

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

static void enterScreensaver() {
    tft.fillScreen(COL_BG);
    g_gifActive = false;
    if (LittleFS.exists("/saver.gif")) {
        if (g_gif.open("/saver.gif", gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
            g_gifActive = true;
            Serial.printf("[Saver] GIF %dx%d 재생 시작\n", g_gif.getCanvasWidth(), g_gif.getCanvasHeight());
            return;
        }
    }
    if (LittleFS.exists("/saver.jpg")) {
        TJpgDec.drawFsJpg(0, 0, "/saver.jpg", LittleFS);
        Serial.println("[Saver] JPEG 표시");
        return;
    }
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(70, 100);  tft.print("Screensaver");
    tft.setTextSize(1);
    tft.setCursor(70, 130);  tft.print("Upload an image via web UI");
    tft.setCursor(70, 146);  tft.print("/api/saver/image  (jpg or gif)");
}
static void exitScreensaver() {
    if (g_gifActive) { g_gif.close(); g_gifActive = false; }
}

static void renderInfoFull() {
    tft.fillScreen(COL_BG);
    drawHeader("SYSTEM INFO", COL_BLUE);
    drawFooter("KO or PUSH: Back", COL_DGRAY);
    char buf[64];
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(1);
    int y = 38;
    auto line = [&](const char* s){ tft.setCursor(8, y); tft.print(s); y += 14; };
    line("Firmware    : v3.1 (C++/OOP)");
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

// ──────────────────────────────────────────────────────────────
// 입력 처리
// ──────────────────────────────────────────────────────────────
static bool isRecipeRunning() { return s_recipe->running(); }

static void onStatusOk() {
    bool running  = s_recipe->running();
    bool paused   = s_recipe->paused();
    bool waitConf = s_recipe->waitConfirm();

    Cmd c{};
    if (running || paused || waitConf) {
        if (waitConf) { c.type = CmdType::CONFIRM; Serial.println("[UI/Status] KO → confirm next step"); }
        else          { c.type = CmdType::PAUSE_TOGGLE; Serial.printf("[UI/Status] KO → recipe %s\n", paused ? "resume" : "pause"); }
        s_cmd->enqueue(c);
        return;
    }
    if (s_motion->manualMode() || s_motion->state() != MotorState::IDLE) {
        c.type = CmdType::SAFE_STOP;
        s_cmd->enqueue(c);
        Serial.println("[UI/Status] KO → safe stop");
        return;
    }
    c.type   = CmdType::MANUAL;
    c.rpm    = g_uiSet.rpm;
    c.fwd    = g_uiSet.fwd;
    c.cycle  = g_uiSet.cycle;
    c.rotSec = g_uiSet.rotSec;
    s_cmd->enqueue(c);
    Serial.printf("[UI/Status] KO → manual start: %dRPM %s cyc=%d per=%ds\n",
                  c.rpm, c.fwd ? "FWD" : "REV", c.cycle, c.rotSec);
}

static void tryEnterMenu() {
    if (isRecipeRunning()) {
        changePage(PAGE_RECIPE_WARN);
    } else {
        g_ui.cursor = 0;
        changePage(PAGE_MENU);
    }
}
static void onRecipeWarnOk() {
    Cmd c{}; c.type = CmdType::STOP;
    s_cmd->enqueue(c);
    Serial.println("[UI] Recipe stop enqueued (from warn dialog)");
    g_ui.cursor = 0;
    changePage(PAGE_MENU);
}

static void onMenuPush() {
    switch (g_ui.cursor) {
        case MIDX_SPEED:  enterEditPage(PAGE_EDIT_SPEED, g_uiSet.rpm, (int)Cfg::MIN_OUTPUT_RPM, (int)Cfg::MAX_OUTPUT_RPM); break;
        case MIDX_PERIOD: enterEditPage(PAGE_EDIT_ROTSEC, g_uiSet.rotSec, 5, 600); break;
        case MIDX_DIR:    g_uiSet.fwd   = !g_uiSet.fwd;   g_ui.dirty = true; break;
        case MIDX_CYCLE:  g_uiSet.cycle = !g_uiSet.cycle; g_ui.dirty = true; break;
        case MIDX_SAVER_ON: g_uiSet.saverOn = !g_uiSet.saverOn; saveSaverPrefs(); g_ui.dirty = true; break;
        case MIDX_SAVER_TIME: enterEditPage(PAGE_EDIT_SAVER_SEC, g_uiSet.saverSec, 10, 3600); break;
        case MIDX_INFO:  changePage(PAGE_INFO);   break;
        case MIDX_BACK:  changePage(PAGE_STATUS); break;
        default: break;
    }
}
static void onMenuOk() {
    Cmd c{};
    switch (g_ui.cursor) {
        case MIDX_START:
            c.type=CmdType::MANUAL; c.rpm=g_uiSet.rpm; c.fwd=g_uiSet.fwd; c.cycle=g_uiSet.cycle; c.rotSec=g_uiSet.rotSec;
            s_cmd->enqueue(c);
            Serial.printf("[UI] Start enqueued: %dRPM %s cyc=%d per=%ds\n", c.rpm, c.fwd?"FWD":"REV", c.cycle, c.rotSec);
            changePage(PAGE_STATUS);
            break;
        case MIDX_STOP:   c.type=CmdType::STOP; s_cmd->enqueue(c); Serial.println("[UI] Stop enqueued"); break;
        case MIDX_DIR:    g_uiSet.fwd   = !g_uiSet.fwd;   g_ui.dirty = true; break;
        case MIDX_CYCLE:  g_uiSet.cycle = !g_uiSet.cycle; g_ui.dirty = true; break;
        case MIDX_SAVER_ON: g_uiSet.saverOn = !g_uiSet.saverOn; saveSaverPrefs(); g_ui.dirty = true; break;
        case MIDX_BACK:   changePage(PAGE_STATUS); break;
        default: break;
    }
}
static void onEditOk() {
    switch (g_ui.page) {
        case PAGE_EDIT_SPEED:     g_uiSet.rpm      = g_ui.editValue; break;
        case PAGE_EDIT_ROTSEC:    g_uiSet.rotSec   = g_ui.editValue; break;
        case PAGE_EDIT_SAVER_SEC: g_uiSet.saverSec = g_ui.editValue; saveSaverPrefs(); break;
        default: break;
    }
    changePage(PAGE_MENU);
}
static void onEditPush() { changePage(PAGE_MENU); }

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
        } else if (g_uiSet.saverOn && g_ui.page != PAGE_RECIPE_WARN &&
                   (millis() - g_lastInputMs) >= (uint32_t)g_uiSet.saverSec * 1000UL) {
            changePage(PAGE_SCREENSAVER);
        }

        switch (g_ui.page) {
            case PAGE_STATUS:
                if (pushed) tryEnterMenu();
                if (oked)   onStatusOk();
                break;
            case PAGE_RECIPE_WARN:
                if (oked)   onRecipeWarnOk();
                if (pushed) changePage(PAGE_STATUS);
                break;
            case PAGE_MENU:
                if (delta != 0) { g_ui.cursor = (int8_t)wrapIndex(g_ui.cursor + delta, MENU_ITEM_COUNT); g_ui.dirty = true; }
                if (pushed) onMenuPush();
                if (oked)   onMenuOk();
                break;
            case PAGE_EDIT_SPEED:
            case PAGE_EDIT_ROTSEC:
            case PAGE_EDIT_SAVER_SEC:
                if (delta != 0) { g_ui.editValue = constrain(g_ui.editValue + delta, g_ui.editMin, g_ui.editMax); g_ui.dirty = true; }
                if (oked)   onEditOk();
                if (pushed) onEditPush();
                break;
            case PAGE_INFO:
                if (oked || pushed) changePage(PAGE_MENU);
                break;
            case PAGE_SCREENSAVER: break;
        }

        if (g_ui.pageChanged) {
            switch (g_ui.page) {
                case PAGE_STATUS:         renderStatusChrome();           break;
                case PAGE_RECIPE_WARN:    renderRecipeWarnFull();         break;
                case PAGE_MENU:           renderMenuChrome();             break;
                case PAGE_EDIT_SPEED:     renderEditChrome("Speed");      break;
                case PAGE_EDIT_ROTSEC:    renderEditChrome("Period");     break;
                case PAGE_EDIT_SAVER_SEC: renderEditChrome("Saver Time"); break;
                case PAGE_INFO:           renderInfoFull();               break;
                case PAGE_SCREENSAVER:    enterScreensaver();             break;
            }
            g_ui.pageChanged = false;
            g_ui.dirty       = true;
        }

        if (g_ui.dirty) {
            switch (g_ui.page) {
                case PAGE_STATUS:         renderStatusLive();      break;
                case PAGE_MENU:           renderMenuItems();       break;
                case PAGE_EDIT_SPEED:     renderEditValue("RPM");  break;
                case PAGE_EDIT_ROTSEC:    renderEditValue("sec");  break;
                case PAGE_EDIT_SAVER_SEC: renderEditValue("sec");  break;
                default: break;
            }
            g_ui.dirty = false;
        }

        uint32_t now = millis();
        if (g_ui.page == PAGE_STATUS && (now - g_ui.lastLiveRefreshMs) >= 200) {
            renderStatusLive();
            g_ui.lastLiveRefreshMs = now;
        }

        if (g_ui.page == PAGE_SCREENSAVER && g_gifActive) {
            int delayMs = 0;
            int rc = g_gif.playFrame(false, &delayMs);
            if (rc < 0)      { g_gif.close(); g_gifActive = false; }
            else if (rc == 0) g_gif.reset();
            if (delayMs > 0 && delayMs < 200) vTaskDelay(pdMS_TO_TICKS(delayMs));
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
    loadSaverPrefs();
}

void DisplayUI::start() {
    // 스택 8192 — u8g2 한글 글리프 + Adafruit_GFX 안전 마진
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", 8192, nullptr, 2, &s_taskHandle, 0);
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
