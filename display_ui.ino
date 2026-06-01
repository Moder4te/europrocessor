/*
 * ================================================================
 *  display_ui.ino — 디스플레이 / 로터리 인코더 / 버튼 UI 모듈
 *  ----------------------------------------------------------------
 *  - ST7789 320x240 TFT (Hardware SPI / FSPI on ESP32-S3)
 *  - EC11 로터리 인코더 (A/B 인터럽트 + PUSH 폴링)
 *  - KO 확정 버튼 (폴링)
 *
 *  실행 위치: Core 0 별도 FreeRTOS 태스크 (25 Hz)
 *  명령은 g_cmdQueue로 enqueue → Core 1 loop()이 안전하게 dispatch
 *
 *  [UI 인터랙션 규약]
 *    회전:       메뉴 항목 이동 / 편집 값 ±1  (시계방향 = "다음")
 *    PUSH(EC11): 페이지 진입 / 토글 / 취소
 *    KO:         선택 확정 (Start/Stop, 편집 값 저장, 다이얼로그 확인)
 *
 *  [페이지 흐름] — STA 자동 전환 제거, 수동 오버라이드 방식
 *    PAGE_STATUS (기본 부팅 화면, 웹 상태 페이지의 정보 표시)
 *      PUSH ─→  rCtx.running ?
 *                 yes → PAGE_RECIPE_WARN (KO=정지+메뉴, PUSH=취소)
 *                 no  → PAGE_MENU (수동 컨트롤)
 *    PAGE_MENU
 *      "Back" 항목 또는 (KO/PUSH 페이지별) → PAGE_STATUS 로 복귀
 *
 *  [디스플레이 180° 회전]
 *    tft.setRotation(3) — 가로 320x240, 기존 (1) 대비 180° 뒤집힘
 *    물리적으로 인코더 위치도 뒤집히므로 회전 방향 부호도 함께 반전
 * ================================================================
 */

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <TJpg_Decoder.h>          // JPEG 화면보호기
#include <AnimatedGIF.h>           // 애니메이션 GIF 화면보호기
#include <LittleFS.h>
#include <SPI.h>

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
// TFT 객체 — 하드웨어 SPI (S3 기본 FSPI 핀: SCK=12, MOSI=11)
// ──────────────────────────────────────────────────────────────
static Adafruit_ST7789       tft(PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

// U8g2 — Adafruit_GFX 위에서 UTF-8/한글 폰트 출력
//   기존 tft 객체를 그대로 사용 (별도 SPI 핸들 불필요)
//   setFontMode(0) + setBackgroundColor() 로 글리프 단위 배경 페인트
//   → clearArea() 없이도 잔상 없이 깜빡임 없음
static U8G2_FOR_ADAFRUIT_GFX u8g2;

// 한글 폰트의 ascent (글자 윗변 → baseline 픽셀 수). unifont_t_korean1 기준.
// Adafruit_GFX 좌표(좌상단 y_top)를 u8g2 좌표(baseline y)로 변환할 때 사용.
static const int K_FONT_ASCENT = 13;

// UTF-8 한글 포함 문자열 출력
//   (x, y_top): Adafruit_GFX 스타일 좌상단 좌표
//   fg/bg    : 글자색 / 배경색 (배경은 글리프 advance 영역에만 칠해짐)
static void drawKText(int x, int y_top, const String& s,
                      uint16_t fg, uint16_t bg) {
    if (!s.length()) return;
    u8g2.setFont(u8g2_font_unifont_t_korean1);
    u8g2.setFontMode(0);                 // 0 = solid (배경 페인트)
    u8g2.setForegroundColor(fg);
    u8g2.setBackgroundColor(bg);
    u8g2.setCursor(x, y_top + K_FONT_ASCENT);
    u8g2.print(s.c_str());
}

// ──────────────────────────────────────────────────────────────
// 로터리 인코더 — 인터럽트 기반 양 자코드 디코딩
//   16엔트리 룩업 테이블로 잡음(글리치)에도 안정적
//   EC11 디텐트 1칸 = 4 transition (g_encAccum / 4 로 정상화)
//
//   화면 180° 회전 시 사용자가 느끼는 "시계방향"이 물리적으로 반대가 되므로
//   popEncoderSteps()에서 부호를 반전시켜 UI 방향을 보정한다.
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

// 회전 방향: 물리 시계방향이 UI "다음" 항목 (사용자 요청으로 반전 해제)
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
// UI 상태
// ──────────────────────────────────────────────────────────────
enum UiPage : uint8_t {
    PAGE_STATUS,         // 부팅 시 진입 / 상시 상태 표시
    PAGE_RECIPE_WARN,    // 레시피 진행 중 메뉴 진입 시 경고 다이얼로그
    PAGE_MENU,           // 수동 컨트롤 메뉴
    PAGE_EDIT_SPEED,
    PAGE_EDIT_ROTSEC,
    PAGE_EDIT_SAVER_SEC, // 화면보호기 진입 대기 시간 편집
    PAGE_INFO,
    PAGE_SCREENSAVER,    // 일정 시간 무입력 시 자동 진입
};

struct UiSettings {
    int  rpm        = 50;
    int  rotSec     = 30;
    bool fwd        = true;
    bool cycle      = true;
    bool saverOn    = true;        // 화면보호기 활성화
    int  saverSec   = 60;          // 무입력 후 진입까지 (초)
} g_uiSet;

// 무입력 추적 — 어떤 입력이든 들어오면 millis()로 갱신
static uint32_t g_lastInputMs = 0;
static inline void touchInput() { g_lastInputMs = millis(); }

// 화면보호기 영구 저장 (Preferences "ui" 네임스페이스)
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

// 외부(rotary_processor.ino 웹 핸들러)에서 호출하는 게터/세터
// — UiSettings 타입을 노출하지 않기 위한 얇은 래퍼
bool saverGetEnabled()         { return g_uiSet.saverOn; }
int  saverGetTimeoutSec()      { return g_uiSet.saverSec; }
void saverSetEnabled(bool en)  { g_uiSet.saverOn = en; saveSaverPrefs(); }
void saverSetTimeoutSec(int s) {
    if (s < 10)   s = 10;
    if (s > 3600) s = 3600;
    g_uiSet.saverSec = s;
    saveSaverPrefs();
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

// 메뉴 항목
enum MenuIdx {
    MIDX_START = 0, MIDX_STOP, MIDX_SPEED, MIDX_PERIOD,
    MIDX_DIR, MIDX_CYCLE, MIDX_SAVER_ON, MIDX_SAVER_TIME,
    MIDX_INFO, MIDX_BACK
};
static const uint8_t MENU_ITEM_COUNT = 10;
static const int     MENU_ITEM_Y0    = 32;   // 첫 항목 Y
static const int     MENU_ITEM_H     = 20;   // 항목 높이 (10항목 × 20 = 200px, 36~232 영역)

// ──────────────────────────────────────────────────────────────
// 화면보호기 — JPEG / GIF 디코더 콜백
// ──────────────────────────────────────────────────────────────
//   파일 위치: LittleFS  /saver.jpg  또는  /saver.gif
//   업로드: 웹 UI(/api/saver/image)에서 POST
// ──────────────────────────────────────────────────────────────
static AnimatedGIF g_gif;
static File        g_gifFile;
static bool        g_gifActive = false;

// TJpg_Decoder 출력 콜백 — 디코더가 16x16 블록을 단위로 호출
static bool tjpg_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (y >= tft.height()) return false;
    tft.drawRGBBitmap(x, y, bitmap, w, h);
    return true;
}

// AnimatedGIF 파일 I/O 콜백
static void* gifOpen(const char* fname, int32_t* pSize) {
    g_gifFile = LittleFS.open(fname, "r");
    if (!g_gifFile) return nullptr;
    *pSize = g_gifFile.size();
    return (void*)&g_gifFile;
}
static void gifClose(void* /*handle*/) {
    if (g_gifFile) g_gifFile.close();
}
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
// 프레임을 라인 단위로 RGB565 변환해 TFT에 그리는 콜백
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
        for (int x = 0; x < w; ++x) {
            uint8_t c = s[x];
            lineBuf[x] = (c == tcol) ? COL_BG : pal[c];
        }
    } else {
        for (int x = 0; x < w; ++x) lineBuf[x] = pal[s[x]];
    }
    tft.drawRGBBitmap(d->iX, y, lineBuf, w, 1);
}

// ──────────────────────────────────────────────────────────────
// 렌더 헬퍼
// ──────────────────────────────────────────────────────────────
static inline void clearArea(int x, int y, int w, int h) {
    tft.fillRect(x, y, w, h, COL_BG);
}

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

static int wrapIndex(int v, int n) {
    while (v < 0) v += n;
    return v % n;
}

// 페이지 전환 헬퍼
static void changePage(UiPage next) {
    g_ui.page         = next;
    g_ui.dirty        = true;
    g_ui.pageChanged  = true;
}

static void enterEditPage(UiPage p, int v, int lo, int hi) {
    g_ui.editValue = v;
    g_ui.editMin   = lo;
    g_ui.editMax   = hi;
    changePage(p);
}

// ──────────────────────────────────────────────────────────────
// 페이지: STATUS — 웹 status 페이지와 동등한 정보 표시
//   (스톱워치 기능 제외)
//   레이아웃 (320x240):
//     [Header  0-28]    "STATUS"  + 우측 모드 태그
//     [영역 A  32-92]   대형 RPM + 방향 배지
//     [영역 B  96-126]  진행바 + 온도
//     [영역 C 130-170]  레시피 이름 / 현재 단계 / 다음 단계
//     [영역 D 174-210]  남은 시간 (대형) + 단계 진행바
//     [Footer 216-240]  KO 동작 안내 (상태별)
//
//   [Flicker 억제 전략]
//     - 직전 프레임 값을 캐시 → 변화한 필드만 다시 그림
//     - 숫자/고정폭 필드는 setTextColor(fg, bg) 으로 글리프 단위 배경 페인트
//       → clearArea() 없이도 잔상 없음, 깜빡임 자체 발생 X
//     - 가변길이 텍스트(레시피명/단계명)는 변화 시에만 clear+redraw
//     - 페이지 진입(chrome) 시 캐시 invalidate → 첫 프레임은 전체 그림
// ──────────────────────────────────────────────────────────────

// 캐시 — 이전 프레임에 그렸던 값들. valid=false면 전체 강제 재렌더
static struct StatusCache {
    bool        valid       = false;
    int         rpm         = -1;
    int         tgt         = -1;
    uint8_t     motorState  = 0xFF;
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
    g_sCache.valid   = false;
    g_sCache.modeTxt = nullptr;
    g_sCache.hint    = "";
}

// (예전의 한글 차단 헬퍼는 u8g2 한글 폰트 도입으로 제거됨.
//  영역 C는 drawKText()로 UTF-8 한글까지 출력 가능.)

static void renderStatusChrome() {
    tft.fillScreen(COL_BG);
    drawHeader("STATUS", COL_GREEN);
    // 푸터는 renderStatusLive()가 상태에 따라 매번 다시 그림

    // 정적 라벨 (chrome에서 한 번만 그림 — Live에서 건드리지 않음)
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(8,   34); tft.print("RPM");
    tft.setCursor(8,  102); tft.print("Temp");
    tft.setCursor(8,  132); tft.print("Recipe");
    tft.setCursor(8,  178); tft.print("Remaining");

    // RPM 단위 라벨 — 큰 숫자 옆 고정 위치
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(160, 56);
    tft.print("RPM");

    invalidateStatusCache();   // 첫 Live 호출에서 전체 그림
}

// KO 동작 안내 문자열 — 현재 상태에 따라 다르게 표시
static const char* statusKoHint(bool running, bool paused, bool waitConf) {
    if (waitConf)                       return "PUSH:Menu   KO:Next step";
    if (paused)                         return "PUSH:Menu   KO:Resume";
    if (running)                        return "PUSH:Menu   KO:Pause";
    if (g_manualMode || mCtx.state != MS_IDLE)
                                        return "PUSH:Menu   KO:Safe stop";
    return                                     "PUSH:Menu   KO:Start manual";
}

static void renderStatusLive() {
    char buf[64];

    // ────────── 스냅샷 수집 ──────────
    int     rpm   = (int)round(mCtx.curRpm);
    int     tgt   = mCtx.targetRpm;
    uint8_t state = mCtx.state;

    const char* dir = "IDLE";
    uint16_t    dc  = COL_GRAY;
    switch (state) {
        case MS_RUN_FWD: case MS_STOP_FWD: dir = "FWD";  dc = COL_GREEN; break;
        case MS_RUN_REV: case MS_STOP_REV: dir = "REV";  dc = COL_BLUE;  break;
        case MS_REST:                       dir = "REST"; dc = COL_AMBER; break;
        case MS_STOP_RECIPE:                dir = "STOP"; dc = COL_GRAY;  break;
        case MS_STOP_SAFE:                  dir = "STOP"; dc = COL_GRAY;  break;
        default: break;
    }

    bool running = false, paused = false, waitConf = false;
    bool manual  = g_manualMode;
    bool noCyc   = g_noCycle;
    int  rotInt  = mCtx.rotIntSec;
    int  stepIdx = 0, stepTotal = 0, stepDur = 0;
    long stepRem = 0;
    String recName, curName, nxtName;
    if (g_rCtxMux && xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        running   = rCtx.running;
        paused    = rCtx.paused;
        waitConf  = rCtx.waitConfirm;
        stepIdx   = rCtx.stepIdx;
        stepTotal = (int)rCtx.steps.size();
        recName   = rCtx.name;
        if (running && stepIdx < stepTotal) {
            const StepInfo& s = rCtx.steps[stepIdx];
            curName = s.name;
            stepDur = s.durSec;
            if (stepIdx + 1 < stepTotal) nxtName = rCtx.steps[stepIdx + 1].name;
            uint32_t el = paused ? rCtx.pausedMs : (millis() - rCtx.stepStartMs);
            int64_t r = (int64_t)s.durSec * 1000LL - (int64_t)el;
            if (r < 0) r = 0;
            stepRem = (long)(r / 1000LL);
        }
        xSemaphoreGive(g_rCtxMux);
    }
    // u8g2 한글 폰트로 출력하므로 ASCII 필터링 불필요

    float   t  = -999.0f;
    uint8_t tf = 0;
    if (g_tempMux && xSemaphoreTake(g_tempMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        t  = g_temperature;
        tf = g_tempFault;
        xSemaphoreGive(g_tempMux);
    }
    // 온도 비교용 정수화: -10000=fault, -9999=미측정, 그 외 = t*10
    int tempTenths = tf ? -10000 : (t > -100.0f ? (int)round(t * 10.0f) : -9999);

    const char* modeTxt = "IDLE";
    uint16_t    modeBg  = COL_GRAY;
    if (waitConf)      { modeTxt = "CONFIRM"; modeBg = COL_AMBER; }
    else if (paused)   { modeTxt = "PAUSE";   modeBg = COL_AMBER; }
    else if (running)  { modeTxt = "RECIPE";  modeBg = COL_GREEN; }
    else if (manual)   { modeTxt = "MANUAL";  modeBg = COL_BLUE;  }

    String hint = statusKoHint(running, paused, waitConf);

    // ────────── Diff 판정 ──────────
    const bool force = !g_sCache.valid;
    const bool dRpm        = force || g_sCache.rpm        != rpm;
    const bool dTgt        = force || g_sCache.tgt        != tgt;
    const bool dState      = force || g_sCache.motorState != state;
    const bool dMode       = force || g_sCache.modeTxt   != modeTxt;
    const bool dHint       = force || g_sCache.hint      != hint;
    const bool dTemp       = force || g_sCache.tempTenths != tempTenths
                                   || g_sCache.tempFault  != tf;
    const bool dRecArea    = force
        || g_sCache.running    != running
        || g_sCache.paused     != paused
        || g_sCache.waitConf   != waitConf
        || g_sCache.manual     != manual
        || g_sCache.noCycleVal != noCyc
        || g_sCache.rotIntSec  != rotInt
        || g_sCache.stepIdx    != stepIdx
        || g_sCache.stepTotal  != stepTotal
        || g_sCache.recName    != recName
        || g_sCache.curName    != curName
        || g_sCache.nxtName    != nxtName;
    const bool dRem        = force
        || g_sCache.running != running
        || g_sCache.stepDur != stepDur
        || g_sCache.stepRem != stepRem;

    // ────────── 1) 헤더 우측 모드 태그 ──────────
    if (dMode) {
        tft.fillRect(222, 4, 92, 22, modeBg);
        tft.setTextColor(COL_BG);
        tft.setTextSize(1);
        int mlen = (int)strlen(modeTxt) * 6;
        tft.setCursor(222 + (92 - mlen)/2, 11);
        tft.print(modeTxt);
    }

    // ────────── 2) 푸터 (KO 안내) ──────────
    if (dHint) drawFooter(hint.c_str(), COL_DGRAY);

    // ────────── 3) 대형 RPM (고정폭 "%3d" + 배경 페인트) ──────────
    // setTextColor(fg, bg) 모드: 각 글리프가 자신의 배경까지 칠함 → 깜빡임 없음
    if (dRpm) {
        snprintf(buf, sizeof(buf), "%3d", rpm);
        tft.setTextColor(COL_FG, COL_BG);
        tft.setTextSize(6);
        tft.setCursor(8, 44);
        tft.print(buf);

        // 모터 출력 진행바 (0~MAX_OUTPUT_RPM, 폭 198px) — RPM 변경 시 동기 갱신
        int maxR = (int)MAX_OUTPUT_RPM;
        int sat  = rpm < 0 ? 0 : (rpm > maxR ? maxR : rpm);
        int barW = sat * 198 / maxR;
        tft.drawRect(8, 96, 200, 6, COL_DGRAY);
        if (barW > 0) tft.fillRect(9, 97, barW, 4, COL_AMBER);
        if (barW < 198) tft.fillRect(9 + barW, 97, 198 - barW, 4, COL_BG);
    }

    // ────────── 4) 목표 RPM (가변폭이라 변화 시만 clear+draw) ──────────
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

    // ────────── 5) 방향 배지 ──────────
    if (dState) {
        tft.fillRect(232, 44, 84, 32, dc);
        tft.setTextColor(COL_BG);
        tft.setTextSize(3);
        int dlen = (int)strlen(dir) * 18;
        tft.setCursor(232 + (84 - dlen)/2, 50);
        tft.print(dir);
    }

    // ────────── 6) 온도 ──────────
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

    // ────────── 7) 레시피 영역 (한글 UTF-8 지원, 16px 3줄) ──────────
    //   라인1 y_top=130 : 레시피명 + [n/total]    (AMBER)
    //   라인2 y_top=148 : 현재 단계명              (FG, 강조)
    //   라인3 y_top=166 : "Next: <다음 단계명>"   (GRAY)
    if (dRecArea) {
        clearArea(60, 130, 256, 50);   // 16px × 3 = 48px + 여유
        if (running) {
            // 라인1: 레시피명 + [n/total]
            String l1;
            if (recName.length()) {
                l1 = recName + "  [" + String(stepIdx + 1) + "/" +
                     String(stepTotal) + "]";
            } else {
                l1 = "[" + String(stepIdx + 1) + "/" + String(stepTotal) + "]";
            }
            drawKText(60, 130, l1, COL_AMBER, COL_BG);

            // 라인2: 현재 단계명
            if (curName.length()) {
                drawKText(60, 148, curName, COL_FG, COL_BG);
            }

            // 라인3: 다음 단계
            if (nxtName.length()) {
                drawKText(60, 166, String("Next: ") + nxtName, COL_GRAY, COL_BG);
            } else if (stepIdx + 1 >= stepTotal) {
                drawKText(60, 166, "Next: (last step)", COL_GRAY, COL_BG);
            }
        } else if (manual) {
            drawKText(60, 130, "Manual operation", COL_BLUE, COL_BG);
            snprintf(buf, sizeof(buf), "Cycle: %s   Period: %ds",
                     noCyc ? "OFF" : "ON", rotInt);
            drawKText(60, 148, buf, COL_GRAY, COL_BG);
        } else {
            drawKText(60, 140, "Idle", COL_GRAY, COL_BG);
        }
    }

    // ────────── 8) 남은 시간 + 단계 진행바 ──────────
    if (dRem) {
        if (running && stepDur > 0) {
            // "%02ld:%02ld" 5자 고정폭 → 배경 페인트로 잔상 없음
            snprintf(buf, sizeof(buf), "%02ld:%02ld",
                     stepRem / 60, stepRem % 60);
            tft.setTextColor(COL_FG, COL_BG);
            tft.setTextSize(3);
            tft.setCursor(64, 184);
            tft.print(buf);

            // 단계 진행 바 (폭 128px)
            int prog = stepDur > 0
                ? (int)(((int64_t)(stepDur - stepRem) * 128) / stepDur)
                : 0;
            if (prog < 0) prog = 0;
            if (prog > 128) prog = 128;
            tft.drawRect(180, 192, 130, 8, COL_DGRAY);
            if (prog > 0)        tft.fillRect(181, 193, prog, 6, COL_BLUE);
            if (prog < 128)      tft.fillRect(181 + prog, 193, 128 - prog, 6, COL_BG);
        } else {
            clearArea(64, 184, 256, 28);
            tft.setTextColor(COL_GRAY, COL_BG);
            tft.setTextSize(2);
            tft.setCursor(64, 188);
            tft.print("--:--");
        }
    }

    // ────────── 캐시 갱신 ──────────
    g_sCache.valid       = true;
    g_sCache.rpm         = rpm;
    g_sCache.tgt         = tgt;
    g_sCache.motorState  = state;
    g_sCache.tempTenths  = tempTenths;
    g_sCache.tempFault   = tf;
    g_sCache.running     = running;
    g_sCache.paused      = paused;
    g_sCache.waitConf    = waitConf;
    g_sCache.manual      = manual;
    g_sCache.noCycleVal  = noCyc;
    g_sCache.rotIntSec   = rotInt;
    g_sCache.stepIdx     = stepIdx;
    g_sCache.stepTotal   = stepTotal;
    g_sCache.stepDur     = stepDur;
    g_sCache.stepRem     = stepRem;
    g_sCache.recName     = recName;
    g_sCache.curName     = curName;
    g_sCache.nxtName     = nxtName;
    g_sCache.modeTxt     = modeTxt;
    g_sCache.hint        = hint;
}

// ──────────────────────────────────────────────────────────────
// 페이지: 레시피 진행 중 경고 다이얼로그
// ──────────────────────────────────────────────────────────────
static void renderRecipeWarnFull() {
    tft.fillScreen(COL_BG);
    drawHeader("RECIPE RUNNING", COL_RED);
    drawFooter("KO:Stop & enter   PUSH:Cancel", COL_DGRAY);

    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(20, 64);
    tft.print("A recipe is running.");

    tft.setTextColor(COL_AMBER, COL_BG);
    tft.setCursor(20, 100);
    tft.print("Stop it and enter");
    tft.setCursor(20, 124);
    tft.print("manual mode?");

    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(20, 168);
    tft.print("KO   = confirm (recipe will be stopped)");
    tft.setCursor(20, 184);
    tft.print("PUSH = cancel  (back to status)");
}

// ──────────────────────────────────────────────────────────────
// 페이지: 수동 컨트롤 메뉴
// ──────────────────────────────────────────────────────────────
static void renderMenuChrome() {
    tft.fillScreen(COL_BG);
    drawHeader("MANUAL CONTROL", COL_AMBER);
    drawFooter("Turn:Move  PUSH:Enter  KO:Confirm", COL_DGRAY);
}

static void renderMenuItems() {
    char buf[48];
    // 10개 항목, 항목 높이 20px → 메뉴 영역 32..232
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
            case MIDX_SPEED:
                snprintf(buf, sizeof(buf), "Speed     %3d RPM", g_uiSet.rpm);
                tft.print(buf); break;
            case MIDX_PERIOD:
                snprintf(buf, sizeof(buf), "Period    %3d s", g_uiSet.rotSec);
                tft.print(buf); break;
            case MIDX_DIR:
                snprintf(buf, sizeof(buf), "Dir       %s", g_uiSet.fwd ? "FWD" : "REV");
                tft.print(buf); break;
            case MIDX_CYCLE:
                snprintf(buf, sizeof(buf), "Cycle     %s", g_uiSet.cycle ? "ON " : "OFF");
                tft.print(buf); break;
            case MIDX_SAVER_ON:
                snprintf(buf, sizeof(buf), "Saver     %s", g_uiSet.saverOn ? "ON " : "OFF");
                tft.print(buf); break;
            case MIDX_SAVER_TIME:
                snprintf(buf, sizeof(buf), "SaverTime %3d s", g_uiSet.saverSec);
                tft.print(buf); break;
            case MIDX_INFO:  tft.print("Info"); break;
            case MIDX_BACK:  tft.print("Back to Status"); break;
        }
    }
}

// ──────────────────────────────────────────────────────────────
// 페이지: 편집 (속도 / 주기)
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
    int cx     = (320 - textPx) / 2;
    tft.setCursor(cx, 88);
    tft.print(buf);

    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(3);
    int unitPx = (int)strlen(unit) * 3 * 6;
    tft.setCursor((320 - unitPx) / 2, 168);
    tft.print(unit);
}

// ──────────────────────────────────────────────────────────────
// 페이지: 화면보호기
//   파일 우선순위:  /saver.gif  >  /saver.jpg  >  텍스트 폴백
//   GIF면 g_gifActive=true, 프레임은 displayTask 라이브 루프에서 진행
// ──────────────────────────────────────────────────────────────
static void enterScreensaver() {
    tft.fillScreen(COL_BG);
    g_gifActive = false;

    if (LittleFS.exists("/saver.gif")) {
        if (g_gif.open("/saver.gif", gifOpen, gifClose, gifRead, gifSeek, gifDraw)) {
            g_gifActive = true;
            Serial.printf("[Saver] GIF %dx%d 재생 시작\n",
                          g_gif.getCanvasWidth(), g_gif.getCanvasHeight());
            return;
        }
    }
    if (LittleFS.exists("/saver.jpg")) {
        TJpgDec.drawFsJpg(0, 0, "/saver.jpg", LittleFS);
        Serial.println("[Saver] JPEG 표시");
        return;
    }

    // 폴백 — 이미지 미설정
    tft.setTextColor(COL_GRAY, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(70, 100);  tft.print("Screensaver");
    tft.setTextSize(1);
    tft.setCursor(70, 130);  tft.print("Upload an image via web UI");
    tft.setCursor(70, 146);  tft.print("/api/saver/image  (jpg or gif)");
}

static void exitScreensaver() {
    if (g_gifActive) {
        g_gif.close();
        g_gifActive = false;
    }
}

// ──────────────────────────────────────────────────────────────
// 페이지: 정보
// ──────────────────────────────────────────────────────────────
static void renderInfoFull() {
    tft.fillScreen(COL_BG);
    drawHeader("SYSTEM INFO", COL_BLUE);
    drawFooter("KO or PUSH: Back", COL_DGRAY);

    char buf[64];
    tft.setTextColor(COL_FG, COL_BG);
    tft.setTextSize(1);
    int y = 38;
    auto line = [&](const char* s){
        tft.setCursor(8, y); tft.print(s); y += 14;
    };

    line("Firmware    : v2.2");
    line("MCU         : ESP32-S3");
    line("Motor       : NEMA17 + TMC2209 (1/8 microstep)");
    line("Temp        : MAX31865 + PT100 (RREF 412 ohm)");
    snprintf(buf, sizeof(buf), "AP SSID     : %s", g_apSSID.c_str());  line(buf);
    snprintf(buf, sizeof(buf), "AP IP       : %s", WiFi.softAPIP().toString().c_str()); line(buf);
    snprintf(buf, sizeof(buf), "STA         : %s",
             (WiFi.status() == WL_CONNECTED) ? "Connected" : "Disconnected");
    line(buf);
    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "STA IP      : %s", WiFi.localIP().toString().c_str());
        line(buf);
    }
    snprintf(buf, sizeof(buf), "Free heap   : %u B", (unsigned)ESP.getFreeHeap());
    line(buf);
}

// ──────────────────────────────────────────────────────────────
// 입력 처리
// ──────────────────────────────────────────────────────────────

// 레시피 진행 중인지 확인 (g_rCtxMux 보호)
static bool isRecipeRunning() {
    bool r = false;
    if (g_rCtxMux && xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        r = rCtx.running;
        xSemaphoreGive(g_rCtxMux);
    }
    return r;
}

// STATUS 페이지 KO — 현재 상태에 따라 다른 동작
//   Idle              → 마지막 g_uiSet 값으로 수동 시작
//   레시피 진행중     → 일시정지
//   레시피 일시정지중 → 재개 (둘 다 CMD_PAUSE_TOGGLE로 해결)
//   레시피 확인대기   → 다음 단계 (CMD_CONFIRM)
//   수동 운전중       → 안전 정지 (CMD_SAFE_STOP — 가속도 곡선 감속)
static void onStatusOk() {
    // 레시피 컨텍스트 스냅샷
    bool running  = false, paused = false, waitConf = false;
    if (g_rCtxMux && xSemaphoreTake(g_rCtxMux, pdMS_TO_TICKS(5)) == pdTRUE) {
        running  = rCtx.running;
        paused   = rCtx.paused;
        waitConf = rCtx.waitConfirm;
        xSemaphoreGive(g_rCtxMux);
    }

    Cmd c{};
    if (running || paused || waitConf) {
        if (waitConf) {
            c.type = CMD_CONFIRM;
            Serial.println("[UI/Status] KO → confirm next step");
        } else {
            c.type = CMD_PAUSE_TOGGLE;
            Serial.printf("[UI/Status] KO → recipe %s\n",
                          paused ? "resume" : "pause");
        }
        enqueueCmd(c);
        return;
    }

    // 수동 모드 동작 중 → 안전 정지
    if (g_manualMode || mCtx.state != MS_IDLE) {
        c.type = CMD_SAFE_STOP;
        enqueueCmd(c);
        Serial.println("[UI/Status] KO → safe stop");
        return;
    }

    // Idle → 수동 시작 (g_uiSet 마지막 값)
    c.type   = CMD_MANUAL;
    c.rpm    = g_uiSet.rpm;
    c.fwd    = g_uiSet.fwd;
    c.cycle  = g_uiSet.cycle;
    c.rotSec = g_uiSet.rotSec;
    enqueueCmd(c);
    Serial.printf("[UI/Status] KO → manual start: %dRPM %s cyc=%d per=%ds\n",
                  c.rpm, c.fwd ? "FWD" : "REV", c.cycle, c.rotSec);
}

// STATUS → 메뉴 진입 시도 (필요 시 경고 다이얼로그 거침)
static void tryEnterMenu() {
    if (isRecipeRunning()) {
        changePage(PAGE_RECIPE_WARN);
    } else {
        g_ui.cursor = 0;
        changePage(PAGE_MENU);
    }
}

// 경고 다이얼로그 KO = 레시피 정지 후 메뉴 진입
static void onRecipeWarnOk() {
    Cmd c{};
    c.type = CMD_STOP;
    enqueueCmd(c);
    Serial.println("[UI] Recipe stop enqueued (from warn dialog)");
    g_ui.cursor = 0;
    changePage(PAGE_MENU);
}

static void onMenuPush() {
    switch (g_ui.cursor) {
        case MIDX_SPEED:
            enterEditPage(PAGE_EDIT_SPEED, g_uiSet.rpm,
                          (int)MIN_OUTPUT_RPM, (int)MAX_OUTPUT_RPM);
            break;
        case MIDX_PERIOD:
            enterEditPage(PAGE_EDIT_ROTSEC, g_uiSet.rotSec, 5, 600);
            break;
        case MIDX_DIR:
            g_uiSet.fwd = !g_uiSet.fwd;
            g_ui.dirty = true;
            break;
        case MIDX_CYCLE:
            g_uiSet.cycle = !g_uiSet.cycle;
            g_ui.dirty = true;
            break;
        case MIDX_SAVER_ON:
            g_uiSet.saverOn = !g_uiSet.saverOn;
            saveSaverPrefs();
            g_ui.dirty = true;
            break;
        case MIDX_SAVER_TIME:
            // 10초~3600초 범위, 5초 단위 추천이지만 인코더 1tick=1초로 단순화
            enterEditPage(PAGE_EDIT_SAVER_SEC, g_uiSet.saverSec, 10, 3600);
            break;
        case MIDX_INFO:
            changePage(PAGE_INFO);
            break;
        case MIDX_BACK:
            changePage(PAGE_STATUS);
            break;
        default: break;
    }
}

static void onMenuOk() {
    Cmd c{};
    switch (g_ui.cursor) {
        case MIDX_START:
            c.type   = CMD_MANUAL;
            c.rpm    = g_uiSet.rpm;
            c.fwd    = g_uiSet.fwd;
            c.cycle  = g_uiSet.cycle;
            c.rotSec = g_uiSet.rotSec;
            enqueueCmd(c);
            Serial.printf("[UI] Start enqueued: %dRPM %s cyc=%d per=%ds\n",
                          c.rpm, c.fwd ? "FWD" : "REV", c.cycle, c.rotSec);
            changePage(PAGE_STATUS);   // 시작 후 상태창으로 복귀
            break;
        case MIDX_STOP:
            c.type = CMD_STOP;
            enqueueCmd(c);
            Serial.println("[UI] Stop enqueued");
            break;
        case MIDX_DIR:
            g_uiSet.fwd = !g_uiSet.fwd;
            g_ui.dirty = true;
            break;
        case MIDX_CYCLE:
            g_uiSet.cycle = !g_uiSet.cycle;
            g_ui.dirty = true;
            break;
        case MIDX_SAVER_ON:
            g_uiSet.saverOn = !g_uiSet.saverOn;
            saveSaverPrefs();
            g_ui.dirty = true;
            break;
        case MIDX_BACK:
            changePage(PAGE_STATUS);
            break;
        default: break;
    }
}

static void onEditOk() {
    switch (g_ui.page) {
        case PAGE_EDIT_SPEED:     g_uiSet.rpm      = g_ui.editValue; break;
        case PAGE_EDIT_ROTSEC:    g_uiSet.rotSec   = g_ui.editValue; break;
        case PAGE_EDIT_SAVER_SEC: g_uiSet.saverSec = g_ui.editValue;
                                  saveSaverPrefs(); break;
        default: break;
    }
    changePage(PAGE_MENU);
}

static void onEditPush() {
    changePage(PAGE_MENU);
}

// ──────────────────────────────────────────────────────────────
// 초기화
// ──────────────────────────────────────────────────────────────
static void initTft() {
    // setup()에서 이미 panel 핀들을 OUTPUT/idle 상태로 고정해두었음.
    // 여기서는 cold reset 펄스 후 panel init.
    Serial.println("[TFT] step1: BL off");
    digitalWrite(PIN_TFT_BL, LOW);

    // RST 명시 펄스 — panel 컨트롤러 cold reset (panic 후 잠긴 state 해제)
    digitalWrite(PIN_TFT_RST, HIGH); delay(50);
    digitalWrite(PIN_TFT_RST, LOW);  delay(100);
    digitalWrite(PIN_TFT_RST, HIGH); delay(300);
    Serial.println("[TFT] step2: RST pulse done");

    // FSPI 핀 명시 — ESP32-S3 IO-MUX 직결 최고 속도 보장
    SPI.begin(PIN_TFT_SCK, /*MISO 미사용*/ -1, PIN_TFT_MOSI);
    Serial.println("[TFT] step3: SPI.begin");

    tft.init(240, 320);
    Serial.println("[TFT] step4: tft.init OK");

    // SPI 26MHz로 보수적 시작 (안정 확인 후 40MHz 상향 가능)
    tft.setSPISpeed(26000000);
    tft.setRotation(3);               // 디스플레이 보드 장착 방향 180° 회전
    tft.fillScreen(COL_BG);
    Serial.println("[TFT] step5: panel cleared (26MHz, rot=3)");

    // panel을 검정으로 클리어한 후 백라이트 ON — 흰 잔상 방지
    digitalWrite(PIN_TFT_BL, HIGH);
    Serial.println("[TFT] step6: BL on — ready");

    // U8g2 한글 폰트 엔진 — 같은 tft 객체에 부착
    u8g2.begin(tft);
    u8g2.setFontDirection(0);

    // JPEG 디코더 — 16x16 블록 단위 콜백
    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(tjpg_output);
}

static void initInputs() {
    pinMode(PIN_ENC_A,    INPUT_PULLUP);
    pinMode(PIN_ENC_B,    INPUT_PULLUP);
    pinMode(PIN_ENC_PUSH, INPUT_PULLUP);
    pinMode(PIN_KEY_OK,   INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_A), encISR, CHANGE);
    attachInterrupt(digitalPinToInterrupt(PIN_ENC_B), encISR, CHANGE);
}

// ──────────────────────────────────────────────────────────────
// 메인 태스크
// ──────────────────────────────────────────────────────────────
static void displayTask(void* /*param*/) {
    esp_task_wdt_add(nullptr);

    initTft();
    initInputs();
    loadSaverPrefs();
    g_gif.begin(LITTLE_ENDIAN_PIXELS);
    touchInput();   // 부팅 직후 즉시 화면보호기 진입 방지

    Serial.printf("[Display] init OK — saver %s, timeout %ds\n",
                  g_uiSet.saverOn ? "ON" : "OFF", g_uiSet.saverSec);

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(40));   // 25 Hz 루프
        esp_task_wdt_reset();

        // ── 스택 잔여 모니터링 (10초마다 출력) ──
        // 안전 마진 검증 — 잔여 < 1024 bytes면 경고.
        static uint32_t lastStackLogMs = 0;
        uint32_t nowMs = millis();
        if (nowMs - lastStackLogMs >= 10000) {
            UBaseType_t free = uxTaskGetStackHighWaterMark(nullptr);
            Serial.printf("[DisplayTask] free stack: %u bytes%s\n",
                          (unsigned)free,
                          (free < 1024) ? "  ⚠ 위험 — 스택 키워야 함" : "");
            lastStackLogMs = nowMs;
        }

        // ── 1) 입력 ──
        btnPoll(btnPush);
        btnPoll(btnOk);
        int8_t delta = popEncoderSteps();
        bool pushed  = btnConsume(btnPush);
        bool oked    = btnConsume(btnOk);
        bool anyInput = (delta != 0) || pushed || oked;
        if (anyInput) touchInput();

        // ── 1.5) 화면보호기 진입 / 종료 자동 처리 ──
        if (g_ui.page == PAGE_SCREENSAVER) {
            if (anyInput) {
                // 깨어남 — 입력 이벤트는 소비 (메뉴/액션 트리거하지 않음)
                exitScreensaver();
                changePage(PAGE_STATUS);
                continue;
            }
        } else if (g_uiSet.saverOn &&
                   g_ui.page != PAGE_RECIPE_WARN &&
                   (millis() - g_lastInputMs) >= (uint32_t)g_uiSet.saverSec * 1000UL) {
            changePage(PAGE_SCREENSAVER);
        }

        // ── 2) 페이지별 입력 처리 ──
        switch (g_ui.page) {
            case PAGE_STATUS:
                // 회전은 무시. PUSH로 메뉴 진입, KO로 상태별 액션.
                if (pushed) tryEnterMenu();
                if (oked)   onStatusOk();
                break;

            case PAGE_RECIPE_WARN:
                if (oked)   onRecipeWarnOk();          // 확인: 레시피 정지 + 메뉴
                if (pushed) changePage(PAGE_STATUS);   // 취소: 상태창 복귀
                break;

            case PAGE_MENU:
                if (delta != 0) {
                    g_ui.cursor = (int8_t)wrapIndex(g_ui.cursor + delta, MENU_ITEM_COUNT);
                    g_ui.dirty = true;
                }
                if (pushed) onMenuPush();
                if (oked)   onMenuOk();
                break;

            case PAGE_EDIT_SPEED:
            case PAGE_EDIT_ROTSEC:
            case PAGE_EDIT_SAVER_SEC:
                if (delta != 0) {
                    g_ui.editValue = constrain(g_ui.editValue + delta,
                                               g_ui.editMin, g_ui.editMax);
                    g_ui.dirty = true;
                }
                if (oked)   onEditOk();
                if (pushed) onEditPush();
                break;

            case PAGE_INFO:
                if (oked || pushed) changePage(PAGE_MENU);
                break;

            case PAGE_SCREENSAVER:
                // 입력 처리 위 1.5에서 처리됨
                break;
        }

        // ── 3) 페이지 전환 시 chrome 그리기 ──
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

        // ── 4) 페이지 내부 변경 시 컨텐츠 갱신 ──
        if (g_ui.dirty) {
            switch (g_ui.page) {
                case PAGE_STATUS:         renderStatusLive();           break;
                case PAGE_MENU:           renderMenuItems();            break;
                case PAGE_EDIT_SPEED:     renderEditValue("RPM");       break;
                case PAGE_EDIT_ROTSEC:    renderEditValue("sec");       break;
                case PAGE_EDIT_SAVER_SEC: renderEditValue("sec");       break;
                case PAGE_INFO:           /* renderInfoFull로 일괄 */    break;
                case PAGE_RECIPE_WARN:    /* renderRecipeWarnFull로 */   break;
                case PAGE_SCREENSAVER:    /* enterScreensaver로 */       break;
            }
            g_ui.dirty = false;
        }

        // ── 5) 상태창 라이브 갱신 (5 Hz) ──
        uint32_t now = millis();
        if (g_ui.page == PAGE_STATUS &&
            (now - g_ui.lastLiveRefreshMs) >= 200) {
            renderStatusLive();
            g_ui.lastLiveRefreshMs = now;
        }

        // ── 6) 화면보호기 애니메이션 GIF 프레임 진행 ──
        if (g_ui.page == PAGE_SCREENSAVER && g_gifActive) {
            int delayMs = 0;
            int rc = g_gif.playFrame(false, &delayMs);
            if (rc < 0) {
                // 디코딩 오류 — 종료
                g_gif.close();
                g_gifActive = false;
            } else if (rc == 0) {
                // 끝까지 재생 — 처음부터 다시
                g_gif.reset();
            }
            // delayMs 만큼 양보 (다음 프레임까지 대기)
            if (delayMs > 0 && delayMs < 200) vTaskDelay(pdMS_TO_TICKS(delayMs));
        }
    }
}

// ──────────────────────────────────────────────────────────────
// 공개 진입점 — rotary_processor.ino의 setup()에서 호출
// ──────────────────────────────────────────────────────────────
// [스택 크기 8192]
//   기존 4096은 u8g2 한글 글리프 디코드(unifont_t_korean1) + Adafruit_GFX
//   호출 시 스택 한계에 근접 → stack overflow panic 위험.
//   안전 마진 2배 확보. ESP32-S3는 메모리 여유 충분 (PSRAM 8MB).
//   uxTaskGetStackHighWaterMark()로 실시간 잔여 스택 모니터링 (loop 안).
// ──────────────────────────────────────────────────────────────
static TaskHandle_t g_displayTaskHandle = nullptr;

void startDisplayTask() {
    xTaskCreatePinnedToCore(
        displayTask,
        "DisplayTask",
        8192,                  // 4096 → 8192 (u8g2 한글 폰트 + Adafruit_GFX 안전 마진)
        nullptr,
        2,
        &g_displayTaskHandle,
        0
    );
}

// 스택 잔여 모니터링 — 외부에서 호출 가능
// 반환값이 작을수록(<512 bytes) 스택 위험. 0이면 overflow 임박.
uint32_t displayTaskFreeStack() {
    if (!g_displayTaskHandle) return 0;
    return uxTaskGetStackHighWaterMark(g_displayTaskHandle);
}
