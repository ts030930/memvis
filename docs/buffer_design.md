# MemVis: Data Pipeline & Architecture Specification

**MemVis (Memory Visualizer)** 프로젝트의 내부 데이터 흐름, 가공 알고리즘, 시각화 메커니즘 및 감시 제어 파이프라인을 소스 코드 수준에서 상세히 분석하여 정의한 기술 사양서입니다.

---

## 1. 아키텍처 개요 (Architectural Overview)

MemVis는 대량의 커널 모드 및 유저 모드 메모리 데이터를 병목 없이 실시간(Per Second)으로 처리하기 위해 **생산자-소비자(Producer-Consumer) 패턴**을 핵심 구조로 채택하였습니다. 

이를 기반으로 데이터의 통계적 가공을 담당하는 **분석 엔진(Analyzer Engine)** 과 시스템 자원 제어 정책을 집행하는 **감시 기구(Watchdog/Actuator)** 가 Qt의 Signal/Slot 메커니즘(Event Loop)을 통해 유기적으로 결합된 **4단계 고도화 데이터 파이프라인**을 형성합니다.

```text
+------------------------------------------------------------------------+
|                      [1단계: Data Production]                          |
|  MemoryWorker (백그라운드 스레드, QTimer 1000ms 주기 구동)             |
|    - GlobalMemoryStatusEx() / GetPerformanceInfo() -> 시스템 정보 수집  |
|    - NtQuerySystemInformation() -> 전체 프로세스 커널 정보 스냅샷       |
|    - VirtualQueryEx() -> 타겟 프로세스 VAD(Virtual Address Space) 분석 |
+------------------------------------------------------------------------+
                                    │
                                    ▼ (Qt Signal: processListUpdated)
+------------------------------------------------------------------------+
|                      [2단계: Data Processing & Analysis]               |
|  LeakAnalyzer (분석 엔진 계층)                                          |
|    - std::deque<MemoryDataPoint> 기반의 슬라이딩 윈도우 큐(Time-Series)  |
|    - 최소자승법(Least Squares Method) 기반 선형 회귀 분석 (Slope 계산)  |
|    - 모델 적합도 판별을 위한 결정계수 ($R^2$) 유도                       |
|    - 다중 가중치 다이내믹 스코어링 (Leak Score 산출: 0 ~ 100)           |
+------------------------------------------------------------------------+
                                    │
            ┌───────────────────────┴───────────────────────┐
            ▼ (Qt Signal: analyzedDataReady)                ▼ (Qt Signal)
+---------------------------------------+ +---------------------------------------+
|        [3단계: Data Visualization]    | |          [4단계: Guard & Actuation]   |
|  ProcessTableModel (UI 데이터 모델)     | |  MemoryWatchdog & EventLogger        |
|    - beginResetModel() 원자적 교체      | |    - GetLastInputInfo() 유휴시간 체크  |
|    - Leak Score 기반 Foreground/      | |    - 운영 모드별 임계치 제어           |
|      Background 동적 색상 매핑         | |    - Level 1~4 단계별 정책 트리거      |
|    - std::sort + 람다 기반 $O(N \log N)$| |    - QMutexLocker 기반 동기화 로깅     |
|      고속 멀티 컬럼 정렬              | |                                       |
+---------------------------------------+ +---------------------------------------+

