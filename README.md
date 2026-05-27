# MemVis (Memory Visualizer)

## 프로젝트 소개
**MemVis**는 Windows 환경에서 시스템 전반의 메모리 사용량을 프로파일링하고 메모리 누수를 정밀하게 탐지하기 위해 개발된 저수준 메모리 분석 및 시각화 도구입니다. Windows/NT API를 직접 활용하여 심도 있는 시스템 메모리 데이터를 수집하며, 직관적인 GUI를 통해 복잡한 메모리 상태를 실시간으로 모니터링할 수 있는 환경을 제공합니다.

## 주요 기능
* **시스템 전반의 메모리 프로파일링:** `ntdll.dll` 및 저수준 Windows API를 활용한 시스템 및 프로세스 단위의 메모리 상태 수집
* **메모리 누수 탐지:** 세밀한 메모리 할당 패턴 분석을 통한 누수 의심 영역 추적
* **비동기 실시간 데이터 처리:** Producer-Consumer 패턴을 도입하여 데이터 수집(Producer)과 UI 렌더링(Consumer)을 분리, 지연 없는 실시간 모니터링 구현
* **고급 메모리 스캐닝 (현재 Phase 2):** 심화된 메모리 영역 스캔 및 분석 정보 시각화 제공

## 기술 스택
* **Language:** C++
* **GUI Framework:** Qt 6
* **OS / System API:** Windows API, NT API (`ntdll.dll`)

## 아키텍처 개요
대량의 메모리 데이터를 병목 없이 처리하기 위해 **Producer-Consumer 패턴**을 핵심 아키텍처로 사용합니다.
* **Producer:** 백그라운드에서 NT API를 지속적으로 호출하여 시스템 메모리 지표를 수집하고 버퍼에 적재합니다.
* **Consumer:** Qt 6 메인 스레드에서 버퍼의 데이터를 가져와 UI에 시각적 요소로 렌더링하여 데이터 수집과 렌더링의 결합도를 낮췄습니다.

## 개발 로드맵
- [x] **Phase 1: Foundation** - 프로젝트 기반 아키텍처 설계, NT API 연동 및 기본 Producer-Consumer 파이프라인 구축
- [🏃] **Phase 2: Advanced Scanning & UI Refinement (진행 중)** - 고급 메모리 스캐닝 로직 구현 및 Qt 기반 GUI 사용자 경험 고도화
- [ ] **Phase 3: Optimization** - 대규모 메모리 데이터 처리 성능 최적화 및 안정화

## 빌드 및 실행 방법

현재 이 프로젝트는 Visual Studio 기반으로 구성되어 있습니다.

1. **사전 요구 사항:**
   * Visual Studio (C++ 데스크톱 개발 워크로드 포함)
   * Qt 6 프레임워크 설치
   * Visual Studio 내 **Qt Visual Studio Tools** 확장 프로그램 설치 및 Qt 6 경로(버전) 연동

2. **빌드 및 실행:**
   ```bash
   # 저장소 클론
   git clone [https://github.com/ts030930/memvis.git](https://github.com/ts030930/memvis.git)
   cd memvis
