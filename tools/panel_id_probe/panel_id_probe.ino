/*
 * ================================================================
 *  panel_id_probe.ino — ST7789 panel ID 응답 확인
 *
 *  panel이 살아있는지 100% 확정 진단:
 *    RDDID (0x04) 명령 → panel이 4바이트 응답
 *    응답 마지막 바이트가 0x52면 ST7789 살아있음
 *
 *  배선 필요사항:
 *    기존 핀 + Panel MISO/SDO → ESP32-S3 GPIO 13
 *    (panel에 MISO 핀이 있어야 가능)
 *
 *  ※ 이 스케치는 Adafruit 라이브러리 안 쓰고 SPI 직접 사용
 * ================================================================
 */
#include <SPI.h>

#define PIN_TFT_SCK   12
#define PIN_TFT_MOSI  11
#define PIN_TFT_MISO  13   // panel SDO/MISO 연결 필요
#define PIN_TFT_CS    10
#define PIN_TFT_DC     9
#define PIN_TFT_RST   14
#define PIN_TFT_BL    21

SPIClass& spi = SPI;

static void txCmd(uint8_t cmd) {
    digitalWrite(PIN_TFT_DC, LOW);   // command 모드
    digitalWrite(PIN_TFT_CS, LOW);
    spi.transfer(cmd);
    digitalWrite(PIN_TFT_CS, HIGH);
}

static uint8_t rxByte() {
    digitalWrite(PIN_TFT_DC, HIGH);  // data 모드
    digitalWrite(PIN_TFT_CS, LOW);
    uint8_t b = spi.transfer(0x00);
    digitalWrite(PIN_TFT_CS, HIGH);
    return b;
}

void setup() {
    Serial.begin(115200);
    delay(1500);
    Serial.println("\n[Probe] ST7789 ID 진단 시작");

    pinMode(PIN_TFT_BL, OUTPUT);
    pinMode(PIN_TFT_CS, OUTPUT);
    pinMode(PIN_TFT_DC, OUTPUT);
    pinMode(PIN_TFT_RST, OUTPUT);
    digitalWrite(PIN_TFT_CS, HIGH);

    // Hardware reset
    digitalWrite(PIN_TFT_RST, HIGH); delay(50);
    digitalWrite(PIN_TFT_RST, LOW);  delay(100);
    digitalWrite(PIN_TFT_RST, HIGH); delay(300);
    Serial.println("[Probe] reset 완료");

    spi.begin(PIN_TFT_SCK, PIN_TFT_MISO, PIN_TFT_MOSI);
    spi.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    Serial.println("[Probe] SPI 2MHz mode 0");

    digitalWrite(PIN_TFT_BL, HIGH);

    // Sleep out — panel 활성화
    txCmd(0x11);
    delay(120);
    Serial.println("[Probe] Sleep Out (0x11) 송신");

    // ── RDDID (0x04) — Read Display ID ──
    // 응답: dummy + MID + Module ver + Module ID
    Serial.println("\n[Probe] === RDDID (0x04) ===");
    txCmd(0x04);
    uint8_t dummy = rxByte();
    uint8_t mid   = rxByte();
    uint8_t mver  = rxByte();
    uint8_t mod   = rxByte();
    Serial.printf("  dummy=0x%02X  MID=0x%02X  ModVer=0x%02X  ModID=0x%02X\n",
                  dummy, mid, mver, mod);
    Serial.println("  ※ ModID=0x52 → ST7789 정상");
    Serial.println("  ※ 전부 0x00 또는 0xFF → panel 무응답 = 사망");

    // ── RDDST (0x09) — Read Display Status ──
    Serial.println("\n[Probe] === RDDST (0x09) ===");
    txCmd(0x09);
    uint8_t st[5];
    for (int i = 0; i < 5; i++) st[i] = rxByte();
    Serial.printf("  bytes: 0x%02X 0x%02X 0x%02X 0x%02X 0x%02X\n",
                  st[0], st[1], st[2], st[3], st[4]);
    Serial.println("  ※ 전부 0xFF 또는 0x00 → panel 무응답");

    // ── RDPM (0x0A) — Read Display Power Mode ──
    Serial.println("\n[Probe] === RDPM (0x0A) ===");
    txCmd(0x0A);
    uint8_t d = rxByte();
    uint8_t pm = rxByte();
    Serial.printf("  dummy=0x%02X  PowerMode=0x%02X (bin %d%d%d%d%d%d%d%d)\n",
                  d, pm,
                  (pm>>7)&1,(pm>>6)&1,(pm>>5)&1,(pm>>4)&1,
                  (pm>>3)&1,(pm>>2)&1,(pm>>1)&1,pm&1);
    Serial.println("  ※ bit7=Booster, bit6=Idle, bit5=Partial, bit4=Sleep OUT, bit2=Display ON");

    spi.endTransaction();
    Serial.println("\n[Probe] === 진단 완료 ===");
    Serial.println("  → ModID(0x52) 또는 의미 있는 PowerMode 비트 보이면 panel 살아있음");
    Serial.println("  → 전부 0x00/0xFF 응답이면 panel 또는 MISO 배선 문제");
}

void loop() {
    delay(5000);
}
