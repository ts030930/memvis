#include "MemoryWatchdog.h"
#include "EventLogger.h"
#include <windows.h>

MemoryWatchdog::MemoryWatchdog(QObject* parent)
    : QObject(parent), m_lastLevel2Time(QDateTime::currentDateTime().addSecs(-900)) // 초기엔 쿨다운 패스되게 세팅
{
}

void MemoryWatchdog::onSystemMemoryUpdated(const MonitorTypes::SystemMemoryInfo& sysInfo) {
    m_currentSysInfo = sysInfo;

    // 시스템 정보가 들어올 때마다 Level 1, 2, 4 발동 조건을 체크합니다.
    checkAndExecuteLevel1();
    checkAndExecuteLevel2();
    checkAndExecuteLevel4();
}

void MemoryWatchdog::onAnalyzedDataReady(const QList<MonitorTypes::AnalyzedProcessInfo>& analyzedList) {
    // 분석된 프로세스 리스트가 들어오면, 타겟팅이 필요한 Level 3 발동 조건을 체크합니다.
    checkAndExecuteLevel3(analyzedList);
}

double MemoryWatchdog::getIdleTimeSeconds() {
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD currentTick = GetTickCount();
        DWORD elapsedMillis = currentTick - lii.dwTime;
        return (double)elapsedMillis / 1000.0;
    }
    return 0.0;
}


void MemoryWatchdog::setWatchdogEnabled(bool enabled) {
    m_isEnabled = enabled;

    QString status = enabled ? "활성화됨" : "비활성화됨";
    // 1. 파일에 로그 남기기
    EventLogger::getInstance().log("System", "Watchdog 백그라운드 감시가 " + status);

    // 2. UI 로그 창에 텍스트 띄우기 위해 시그널 방출
    emit logMessage("시스템: Watchdog 백그라운드 감시가 " + status);
}

void MemoryWatchdog::setOptimizationMode(int mode) {
    switch (mode) {
    case ModeStandard:
        m_allowPagefileFlush = true;
        m_thresholdPercent = 85;
        emit logMessage("설정 변경: [표준 모드] 적용 (안정성 및 밸런스)");
        break;

    case ModeGaming:
        m_allowPagefileFlush = false; // 게임 시 디스크 I/O 프리징 방지
        m_thresholdPercent = 70;      // 더 잦은 램 확보
        emit logMessage("설정 변경: [반응속도 극대화 모드] 적용 (디스크 쓰기 제한)");
        break;

    case ModeWorkstation:
        m_allowPagefileFlush = true;
        m_thresholdPercent = 95;      // 웬만해선 개입 안 함
        emit logMessage("설정 변경: [안정성 우선 모드] 적용 (개입 최소화)");
        break;
    }
}

void MemoryWatchdog::setAllowStandbyPurge(bool allow) {
    m_allowStandbyPurge = allow;
}

void MemoryWatchdog::setAllowPagefileFlush(bool allow) {
    m_allowPagefileFlush = allow;
}

void MemoryWatchdog::setAllowHardTrim(bool allow) {
    m_allowHardTrim = allow;
}

void MemoryWatchdog::setThreshold(int percent) {
    m_thresholdPercent = percent;
}


// ----------------------------------------------------
// [Phase 3에서 채워넣을 핵심 로직 공간]
// ----------------------------------------------------
void MemoryWatchdog::checkAndExecuteLevel1() {
    // TODO: sysLoad > 75 && getIdleTimeSeconds() > 60 && cpuLoad < 15 판단 후 NtSetSystemInformation 호출
}

void MemoryWatchdog::checkAndExecuteLevel2() {
    // TODO: sysLoad > 85 && 15분 쿨다운 판단 후 강제 Flush
}

void MemoryWatchdog::checkAndExecuteLevel3(const QList<MonitorTypes::AnalyzedProcessInfo>& analyzedList) {
    // TODO: sysLoad > 90 판단 후 Score 내림차순 정렬 및 타겟 처벌 (SetPriorityClass, Job Object 등)
}

void MemoryWatchdog::checkAndExecuteLevel4() {
    // TODO: Non-Paged Pool 태그 스캔 및 알림
}