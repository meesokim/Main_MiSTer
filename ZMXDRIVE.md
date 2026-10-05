# ZMXDRIVE: MiSTer MSX Core ↔ Main_MiSTer ↔ zmxdrive 하이브리드 연동 구현 계획

## 1. 개요 (Overview)

본 문서는 **MiSTer FPGA의 MSX 코어(Cyclone V RTL)**, **Main_MiSTer (ARM Cortex-A9 Linux)**, 그리고 **RPMP2 프로젝트의 가상 드라이브/슬롯 에뮬레이션 엔진(`zmxdrive.zxl`)**을 실시간으로 인터페이스하는 **하이브리드 카트리지 슬롯 브리지(방안 B)**의 상세 구현 사양 및 단계별 계획을 정의합니다.

### 1.1 연동 목표
- MSX 하드웨어(CPU, VDP, 사운드 등)는 Cyclone V FPGA의 고속 RTL로 충실하게 재현.
- 카트리지 슬롯(Slot 1 또는 Slot 2)의 메모리/IO 트랜잭션을 ARM HPS(리눅스)로 실시간 전달.
- HPS 상에서 구동되는 `zmxdrive` 엔진(`msxread`, `msxwrite`)이 가상 카트리지(MegaSCC, ASCII-8K/16K, 가상 디스크, MP3 오디오)를 처리한 후 결과를 즉각 반환.

---

## 2. 전체 시스템 아키텍처

```
+---------------------------------------------------------------------------------+
|  Cyclone V SoC (DE10-Nano / MiSTer)                                             |
|                                                                                 |
|  [ ARM Cortex-A9 Dual Core (HPS - Linux) ]                                      |
|    │                                                                            |
|    ├─ Main_MiSTer (메인 프로세스)                                               |
|    │    │                                                                       |
|    │    └─ ZMX Server Thread (실시간 I/O 서빙 스레드)                            |
|    │         └─ libzmxdrive.a (C++ 가상 하드웨어 엔진)                          |
|    │              - msxread(cmd, addr)                                          |
|    │              - msxwrite(cmd, addr, val)                                    |
|    │              - SubSlotManager / RPMPROM / FATFS                            |
|    │                                                                            |
|    └─ /dev/mem (mmap) ────────────────────────────────────┐                     |
|                                                           │ (MMIO 직접 접근)    |
|                                                           │ (0xFF20_XXXX)       |
|  [ FPGA Fabric (MSX Core) ]                               ▼                     |
|    ┌────────────────────────────────────────────────────────┐                   |
|    │ zmx_bridge (AXI-Lite Slave / 브리지 로직)              │                   |
|    │  - REG_CMD, REG_ADDR, REG_WDATA, REG_RDATA, REG_STATUS │                   |
|    │  - CPU WAIT_n 인터럽트/핸드셰이크 제어                 │                   |
|    └───────────────────────────┬────────────────────────────┘                   |
|                                │ Slot 2 / IO Bus Control                        |
|    ┌───────────────────────────▼────────────────────────────┐                   |
|    │ emsx_top (MSX 시스템 RTL)                              │                   |
|    │  - Z80 / R800 CPU (pCpuWait_n 신호 수신)               │                   |
|    │  - V9958 VDP, PSG, OPLL, Slot 0/3 (BIOS, Main RAM)     │                   |
|    └────────────────────────────────────────────────────────┘                   |
+---------------------------------------------------------------------------------+
```

---

## 3. 하드웨어 인터페이스 및 레지스터 맵 (MMIO)

Cyclone V의 **Lightweight HPS-to-FPGA 브리지(`0xFF20_0000`)** 영역에 `zmx_bridge` 전용 32비트 레지스터 블록을 할당합니다.

### 3.1 레지스터 맵 (`0xFF20_4000` 기준)

| 오프셋 (Offset) | 이름 (Name) | R/W | 비트폭 | 설명 |
| :--- | :--- | :---: | :---: | :--- |
| `0x00` | `REG_CMD` | R | 8-bit | 트랜잭션 종류 (`zmxbus.h` 규격과 1:1 매핑) |
| `0x04` | `REG_ADDR` | R | 16-bit | 트랜잭션 대상 16비트 주소 (`0x0000 ~ 0xFFFF`) |
| `0x08` | `REG_WDATA`| R | 8-bit | 쓰기 데이터 (`WR` 트랜잭션 시 유효) |
| `0x0C` | `REG_RDATA`| W | 8-bit | 읽기 반환 데이터 (HPS `msxread()` 결과 기록) |
| `0x10` | `REG_STATUS`| R/W | 8-bit | 상태 및 핸드셰이크 제어 (아래 참조) |
| `0x14` | `REG_CTRL` | R/W | 8-bit | 브리지 활성화(ENABLE), 슬롯 선택, 모드 제어 |

### 3.2 상태 레지스터 (`REG_STATUS`) 비트 정의
- `0x00` (`STATUS_IDLE`): 대기 상태. CPU 정상 실행 중.
- `0x01` (`STATUS_REQ_PENDING`): FPGA가 슬롯 트랜잭션 감지 및 CPU WAIT 인가 완료. HPS 처리 대기 중.
- `0x02` (`STATUS_REQ_DONE`): HPS가 데이터 처리 완료 후 기록. FPGA가 데이터 래치 후 CPU WAIT 해제.

### 3.3 커맨드 규격 (`zmxbus.h` 호환)
- `0x00` (`RD_SLTSL1`): Slot 1 읽기
- `0x01` (`WR_SLTSL1`): Slot 1 쓰기
- `0x10` (`RD_SLTSL2`): Slot 2 읽기
- `0x11` (`WR_SLTSL2`): Slot 2 쓰기
- `0x02` (`RD_IO`): I/O 포트 읽기
- `0x03` (`WR_IO`): I/O 포트 쓰기

---

## 4. 실시간 핸드셰이크 프로토콜 및 타이밍 분석

### 4.1 트랜잭션 순서 (Read Cycle 기준)
1. **[FPGA]** MSX CPU가 Slot 2 메모리 영역(`0x4000 ~ 0xBFFF`)을 액세스.
2. **[FPGA]** `zmx_bridge`가 `iSltMerq_n == 0 && xSltRd_n == 0`을 감지.
3. **[FPGA]** 즉시 `pCpuWait_n <= 0` 구동하여 CPU를 WAIT 상태로 유지.
4. **[FPGA]** `REG_CMD <= 0x10`, `REG_ADDR <= iSltAdr`, `REG_STATUS <= 0x01` 기록.
5. **[HPS]** ARM 전담 스레드가 `REG_STATUS == 0x01` 감지.
6. **[HPS]** `rdata = msxread(cmd, addr);` 호출.
7. **[HPS]** `REG_RDATA <= rdata`, `REG_STATUS <= 0x02` 기록.
8. **[FPGA]** `REG_STATUS == 0x02` 감지 후 `iSltDat <= REG_RDATA` 공급.
9. **[FPGA]** `pCpuWait_n <= 1` 해제하여 CPU 동작 재개, `REG_STATUS <= 0x00`.

### 4.2 레이턴시(Latency) 및 타이밍 검증
- **Z80 사이클**: 3.58MHz 기준 1사이클은 **약 279ns**, 읽기 사이클 총 길이는 **약 837ns(3클록)**.
- **AXI 브리지 접근 시간**:
  - HPS ↔ FPGA간 Lightweight AXI 브리지(50MHz~100MHz) MMIO 접근 지연: **~30ns**.
  - ARM Cortex-A9(800MHz) 유저스페이스 스레드 폴링 및 함수 호출: **~150ns ~ 300ns**.
- **결과**: 총 왕복 지연시간은 약 **200ns ~ 400ns**로, Z80의 1~2 WAIT 사이클 이내에 처리가 완료되므로 실시간 동작에 무리가 없습니다.

---

## 5. 단계별 세부 구현 로드맵

### 1단계: RPMP2 zmxdrive ARM 타깃 크로스 컴파일
- **목표**: `~/RPMP2/zmx` 소스코드를 MiSTer ARM(`arm-none-linux-gnueabihf`)용 정적 라이브러리(`libzmxdrive.a`)로 빌드.
- **수행 작업**:
  1. `RPMP2/zmx/Makefile`에 `TARGET_OS=MISTER` 타깃 추가.
  2. 툴체인 경로(`~/Main_MiSTer/gcc-arm-10.2-2020.11-x86_64-arm-none-linux-gnueabihf`) 연동.
  3. `liblhasa_arm.a`, `miniz` 등 종속 모듈 ARMv7 빌드 및 정적 아카이브(`libzmxdrive.a`) 생성.

### 2단계: MSX 코어 FPGA RTL 수정
- **목표**: HPS AXI 브리지와 슬롯 버스를 연결하는 `zmx_bridge.sv` 구현.
- **수행 작업**:
  1. `rtl/peripheral/zmx_bridge.sv` 모듈 작성 (AXI-Lite Slave + WAIT 로직).
  2. `rtl/emsx_top.vhd`에 `zmx_bridge` 슬롯 포트 연결:
     - `iSltMerq_n`, `iSltIorq_n`, `xSltRd_n`, `xSltWr_n`, `iSltAdr`, `iSltDat`
     - CPU `pCpuWait_n` 신호와 결합.
  3. `MSX.sv`의 최상위 모듈에 AXI Lightweight 인터페이스 연결 및 Quartus 재합성.

### 3단계: Main_MiSTer HPS 드라이버 통합
- **목표**: `Main_MiSTer` 프로세스에 ZMX 서비스 스레드를 내장하여 실시간 I/O 중계.
- **수행 작업**:
  1. `Main_MiSTer/support/msx/` 디렉토리 신설.
  2. `zmx_service.cpp` 작성:
     - `/dev/mem`을 열어 `0xFF20_4000`을 `mmap` 매핑.
     - 전담 `pthread` 생성 후 고속 비차단(Non-blocking) 루프에서 `REG_STATUS` 감시.
     - `msxread()` / `msxwrite()` 핸들러 실행.
  3. `Main_MiSTer/Makefile`에 `libzmxdrive.a` 링크 추가.
  4. OSD 메뉴(`menu.cpp`)에 "ZMX Slot2: Enabled/Disabled" 설정 메뉴 항목 추가.

### 4단계: 통합 테스트 및 검증
1. **레지스터 루프백 테스트**: HPS에서 FPGA 레지스터 R/W 검증.
2. **단순 ROM 매핑 테스트**: ZMXDrive 기본 바이너리를 통해 32KB ROM 로딩 및 정상 기동 확인.
3. **MegaSCC / RPMP DSK 테스트**: 고용량 메가롬 및 가상 디스크 드라이브 안정성 검증.
