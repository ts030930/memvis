#include "MemoryWorker.h"
#include <windows.h> // API 호출용
#include <psapi.h>    // GetPerformanceInfo 사용을 위해 필수
#include <QDebug>
#include <tlhelp32.h>
#include <QFileInfo>
#include "SystemMonitorTypes.h"

MemoryWorker::MemoryWorker(QObject* parent) : QObject(parent) {
    // 생성자에서는 아무것도 하지 않는 것이 좋습니다.
    // (이 시점에는 아직 메인 스레드에 속해 있기 때문입니다)
}

MemoryWorker::~MemoryWorker() {
    if (m_timer) {
        m_timer->stop();
        delete m_timer;
    }
}

void MemoryWorker::startWork() {
    m_timer = new QTimer(this);

    //  개별 함수들을 연결하지 않고, onTick 하나만 연결합니다.
    connect(m_timer, &QTimer::timeout, this, &MemoryWorker::onTick);

    m_timer->start(1000);
}

//  1초마다 실행되는 "진짜 작업장"
void MemoryWorker::onTick() {
    // 1. 기본 시스템 데이터 수집
    fetchSystemData();

    // 2. 전체 프로세스 목록 수집
    fetchProcessData();

    // 3. 🎯 집중 분석 대상 PID가 있다면 딥스캔 실행!
    if (m_targetPID != 0) {
        fetchProcessDetail(m_targetPID);
    }
}

void MemoryWorker::stopWork() {
    if (m_timer) m_timer->stop();
}

void MemoryWorker::setTargetProcess(unsigned long pid) {
    m_targetPID = pid;
}

void MemoryWorker::fetchSystemData() {
    MonitorTypes::SystemMemoryInfo sysInfo = {};

    MEMORYSTATUSEX memStat;
    memStat.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memStat)) {
        sysInfo.totalPhys = memStat.ullTotalPhys;
        sysInfo.availPhys = memStat.ullAvailPhys;
        sysInfo.memoryLoad = memStat.dwMemoryLoad;
    }

    PERFORMANCE_INFORMATION perfInfo;
    perfInfo.cb = sizeof(PERFORMANCE_INFORMATION);
    if (GetPerformanceInfo(&perfInfo, sizeof(perfInfo))) {
        SIZE_T pageSize = perfInfo.PageSize;
        sysInfo.commitLimit = perfInfo.CommitLimit * pageSize;
        sysInfo.commitCharge = perfInfo.CommitTotal * pageSize;
        sysInfo.pagedPool = perfInfo.KernelPaged * pageSize;
        sysInfo.nonPagedPool = perfInfo.KernelNonpaged * pageSize;
        sysInfo.systemCache = perfInfo.SystemCache * pageSize;
        sysInfo.cpuLoad = calculateCpuLoad();
    }

    emit systemMemoryUpdated(sysInfo);
}


void MemoryWorker::fetchProcessData() {
    // 1. ntdll에서 함수 로드 (최초 1회만 수행하도록 구현 권장)
    if (!NtQuerySystemInformation) {
        NtQuerySystemInformation = (PNtQuerySystemInformation)GetProcAddress(
            GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation");
    }

    ULONG bufferSize = 0x10000; // 초기 버퍼 64KB
    PVOID buffer = malloc(bufferSize);

    // 2. 전체 프로세스 정보 한 방에 긁어오기
    // 부족하면 버퍼 크기를 늘려가며 반복 호출
    while (NtQuerySystemInformation(SystemProcessInformation, buffer, bufferSize, &bufferSize) == 0xC0000004) {
        buffer = realloc(buffer, bufferSize);
    }

    QList<MonitorTypes::ProcessBasicInfo> pList;
    MonitorTypes::SYSTEM_PROCESS_INFORMATION* pInfo = (MonitorTypes::SYSTEM_PROCESS_INFORMATION*)buffer;

    //  [가비지 컬렉션용] 현재 살아있는 PID 목록을 추적하기 위한 Set
    QSet<DWORD> currentPids;

    while (true) {
        DWORD pid = (DWORD)(DWORD_PTR)pInfo->UniqueProcessId;

        //  [방어 로직] PID 0 (System Idle Process) 예외 처리
        // 0번 프로세스는 경로도 없고 이름도 없어서 오류를 뿜을 수 있으므로 패스합니다.
        if (pid == 0) {
            if (pInfo->NextEntryOffset == 0) break;
            pInfo = (MonitorTypes::SYSTEM_PROCESS_INFORMATION*)((BYTE*)pInfo + pInfo->NextEntryOffset);
            continue;
        }

        currentPids.insert(pid); // 살아있는 PID 기록

        //  [핵심 수정] 끝에 '{}'를 붙여서 구조체를 0으로 싹 초기화! (14TB 쓰레기값 방지)
        MonitorTypes::ProcessBasicInfo info = {};

        info.pid = pid;

        // 커널이나 시스템 프로세스는 이름이 Null일 수 있으므로 예외 처리
        if (pInfo->ImageName.Buffer) {
            info.name = QString::fromWCharArray(pInfo->ImageName.Buffer, pInfo->ImageName.Length / sizeof(WCHAR));
        }
        else {
            info.name = "System";
        }

        info.threads = pInfo->NumberOfThreads;
        info.handles = pInfo->HandleCount;

        info.workingSet = pInfo->WorkingSetSize;
        info.peakWorkingSet = pInfo->PeakWorkingSetSize;

        //  [추가됨] 누락되었던 핵심 데이터들 매핑
        info.pageFaults = pInfo->PageFaultCount;
        info.privateUsage = pInfo->PrivatePageCount;

        //  [해싱/캐싱 로직 적용]
        // 캐시에 경로가 있으면 그대로 쓰고, 없으면 딱 한 번만 OpenProcess 호출
        if (!m_pathCache.contains(pid)) {
            updatePathCache(pid);
        }
        info.path = m_pathCache.value(pid, ""); // 못 찾았을 경우 빈 문자열 반환

        pList.append(info);

        if (pInfo->NextEntryOffset == 0) break;
        pInfo = (MonitorTypes::SYSTEM_PROCESS_INFORMATION*)((BYTE*)pInfo + pInfo->NextEntryOffset);
    }

    free(buffer);

    // [메모리 누수 방지] 죽은 프로세스 캐시 정리 (Garbage Collection)
    // 캐시에 있는 PID 중, 이번에 수집된 currentPids에 없는 녀석은 프로그램이 종료된 것이므로 지웁니다.
    QList<DWORD> cachedPids = m_pathCache.keys();
    for (DWORD cachedPid : cachedPids) {
        if (!currentPids.contains(cachedPid)) {
            m_pathCache.remove(cachedPid);
        }
    }

    emit processListUpdated(pList);
}

void MemoryWorker::fetchProcessDetail(DWORD pid) {
    MonitorTypes::ProcessDetailInfo detail;
    detail.pid = pid;

    // 1. 프로세스 핸들 오픈 (모든 권한 요청)
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!hProcess) return;

    // 2. 기본 정보 수집 (이름, 경로, 핸들 수)
    wchar_t path[MAX_PATH];
    DWORD size = MAX_PATH;
    if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) detail.path = QString::fromWCharArray(path);
    detail.name = QFileInfo(detail.path).fileName();

    DWORD handleCount = 0;
    GetProcessHandleCount(hProcess, &handleCount);
    detail.handles = handleCount;

    // 3. 아키텍처 판별 (64비트 vs 32비트)
    BOOL isWow64 = FALSE;
    IsWow64Process(hProcess, &isWow64);
    detail.architecture = isWow64 ? "32-bit (WoW64)" : "64-bit (Native)";

    // 4. 가상 메모리 카운터 수집
    PROCESS_MEMORY_COUNTERS_EX pmc;
    if (GetProcessMemoryInfo(hProcess, (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        detail.privateUsage = pmc.PrivateUsage;
        detail.workingSet = pmc.WorkingSetSize;
        detail.peakWorkingSet = pmc.PeakWorkingSetSize;
        detail.pageFaults = pmc.PageFaultCount;
    }

    // 5.  [핵심] VAD 스캔 (메모리 구조 해부)
    MEMORY_BASIC_INFORMATION mbi;
    unsigned char* addr = nullptr;

    detail.memPrivate = 0;
    detail.memImage = 0;
    detail.memMapped = 0;

    // 0번 주소부터 가상 메모리 끝까지 점프하며 확인
    while (VirtualQueryEx(hProcess, addr, &mbi, sizeof(mbi))) {
        if (mbi.State == MEM_COMMIT) { // 할당된 메모리만 계산
            switch (mbi.Type) {
            case MEM_PRIVATE: detail.memPrivate += mbi.RegionSize; break; // Heap, Stack
            case MEM_IMAGE:   detail.memImage += mbi.RegionSize;   break; // EXE, DLL
            case MEM_MAPPED:  detail.memMapped += mbi.RegionSize;  break; // Shared Memory
            }
        }
        addr += mbi.RegionSize;
        if (addr >= (unsigned char*)0x7fffffffffff) break; // 사용자 영역 끝방지
    }

    CloseHandle(hProcess);
    emit processDetailUpdated(detail); // UI로 결과 쏩니다!
}

void MemoryWorker::updatePathCache(DWORD pid) {
    // 새로운 프로세스일 때만 수행되므로 전체 부하가 극적으로 감소함
    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (hProcess) {
        wchar_t path[MAX_PATH];
        DWORD size = MAX_PATH;
        if (QueryFullProcessImageNameW(hProcess, 0, path, &size)) {
            m_pathCache.insert(pid, QString::fromWCharArray(path));
        }
        CloseHandle(hProcess);
    }
}

//  [추가됨] FILETIME을 64비트 정수로 변환하는 헬퍼 함수 (cpp 파일 상단에 추가)
inline ULONGLONG FileTimeToULL(const FILETIME& ft) {
    ULARGE_INTEGER uli;
    uli.LowPart = ft.dwLowDateTime;
    uli.HighPart = ft.dwHighDateTime;
    return uli.QuadPart;
}

//  [추가됨] CPU 로드율 계산 함수 구현
double MemoryWorker::calculateCpuLoad() {
    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) return 0.0;

    if (m_firstCpuCheck) {
        m_prevIdleTime = idleTime;
        m_prevKernelTime = kernelTime;
        m_prevUserTime = userTime;
        m_firstCpuCheck = false;
        return 0.0; // 첫 틱은 비교할 과거 데이터가 없으므로 0 반환
    }

    ULONGLONG idleDiff = FileTimeToULL(idleTime) - FileTimeToULL(m_prevIdleTime);
    ULONGLONG kernelDiff = FileTimeToULL(kernelTime) - FileTimeToULL(m_prevKernelTime);
    ULONGLONG userDiff = FileTimeToULL(userTime) - FileTimeToULL(m_prevUserTime);

    ULONGLONG sysDiff = kernelDiff + userDiff;
    double cpuLoad = 0.0;

    if (sysDiff > 0) {
        cpuLoad = (double)(sysDiff - idleDiff) / sysDiff * 100.0;
    }

    // 다음 틱을 위해 현재 시간 저장
    m_prevIdleTime = idleTime;
    m_prevKernelTime = kernelTime;
    m_prevUserTime = userTime;

    return cpuLoad;
}