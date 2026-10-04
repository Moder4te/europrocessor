# 현존 이슈 정리 — 2026-10-04

대상: `rotary_processor` 펌웨어 v4.3 → v4.4(작업 중, **미커밋·실기 미검증**)
기본 사용 환경: **집 WiFi 없음, 폰/PC가 보드 AP(`FilmProcessor`, 192.168.4.1)에 직접 접속**

우선순위: 🔴 즉시 / 🟠 다음 업데이트 / 🟡 여유 있을 때

---

## 1. 🔴 웹 명령 지연 누적 (장시간 사용 시 명령 도달 시간이 길어짐)

**증상**: 오래 켜두면 웹 버튼(정지/일시정지 등) 누른 뒤 반응까지 시간이 점점 늘어남.
아직 원인 확정 전. 가능성 높은 순으로 정리.

### 1-1. 상태 폴링 요청 적체 — 가장 유력 ✅ v4.4 수정 (응답 완료 후 500ms 체인 + 2초 타임아웃 + 동시 1건 + 백그라운드 탭 중지)
- `web_assets.h:552` — `setInterval(fetchStatus, 500)`. 앞 요청이 끝났는지 확인하지 않고 0.5초마다 새 요청을 보냄. 타임아웃도 없음.
- 응답이 한 번 느려지면(WiFi 재전송, 힙 단편화, AsyncTCP 큐 등) 끝나지 않은 `/api/status` 요청이 계속 쌓임.
  브라우저의 호스트당 동시 연결 한도(약 6개)를 다 차지 → **`/api/stop` 같은 명령 POST가 폴링 뒤에 줄을 섬** → 체감 지연.
- 명령 함수(`doStop` 등)도 끝나면 `fetchStatus()`를 한 번 더 호출해 적체를 키움.
- ESPAsyncWebServer는 응답마다 연결을 닫음(keep-alive 없음) → 0.5초마다 TCP 연결을 새로 맺음 = 시간당 7,200회. lwIP PCB/TIME_WAIT 회전 부담.
- **수정안**: 끝나면 다음 요청을 예약하는 방식(`setTimeout` 체인) + `AbortController` 타임아웃 2초 + 백그라운드 탭에선 폴링 중지(`document.hidden`). 주기 1초로 완화 검토.

### 1-2. STA 재연결 폭주 (홈 WiFi SSID가 저장돼 있는 경우) ✅ v4.4 수정 (SSID 없으면 AP 단독, 있으면 30s→5분 백오프, core 자동재연결 OFF)
- `WifiManager.cpp` — 항상 `WIFI_AP_STA` 모드. 저장된 홈 SSID가 범위 밖이면 `STA_DISCONNECTED` 이벤트마다 `WiFi.reconnect()` 즉시 호출 + `setAutoReconnect(true)` 중복.
- 재연결 시도 = 채널 스캔. **AP와 STA가 라디오 하나를 같이 쓰므로** 스캔 중엔 AP 클라이언트 패킷이 지연/유실 → 장시간 누적 시 체감 지연.
- 확인: 시리얼에 `[STA] 연결 끊김 — 재연결 시도`가 반복되는지. 설정 페이지에서 홈 SSID가 비어 있는지.
- **수정안**: STA SSID가 없으면 `WIFI_AP` 단독 모드. 있으면 재연결을 30초 이상 간격으로 제한하고, N회 실패 시 STA 끄기.

### 1-3. 힙 단편화 — 🔍 측정 로그 추가 (v4.4 `[Heap]` 10초 주기: 내부 RAM free/min/largest). 장시간 운전 후 추이 판정 필요
- `/api/status`마다 `JsonDocument` + `String`(reserve 1024) 할당/해제. 0.5초 주기 × 수 시간.
- 현재 힙 추이 로그 없음 → 확인 불가.
- **수정안**: 10초 로그에 `ESP.getFreeHeap()` / `ESP.getMinFreeHeap()` / `heap_caps_get_largest_free_block()` 추가해 장시간 추이 측정. 줄어들면 정적 버퍼로 응답.

### 1-4. WiFi 절전 / 송신 설정 ✅ v4.4 수정 (`WiFi.setSleep(false)`)
- `WiFi.setSleep()` 미설정(core 2.x 기본 = modem sleep). AP+STA 모드에서 지연이 늘어나는 원인으로 자주 보고됨.
- **수정안**: `WiFi.setSleep(false)` 한 줄. 전력 증가는 미미.

### 1-5. 클라이언트(폰) 쪽 요인
- 인터넷 없는 AP에 붙은 폰은 WiFi 절전·"인터넷 없음" 처리로 패킷을 늦게 보내기도 함(특히 Android).
- 확인: PC 브라우저로 같은 증상이 나는지 비교.

### 진단 순서 (권장)
1. 지연 발생 시 브라우저 개발자도구 Network 탭 → `/api/status` pending 쌓임 확인 (→ 1-1 확정)
2. 시리얼에서 `[STA]` 반복 로그 확인 (→ 1-2)
3. 힙 로그 추가 후 2시간 이상 연속 운전 (→ 1-3)
4. 1-1·1-2·1-4는 v4.4에 적용됨 → 실기에서 2시간 이상 연속 운전 후에도 STOP 반응이 즉시인지 확인. 남으면 1-3(힙) 측정

---

## 2. 하드웨어 — 펌웨어로 해결 불가

| # | 우선 | 이슈 | 조치 |
|---|---|---|---|
| H1 | 🔴 | TMC2209 EN/STEP/DIR(GPIO40/42/41, JTAG 겸용 핀)이 **전원 투입~setup 진입, 플래싱 중 내내, 크래시/WDT 리셋마다** 플로팅. ENN 풀다운 모듈이면 이 구간에 드라이버가 켜지고 코일에 전류가 흐름 | **EN→3.3V 10kΩ 풀업 필수** (진행상황 문서에 미완료) |
| H2 | 🟠 | 전원전압 감지·드라이버 DIAG 미배선 → 전압 강하/과열/단락을 소프트웨어로 감지 불가 | DIAG→GPIO38, VM 분압→GPIO1(ADC1). 배선하면 즉시 차단 로직 추가 가능 |
| H3 | 🟠 | 인코더 선이 스테퍼 EMI에 노출 → 유령 입력 (NoiseGuard 오작동 원인) | 꼬임선, ENC_A/B–GND 10nF |
| H4 | 🟡 | 부팅 시 WiFi 순간 전류 → 브라운아웃 위험 | `[Guard] 브라운아웃` 로그가 뜨면 3.3V에 470µF 벌크 커패시터 |
| H5 | 🟠 | `Cfg::RTD_WIRES = 2`인데 진행상황 문서는 3선식. 온도센서 현재 비활성(`TEMP_SENSOR_PRESENT=false`) | 센서 재활성화 전 솔더점퍼와 일치시킬 것 |
| H6 | 🟡 | 미확인 항목: MS1/MS2 = 1/8 배선, Rsense 값, 12V 탈조, 모터 챔버 씰 | 진행상황 문서 오픈 항목 유지 |

---

## 3. 소프트웨어 — 미수정

| # | 우선 | 이슈 | 위치 | 수정안 |
|---|---|---|---|---|
| S1 | 🟠 | 웹 인증 없음 + CORS `*` + 기본 AP 비밀번호 `12345678`. AP에 붙은 누구나, 또는 브라우저 안의 아무 페이지나 `/api/manual` POST로 모터 기동 가능 | `WebServer.cpp`, `WifiManager.cpp` | 최초 부팅 시 AP 비밀번호 변경 유도, CORS를 레시피 에디터 용도로만 제한 |
| S2 | 🟠 | 홈 화면 OK 한 번 = 모터 퀵스타트. 노이즈/오조작에 취약 | `DisplayUI.cpp` `onStatusOk` | 1초 길게 누르기 |
| S3 | 🟠 | `/api/recipes/save`는 모터 운전 중에도 플래시 쓰기 (화면보호기 업로드는 차단함). 플래시 소거 중 캐시 정지 → FastAccelStepper 큐 고갈 → 모터 덜컹 가능성 (미검증) | `WebServer.cpp` | 운전 중 409 반환, 또는 실측 확인 |
| S4 | 🟡 | EN LOW 직후 바로 회전 시작. StealthChop 자동튜닝(AT#1)은 정지 상태로 약 130ms 필요 → 출발 시 토크 부족 가능 (미검증, 12V 탈조와 연관) | `MotionController.cpp` REST | REST 끝나기 150ms 전에 EN 활성 |
| S5 | 🟡 | `/api/settings` POST에서 빠진 필드는 기본값으로 덮어씀 → STA 설정이 의도치 않게 지워짐 | `WebServer.cpp` | 있는 필드만 갱신 |
| S6 | 🟡 | `/api/manual`, `/api/settings` 본문 핸들러가 청크 분할을 무시하고 첫 청크만 파싱 (작은 JSON이라 현재는 문제없음) | `WebServer.cpp` | `/api/start`처럼 버퍼 조립 |
| S7 | 🟡 | 명령 큐(8)가 가득 차면 패널의 SAFE_STOP이 조용히 유실됨 (드묾) | `CommandQueue.h` | 정지류는 estop 플래그처럼 큐 우회 |
| S8 | 🟡 | 레시피 목록 커서가 `int8_t` → 한 카테고리에 128개 이상이면 오버플로 | `DisplayUI.cpp` | `int16_t` |
| S9 | 🟡 | 웹 UI가 새 `guard`(ok/noise/locked) 상태를 표시하지 않음 | `web_assets.h` | 상태바에 경고 칩 추가 |

---

## 4. v4.4 작업분 (미커밋, 빌드만 통과, 실기 미검증)

빌드: RAM 16.8% / Flash 45.7%

| 수정 | 파일 |
|---|---|
| 부팅 시 EN 핀 글리치 제거 (레벨 먼저 쓰고 OUTPUT 전환) | `main.cpp` |
| 비상정지 시 큐/대기 레시피 폐기 (정지 직후 재기동 레이스) | `main.cpp` |
| SPI 미통신 시 약 988°C가 정상값으로 표시되던 버그 → fault 처리 + 온도 스파이크 필터 | `TemperatureSensor.cpp` |
| LittleFS 첫 마운트 실패 시 즉시 포맷 → 1회 재시도 후에만 포맷 | `main.cpp` |
| `/api/start` 본문 16KB 상한 (힙 고갈 방지) | `WebServer.cpp` |
| AP SSID/비밀번호 길이 검증 (AP 접속 불가 방지) | `WebServer.cpp` |
| 버전 표기 통일 `Cfg::FW_VERSION` | `Config.h` 외 |
| 웹 폴링 적체 방지, AP 단독 모드, STA 재연결 백오프, WiFi 절전 OFF (1-1·1-2·1-4) | `web_assets.h`, `WifiManager.*` |
| 웹 STOP/안전정지: 1.5초 타임아웃 × 5회 재시도, 최종 실패 시 경고창. `/api/safestop` 큐 full이면 503(기존: 실패해도 200) | `web_assets.h`, `WebServer.cpp` |
| 홈 WiFi SSID 비우고 저장 시 비번도 삭제 → 재부팅 후 AP 단독 (v4.5.1) | `WebServer.cpp` |
| `[Heap]` 10초 로그 + 웹 About 카드에 메모리/가동시간/가드 표시 | `main.cpp`, `WebServer.cpp`, `web_assets.h` |
| **OTA (v4.5)** ✅ 실기 검증 2026-10-04 (웹 업로드 v4.5→v4.5.1, 60s 확정 로그 확인. 자동 롤백 경로는 미검증): 설정 탭에서 firmware.bin 업로드. X-OTA 헤더(CSRF 차단), 모터 동작 중 거부, 새 펌웨어 크래시 루프 시 이전 파티션 자동 롤백 | `WebServer.cpp`, `main.cpp`, `web_assets.h` |
| **v4.6 멀티 보드**: 보드 번호(1~9) → 이름 `europrocessorN.local`·AP 대역 192.168.(3+N).1 분리, AP 최대 접속 4→10, `/multi` 페이지(상태·정지·일시정지·다음단계·레시피 동시 시작), `/api/recipe` 단계 목록 | `WifiManager.*`, `WebServer.cpp`, `web_multi.h` |
| **v4.6 단계 이동**: 상태 탭 진행도 바에서 단계 클릭 → 확인창 → `/api/goto`로 해당 단계 이동 | `RecipeRunner.*`, `main.cpp`, `web_assets.h` |
| `/api/start` 요청별 버퍼(동시 시작 시 본문 섞임 수정), OTA 동시 업로드 409 busy | `WebServer.cpp` |
| **v4.6.1**: 일시정지·단계 선택·건너뛰기 시 S-커브 감속 정지 후 동작 (기존: 즉시 정지 / 회전 중 바로 속도·방향 전환). `freeze()` 삭제 | `RecipeRunner.*`, `MotionController.*` |
| **v4.6.2**: 인코더 노이즈 판정 완화 — 무변화 전이(바운스) 제외, 한 방향 순이동 ≥ 2디텐트면 손조작으로 보고 면제, ISR 폭주 상한 600→2000/500ms | `DisplayUI.cpp`, `Config.h` |
| **v4.6.3 🔴 reset 4(패닉) 수정**: 모터 운전 중 TFT "Run recipe" 진입 시 크래시. UART 백트레이스 → `pcnt_intr_service`가 PSRAM에 할당된 `p_pcnt_obj`(0x3d800908)를 플래시 읽기(캐시 OFF) 중 접근. IDF 4.4 pcnt 드라이버가 `MALLOC_CAP_DEFAULT`로 할당하는 버그. FAS 초기화 동안 PSRAM을 점유해 내부 RAM 할당 강제 | `MotionController.cpp` |
| **v4.6.4 운전 중 플래시/재부팅 전면 차단**: 보드가 `motorBusy()`(회전·감속·레시피 진행/일시정지/확인대기)면 설정 저장(재부팅)·레시피 저장·화면보호기 업로드/삭제·OTA 모두 409. TFT 화면보호기 설정·OTA 확정 NVS 쓰기는 정지 시까지 지연. 레시피 저장: 동시 저장 409, 64KB 상한, 쓰기 실패 감지, 커밋 전 JSON 검증. 웹: 저장 대기/재시도/실패 표시, 대기 중 페이지 이탈 경고, 설정 저장 결과 확인 | `WebServer.*`, `DisplayUI.cpp`, `main.cpp`, `web_assets.h` |
| **NoiseGuard 신규**: 인코더/버튼 노이즈 → 패널 입력 3초 차단, 60초 내 3회 → 모터 정지(레시피는 일시정지). 연속 비정상 리셋 3회 → 모터 잠금 | `NoiseGuard.*`, `DisplayUI.cpp` |

**실기 검증 시 확인할 것**
- [ ] 부팅/리셋 시 모터가 움찔하지 않는지 (H1 풀업 장착 전후 비교)
- [ ] `[Noise] 10s peak` 로그로 정상 조작 피크 측정 → `Config.h`의 `NOISE_*` 값을 그 3배 정도로 보정 (현재값은 추정치)
- [ ] 모터 75RPM 운전 중 패널 조작 → `NOISE` 오작동 없는지
- [ ] 웹 STOP 반응 시간

---

## 5. 2026-10-04 실측에서 발견

| # | 우선 | 이슈 | 조치 |
|---|---|---|---|
| F1 | 🟠 | **USB 시리얼 포트를 닫으면 보드가 리셋됨** (USB-Serial-JTAG가 DTR/RTS 변화를 리셋으로 처리). 현상 중 노트북 모니터를 닫거나 케이블을 뽑으면 모터 정지·레시피 소실 | 모니터는 `pio device monitor --dtr 0 --rts 0`로 열기. 운전 중 USB 분리 금지 |
| F2 | 🟡 | 보드 재부팅 후 폰/PC가 AP에 재접속하기까지 60초 이상 (Windows는 자동 재접속 안 되는 경우도 있음) | 클라이언트 측 문제. 재부팅 후 WiFi 수동 재연결 안내 |
| F3 | 🟡 | `resetReason 0` = 원인 불명(대부분 USB 리셋). 전원 투입은 1, 소프트웨어 재시작은 3 | 문서화만 |
| F5 | 🟡 | 코어덤프가 켜져 있지만 `coredump` 파티션이 없어 크래시 기록이 안 남음. 파티션 변경은 LittleFS 크기가 바뀌어 레시피가 지워지므로 보류 — 크래시 분석은 UART(GPIO43/44) 녹화로 | — |
| F4 | ✅ | 동시 연결 부하: PC 1대에서 12개 동시 요청 × 40회 = 480/480 성공, 평균 49ms. 메모리 최저 199KB 후 회복 | — |

