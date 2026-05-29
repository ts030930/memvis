#include "MemoryWatchdog.h"
#include "EventLogger.h"
#include <windows.h>
#include <winternl.h>
#include <psapi.h>
#include <algorithm>
#include <QDebug>

// ----------------------------------------------------------------
// 🛠️ NT API 호출을 위한 저수준 커널 구조체 및 열거형 선언
// ----------------------------------------------------------------

// NtSetSystemInformation용 메모리 커맨드 열거형 (Level 1, 2 공통 사용)
typedef enum _SYSTEM_MEMORY_LIST_COMMAND {
    MemoryCaptureAccessedBits,
    MemoryCaptureAndResetAccessedBits,
    MemoryEmptyWorkingSets,
    MemoryFlushModifiedList,          // 3: Modified Page 개입 (Level 2)
    MemoryPurgeStandbyList,           // 4: 전체 Standby List 정리 (Level 2 연쇄)
    MemoryPurgeLowPriorityStandbyList,// 5: 저우선순위 0~2 정리 (Level 1)
    MemoryStoreSystemInformation,
    MemoryClearFreePageDirt,
    MemoryCombinePageFreeList
} SYSTEM_MEMORY_LIST_COMMAND;

// NT API 함수 포인터 타입 정의
typedef NTSTATUS(WINAPI* PNtSetSystemInformation)(
    INT SystemInformationClass,
    PVOID SystemInformation,
    ULONG SystemInformationLength
    );

// ----------------------------------------------------------------
// ⚙️ 생성자 및 기본 유틸리티 함수
// ----------------------------------------------------------------

MemoryWatchdog::MemoryWatchdog(QObject* parent)
    : QObject(parent),
    m_isEnabled(false),
    m_allowStandbyPurge(true),
    m_allowPagefileFlush(true),
    m_allowHardTrim(true),
    m_thresholdPercent(85),
    m_lastLevel2Time(QDateTime::currentDateTime().addSecs(-900)) // 초기 쿨다운 15분 통과 세팅
{
    // 정적 화이트리스트 초기화
    m_globalWhitelist.insert("chrome.exe");
    m_globalWhitelist.insert("memvis1.exe"); // 자기 자신 보호
}

void MemoryWatchdog::onSystemMemoryUpdated(const MonitorTypes::SystemMemoryInfo& sysInfo) {
    if (!m_isEnabled) return;

    m_currentSysInfo = sysInfo;

    // 1Hz 주기로 시스템 지표를 감시하며 Level 1, Level 2 정책 집행 여부 판단
    checkAndExecuteLevel1();
    checkAndExecuteLevel2();
}

void MemoryWatchdog::onAnalyzedDataReady(const QList<MonitorTypes::AnalyzedProcessInfo>& analyzedList) {
    if (!m_isEnabled) return;

    // 프로세스별 위험 점수 정렬 기반 Level 3 정책 집행 여부 판단
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

// ----------------------------------------------------------------
// 🚀 [Level 1] 예방적 캐시 정리 (Safe Soft Optimization)
// ----------------------------------------------------------------
void MemoryWatchdog::checkAndExecuteLevel1() {
    if (!m_allowStandbyPurge) return;

    // 발동 조건: 사용자가 설정한 임계치 초과 && 자리비움 1분 이상 && CPU 로드 < 15% (미디어 시청 방어)
    if (m_currentSysInfo.memoryLoad <= m_thresholdPercent) return;
    if (getIdleTimeSeconds() <= 60.0) return;
    if (m_currentSysInfo.cpuLoad >= 15.0) return;

    // SeProfileSingleProcessPrivilege 권한 확보
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        if (LookupPrivilegeValue(NULL, SE_PROF_SINGLE_PROCESS_NAME, &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(hToken);
    }

    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (hNtDll) {
        PNtSetSystemInformation pNtSetSystemInformation =
            (PNtSetSystemInformation)GetProcAddress(hNtDll, "NtSetSystemInformation");

        if (pNtSetSystemInformation) {
            int SystemMemoryListInformation = 80;
            SYSTEM_MEMORY_LIST_COMMAND command = MemoryPurgeLowPriorityStandbyList; // 우선순위 0~2 대기목록 정리

            NTSTATUS status = pNtSetSystemInformation(SystemMemoryListInformation, &command, sizeof(command));

            if (status == 0) {
                QString msg = QString("RAM: %1% (기준: %2%), Idle: %3초, CPU: %4% -> 저우선순위 Standby List 환원")
                    .arg(m_currentSysInfo.memoryLoad).arg(m_thresholdPercent).arg((int)getIdleTimeSeconds()).arg(m_currentSysInfo.cpuLoad, 0, 'f', 1);
                EventLogger::getInstance().log("Level 1", msg);
                emit logMessage("[Level 1] 백그라운드 캐시 최적화: 유휴 상태가 감지되어 저우선순위 대기 메모리를 가용 상태로 반환했습니다.");
            }
        }
    }
}

// ----------------------------------------------------------------
// 🚀 [Level 2] 적극적 디스크 플러시 (Active Optimization with Cooldown)
// ----------------------------------------------------------------
void MemoryWatchdog::checkAndExecuteLevel2() {
    if (!m_allowPagefileFlush) return;

    // 발동 조건: 사용자가 설정한 임계치 초과 && 마지막 집행 후 최소 15분 경과 (SSD 수명 자원 보호)
    if (m_currentSysInfo.memoryLoad <= m_thresholdPercent) return;

    qint64 secsSinceLastRun = m_lastLevel2Time.secsTo(QDateTime::currentDateTime());
    if (secsSinceLastRun < 900) return; // 15분 하드웨어 디바운싱 타임아웃 미달 시 차단

    // 권한 상승
    HANDLE hToken;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
        TOKEN_PRIVILEGES tp;
        if (LookupPrivilegeValue(NULL, SE_PROF_SINGLE_PROCESS_NAME, &tp.Privileges[0].Luid)) {
            tp.PrivilegeCount = 1;
            tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
            AdjustTokenPrivileges(hToken, FALSE, &tp, 0, NULL, NULL);
        }
        CloseHandle(hToken);
    }

    HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
    if (hNtDll) {
        PNtSetSystemInformation pNtSetSystemInformation =
            (PNtSetSystemInformation)GetProcAddress(hNtDll, "NtSetSystemInformation");

        if (pNtSetSystemInformation) {
            int SystemMemoryListInformation = 80;

            // 1. Modified Page를 가상 메모리 파일(Pagefile)로 강제 Flush
            SYSTEM_MEMORY_LIST_COMMAND cmdFlush = MemoryFlushModifiedList;
            pNtSetSystemInformation(SystemMemoryListInformation, &cmdFlush, sizeof(cmdFlush));

            // 2. 이어서 Standby List 청소 연쇄 처리
            SYSTEM_MEMORY_LIST_COMMAND cmdPurge = MemoryPurgeStandbyList;
            NTSTATUS status = pNtSetSystemInformation(SystemMemoryListInformation, &cmdPurge, sizeof(cmdPurge));

            if (status == 0) {
                m_lastLevel2Time = QDateTime::currentDateTime(); // 쿨다운 시간 인덱스 동기화

                QString msg = QString("RAM 고압박 상태(%1%): Modified Page 플러시 및 대기 영역 전면 청소 완료")
                    .arg(m_currentSysInfo.memoryLoad);
                EventLogger::getInstance().log("Level 2", msg);
                emit logMessage("[Level 2] 적극적 메모리 확보: 수정된 페이지를 디스크로 동기화하고 대기 목록 공간을 전면 리셋했습니다.");
            }
        }
    }
}

// ----------------------------------------------------------------
// 🚀 [Level 3] 다이내믹 워킹셋 트리밍 & 샌드박싱 (정밀 처벌 계층)
// ----------------------------------------------------------------
void MemoryWatchdog::checkAndExecuteLevel3(const QList<MonitorTypes::AnalyzedProcessInfo>& analyzedList) {
    if (analyzedList.isEmpty()) return;

    // 1. 트리밍 후 부작용 검증 파이프라인 (스래싱 피드백 시스템)
    for (auto it = m_thrashingWatchMap.begin(); it != m_thrashingWatchMap.end();) {
        DWORD targetPid = it.key();
        it.value().elapsedSeconds++;

        if (it.value().elapsedSeconds <= 4) { // 집행 후 3~5초간 폴트 모니터링
            for (const auto& pInfo : analyzedList) {
                if (pInfo.basic.pid == targetPid) {
                    ULONG currentFaults = pInfo.basic.pageFaults;
                    ULONG deltaFaults = currentFaults - it.value().lastPageFaultCount;

                    // 초당 하드 페이지 폴트가 1,000회 이상 폭증 -> '열심히 일하는 프로세스'로 판단하여 오진 철회
                    if (deltaFaults >= 1000) {
                        m_sessionWhitelist.insert(targetPid); // 세션 화이트리스트 긴급 격상
                        m_windowsWhitelist.insert(pInfo.basic.name.toLower());

                        QString alertLog = QString("오진 철회: %1 (PID: %2) - Hard Page Fault %3회 폭증으로 인한 보호 구역 격상")
                            .arg(pInfo.basic.name).arg(targetPid).arg(deltaFaults);
                        EventLogger::getInstance().log("Level 3", alertLog);
                        emit logMessage(QString("[Level 3] 오진 예방 가동: %1 프로세스의 스래싱이 감지되어 자원 제한을 긴급 철회합니다.").arg(pInfo.basic.name));
                        break;
                    }
                    it.value().lastPageFaultCount = currentFaults;
                }
            }
            ++it;
        }
        else {
            it = m_thrashingWatchMap.erase(it); // 감시 기간 종료 (정상 누수 확정)
        }
    }

    // 발동 조건: 사용자가 설정한 임계치를 초과했을 때 개입 연산 시작
    if (m_currentSysInfo.memoryLoad <= m_thresholdPercent) return;

    // 2. 위험도 스코어 내림차순 정렬
    QList<MonitorTypes::AnalyzedProcessInfo> sortedTargets = analyzedList;
    std::sort(sortedTargets.begin(), sortedTargets.end(), [](const auto& a, const auto& b) {
        return a.leakScore > b.leakScore;
        });

    // 3. 최상위 악질 타겟부터 맞춤형 정책 집행
    for (const auto& target : sortedTargets) {
        DWORD pid = target.basic.pid;
        QString procName = target.basic.name.toLower();

        // 안전장치: 화이트리스트(정적/동적) 프로세스는 철저히 우회
        if (m_globalWhitelist.contains(procName) || m_sessionWhitelist.contains(pid) || m_windowsWhitelist.contains(procName)) {
            continue;
        }

        // 최소 누수 점수 마일스톤(75점) 미달 시 루프 전면 단락
        if (target.leakScore < 75) break;

        HANDLE hProcess = OpenProcess(PROCESS_SET_QUOTA | PROCESS_SET_INFORMATION | PROCESS_TERMINATE, FALSE, pid);
        if (!hProcess) continue;

        double ramMB = (double)target.basic.privateUsage / (1024.0 * 1024.0);

        // [처방 1] Score 90점 이상 (악성 위험군) -> CPU 스케줄링 자원 등급 유배
        if (target.leakScore >= 90) {
            if (SetPriorityClass(hProcess, IDLE_PRIORITY_CLASS)) {
                QString logStr = QString("%1 (PID: %2) IDLE_PRIORITY_CLASS 유배 - Score: %3, RAM: %4MB")
                    .arg(target.basic.name).arg(pid).arg(target.leakScore).arg(ramMB, 0, 'f', 1);
                EventLogger::getInstance().log("Level 3", logStr);
            }
            emit logMessage(QString("[Level 3] 강제 자원 통제: 폭주 중인 %1 (PID:%2)의 CPU 우선순위를 최하위로 격하했습니다.").arg(target.basic.name).arg(pid));
        }
        // [처방 2] Score 75~89점 (경계군) + Commit Ratio 임계 위반 -> Job Object 샌드박싱 (성장 동결)
        else if (target.leakScore >= 75 && m_allowHardTrim) {
            double commitRatio = (target.basic.workingSet > 0) ? (double)target.basic.privateUsage / (double)target.basic.workingSet : 0;

            if (commitRatio >= 1.5) {
                HANDLE hJob = CreateJobObjectW(NULL, NULL);
                if (hJob) {
                    JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli = {};
                    jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY;
                    jeli.ProcessMemoryLimit = target.basic.privateUsage; // 현재 크기 위로 커밋 차지 팽창 원천 차단

                    if (SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli))) {
                        AssignProcessToJobObject(hJob, hProcess);

                        QString logStr = QString("%1 (PID: %2) Job Object 결속 (메모리 성장 차단) - Score: %3, RAM: %4MB")
                            .arg(target.basic.name).arg(pid).arg(target.leakScore).arg(ramMB, 0, 'f', 1);
                        EventLogger::getInstance().log("Level 3", logStr);
                        emit logMessage(QString("[Level 3] 샌드박싱 잠금: %1의 무한 메모리 팽창을 원천 동결했습니다.").arg(target.basic.name));
                    }
                }
            }
            // [처방 3] Score 75~89점 (경계군) + 일반 누수 -> EmptyWorkingSet 물리 램 원자적 회수 (Hard Trim)
            else {
                if (EmptyWorkingSet(hProcess)) {
                    QString logStr = QString("%1 (PID: %2) Trimmed - Score: %3, RAM: %4MB")
                        .arg(target.basic.name).arg(pid).arg(target.leakScore).arg(ramMB, 0, 'f', 1);
                    EventLogger::getInstance().log("Level 3", logStr);
                    emit logMessage(QString("[Level 3] 자원 회수: 누수 징후가 포착된 %1 (PID:%2)의 물리 RAM 영역을 청소했습니다.").arg(target.basic.name).arg(pid));

                    // 스래싱 검증 파이프라인에 등록하여 오진 피드백 루프 가동
                    MonitorTypes::ThrashingHistory tHistory = { target.basic.pageFaults, 0 };
                    m_thrashingWatchMap.insert(pid, tHistory);
                }
            }
        }

        CloseHandle(hProcess);
        break; // 시스템에 가해지는 충격을 방지하기 위해 한 번의 1Hz 루프 사이클당 '가장 심각한 범인 1개'씩만 순차 처리
    }
}

// ----------------------------------------------------------------
// 🔧 UI 및 슬라이더 연동 제어 세션
// ----------------------------------------------------------------
void MemoryWatchdog::setWatchdogEnabled(bool enabled) {
    m_isEnabled = enabled;
    QString status = enabled ? "활성화됨" : "비활성화됨";
    EventLogger::getInstance().log("System", "Watchdog 백그라운드 감시가 " + status);
    emit logMessage("시스템: Watchdog 백그라운드 감시가 " + status);
}

void MemoryWatchdog::setThreshold(int percent) {
    m_thresholdPercent = percent;
}

void MemoryWatchdog::setAllowStandbyPurge(bool allow) { m_allowStandbyPurge = allow; }
void MemoryWatchdog::setAllowPagefileFlush(bool allow) { m_allowPagefileFlush = allow; }
void MemoryWatchdog::setAllowHardTrim(bool allow) { m_allowHardTrim = allow; }

void MemoryWatchdog::setOptimizationMode(int mode) {
    switch (mode) {
    case ModeStandard:
        m_allowPagefileFlush = true;
        m_thresholdPercent = 85;
        emit logMessage("설정 변경: [표준 모드] 적용 (안정성 및 밸런스 지향)");
        break;
    case ModeGaming:
        m_allowPagefileFlush = false; // 게임 도중 미세 끊김 프리징 원천 방어
        m_thresholdPercent = 70;      // 조기 개입 유도
        emit logMessage("설정 변경: [반응속도 극대화 모드] 적용 (디스크 동기화 강제 차단)");
        break;
    case ModeWorkstation:
        m_allowPagefileFlush = true;
        m_thresholdPercent = 95;      // 커널 고유 동작 신뢰
        emit logMessage("설정 변경: [안정성 우선 모드] 적용 (정책 집행 개입 최소화)");
        break;
    }
}

void MemoryWatchdog::addWhitelist(const QString& processName) {
    // 입력받은 프로세스 이름을 소문자로 강제 변환하여 정적 화이트리스트 셋에 삽입
    m_globalWhitelist.insert(processName.toLower());

    // 디버그 또는 로그 창에 알림
    emit logMessage(QString("보호 목록 추가됨: %1").arg(processName.toLower()));
}

void MemoryWatchdog::removeWhitelist(const QString& processName) {
    // 정적 화이트리스트에서 해당 프로세스명 제거
    m_globalWhitelist.remove(processName.toLower());

    emit logMessage(QString("보호 목록에서 제거됨: %1").arg(processName.toLower()));
}