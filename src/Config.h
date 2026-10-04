
// ================================================================
//  Config.h — 핀 배정 / 하드웨어 상수 (단일 진실원)
//  ----------------------------------------------------------------
//  모든 핀·물리 상수를 namespace Pin / Cfg 로 모음.
//  #define 매크로 → constexpr 로 전환 (타입 안전, 스코프 격리).
//
//  [ESP32-S3 GPIO 회피] strapping 0/3/45/46, UART0 43/44(TX/RX),
//    USB 19/20, Flash 26~32, OPI PSRAM 35~37, RGB LED 48. 전부 회피.
//
//  [터미널 보드 실측 배치 — 두 변]
//    · A변: 3v3 [g12 g11 g10 g9] g46 g3 g20 g19 [g8 g18 g17 g16 g15 g7] g6 g5 g4 en 3v3 gnd
//           → TFT(12·11·10·9·8·18) + EC11(17·16·15) + KO(7) — 모두 인접
//    · B변: v5in [g13 g14 g21 g47] g48 g45 g0 g35 g36 g37 g38 g39 [g40 g41 g42] rx tx g2 g1 gnd
//           → MAX31865(13·14·21·47) + TMC2209(42·41·40)
// ================================================================
#pragma once
#include <Arduino.h>

namespace Pin {
    // TMC2209 스테퍼 (출력 전용) — B변, 연속 3핀
    constexpr uint8_t STEP = 42;
    constexpr uint8_t DIR  = 41;
    constexpr uint8_t EN   = 40;   // LOW=코일 활성, HIGH=전류 차단

    // MAX31865 PT100 (소프트웨어 SPI) — B변, v5in 옆 연속 4핀
    constexpr uint8_t MAX_CS   = 13;
    constexpr uint8_t MAX_MOSI = 14;
    constexpr uint8_t MAX_MISO = 21;
    constexpr uint8_t MAX_CLK  = 47;

    // ST7789 TFT (하드웨어 FSPI: SCK=12, MOSI=11 IO-MUX 직결) — A변
    constexpr uint8_t TFT_SCK  = 12;
    constexpr uint8_t TFT_MOSI = 11;
    constexpr uint8_t TFT_CS   = 10;
    constexpr uint8_t TFT_DC   = 9;
    constexpr uint8_t TFT_RST  = 8;
    constexpr uint8_t TFT_BL   = 18;

    // EC11 인코더 + 확정 버튼 — A변, TFT 바로 옆 연속
    constexpr uint8_t ENC_A    = 17;
    constexpr uint8_t ENC_B    = 16;
    constexpr uint8_t ENC_PUSH = 15;
    constexpr uint8_t KEY_OK   = 7;
}

namespace Cfg {
    constexpr const char* FW_VERSION = "v4.6.4";

    // 모터 — 마이크로스텝 8 (MS1=LOW, MS2=LOW) → 1600 step/rev
    constexpr int   STEPS_PER_REV = 1600;        // 200 × 8
    constexpr float GEAR_RATIO    = 3.71f;
    constexpr float MAX_OUTPUT_RPM = 80.0f;      // 탈조 한계 기반 상한
    constexpr float MIN_OUTPUT_RPM = 5.0f;       // 실속 방지 하한

    // step/s 환산
    constexpr float MAX_SPEED = MAX_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV; // ≈7915
    constexpr float MIN_SPEED = MIN_OUTPUT_RPM * GEAR_RATIO / 60.0f * STEPS_PER_REV;
    // S-커브(소프트스타트) 가감속 — 설정 RPM 도달까지 RAMP_SEC초.
    //   FastAccelStepper setLinearAcceleration: 가속도를 0→a로 선형 증가(저크 제한).
    //   handover(선형가속→정가속 전환)를 목표속도의 SCURVE_HANDOVER 지점에 둠.
    //   목표속도 v_t에 대해 per-run 계산:  a = v_t·(1+f)/T,  s_h = f²·v_t·T / (1.5·(1+f))
    constexpr float RAMP_SEC        = 2.0f;      // 0 → 설정RPM 도달 시간(가속·감속 동일)
    constexpr float SCURVE_HANDOVER = 0.5f;      // 저크제한 구간 비율(목표속도의 f까지 소프트스타트)

    // 타이밍
    constexpr uint32_t REST_MS = 2000;           // 방향전환 휴지 (관성 제어 + TMC2209 열관리)

    // 온도 센서 활성화 — false면 MAX31865 init·폴링 태스크 미생성.
    //   (보드 불량으로 임시 격리. temperature()=TEMP_UNREAD, fault()=0 → "--.-"표시)
    //   양품 보드 장착 시 true로 복귀.
    constexpr bool TEMP_SENSOR_PRESENT = false;

    // MAX31865 (RREF 실측 하드코딩)
    constexpr float RREF     = 412.0f;
    constexpr float RNOMINAL = 100.0f;
    constexpr uint16_t TEMP_FAULT_CLEAR_AFTER = 5;  // 연속 fault N회 후만 clear

    // RTD 결선 모드 — ★보드 솔더점퍼와 반드시 일치★ (2 / 3 / 4)
    //   3선식: 리드선 저항 보상 → 컬러(C-41/E-6) 온도정밀도에 유리.
    //   ★보드 솔더점퍼를 3선식으로 설정 + 페어→F+/RTD+, 단일선→RTD-,
    //     F-↔RTD-는 3선 점퍼가 내부 브리지★. 점퍼/펌웨어 불일치 시 FORCE-
    //     개방 fault(0x10/0x08) → 그땐 임시로 2로 내려 격리 검증 가능.
    constexpr uint8_t RTD_WIRES = 2;

    // 센티넬 온도값
    constexpr float TEMP_UNREAD = -999.0f;

    // 온도 스파이크 제거 — 수조(수 L)는 1초에 이만큼 못 변함 → 초과치는 노이즈로 버림.
    //   연속 N회 초과면 실제 변화(센서 이동 등)로 보고 새 기준 수용.
    constexpr float   TEMP_MAX_STEP_C         = 2.0f;
    constexpr uint8_t TEMP_SPIKE_ACCEPT_AFTER = 3;

    // ── 노이즈 가드 (NoiseGuard) ──
    //   ★실측 보정용 노브★ — 10초마다 [Noise] peak 로그 보고 조정. 정상 조작 피크의 ~3배로.
    //   입력 윈도우(NOISE_WIN_MS) 내 아래 중 하나라도 넘으면 → 패널 입력 차단 INPUT_BLOCK_MS.
    constexpr uint32_t NOISE_WIN_MS         = 500;
    constexpr uint32_t NOISE_ENC_EDGE_MAX   = 2000; // 인코더 ISR 폭주 상한 (Core 0 보호용, 바운스 포함 손조작보다 충분히 큼)
    constexpr uint32_t NOISE_ENC_BAD_MAX    = 60;   // A·B 동시변화(물리적으로 불가능한 전이) — 손조작이 아닐 때만 판정
    //   윈도우 내 한 방향 순이동이 이 전이 수 이상이면 손조작으로 보고 BAD 판정 면제
    //   (4전이 = 1디텐트. 노이즈는 제자리 왕복이라 순이동 ≈ 0, 손은 한 방향으로 누적)
    constexpr int32_t  NOISE_ENC_HUMAN_NET  = 8;
    constexpr uint32_t NOISE_BTN_GLITCH_MAX = 3;    // 디바운스 확정 전 원복된 순간 펄스
    constexpr uint32_t INPUT_BLOCK_MS       = 3000;
    //   ESCALATE_WIN_MS 안에 버스트 N회 → 모터 정지(레시피는 일시정지=재개 가능)
    constexpr uint8_t  NOISE_ESCALATE_BURSTS = 3;
    constexpr uint32_t NOISE_ESCALATE_WIN_MS = 60000;
    //   비정상 리셋(브라운아웃/패닉/WDT) 연속 N회 → 모터 잠금 (전원 재투입으로 해제)
    //   STABLE_MS 이상 정상 가동하면 카운터 리셋.
    constexpr uint8_t  GUARD_BAD_RESETS = 3;
    constexpr uint32_t GUARD_STABLE_MS  = 60000;

    // 웹 JSON 본문 상한 — 비정상 Content-Length로 힙 고갈 방지
    constexpr size_t MAX_JSON_BODY = 16384;
    constexpr size_t MAX_RECIPES_BODY = 65536;   // /recipes.json 상한 (검증 시 통째 파싱하므로 힙 보호)
}
