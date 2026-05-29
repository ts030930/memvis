#pragma once
#include <QString>
#include <QList>
#include <QMetaType>
#include <windows.h>
#include <winternl.h>
namespace MonitorTypes {
    // 시스템 전체 메모리 정보
    struct SystemMemoryInfo {
        qulonglong totalPhys;
        qulonglong availPhys;
        DWORD memoryLoad;
        qulonglong commitLimit;
        qulonglong commitCharge;
        qulonglong pagedPool;
        qulonglong nonPagedPool;
        qulonglong systemCache;
        double cpuLoad;
    };

    // 프로세스 기본 정보 (Worker에서 수집)
    struct ProcessBasicInfo {
        DWORD pid;
        QString name;
        QString path;
        DWORD threads;
        DWORD handles;
        SIZE_T workingSet;
        SIZE_T peakWorkingSet;
        SIZE_T privateUsage;
        ULONG pageFaults;
    };

    // 프로세스 상세 정보 (VAD 스캔용)
    struct ProcessDetailInfo {
        DWORD pid;
        QString name;
        QString path;
        DWORD threads;
        DWORD handles;
        QString architecture;
        SIZE_T privateUsage;
        SIZE_T workingSet;
        SIZE_T peakWorkingSet;
        ULONG pageFaults;
        SIZE_T memPrivate;
        SIZE_T memImage;
        SIZE_T memMapped;
    };

    //  [추가됨] 윈도우 큐에 쌓일 1초 단위 과거 데이터
    struct MemoryDataPoint {
        qint64 timeIndex;
        SIZE_T privateUsage;
        SIZE_T workingSet;
        ULONG pageFaults;
    };

    //  [추가됨] LeakAnalyzer가 계산을 마치고 UI로 보낼 최종 "종합 세트"
    struct AnalyzedProcessInfo {
        ProcessBasicInfo basic; // 기존 프로세스 정보
        int leakScore;          // 0~100 누수 위험도 점수
        double slope;           // 증가 기울기
        double rSquared;        // 선형 신뢰도
    };
    struct ThrashingHistory {
        ULONG lastPageFaultCount; // 직전 틱의 누적 페이지 폴트 수
        int elapsedSeconds;       // 트리밍 집행 후 경과된 시간 (초)
    };
    // (기존) ntdll.dll 에서 사용할 내부 구조체
    typedef struct _SYSTEM_PROCESS_INFORMATION {
        ULONG NextEntryOffset;
        ULONG NumberOfThreads;
        LARGE_INTEGER Reserved[3];
        LARGE_INTEGER CreateTime;
        LARGE_INTEGER UserTime;
        LARGE_INTEGER KernelTime;
        UNICODE_STRING ImageName;
        KPRIORITY BasePriority;
        HANDLE UniqueProcessId;
        HANDLE InheritedFromUniqueProcessId;
        ULONG HandleCount;
        ULONG SessionId;
        ULONG_PTR PageDirectoryBase;
        SIZE_T PeakVirtualSize;
        SIZE_T VirtualSize;
        ULONG PageFaultCount;
        SIZE_T PeakWorkingSetSize;
        SIZE_T WorkingSetSize;
        SIZE_T QuotaPeakPagedPoolUsage;
        SIZE_T QuotaPagedPoolUsage;
        SIZE_T QuotaPeakNonPagedPoolUsage;
        SIZE_T QuotaNonPagedPoolUsage;
        SIZE_T PagefileUsage;
        SIZE_T PeakPagefileUsage;
        SIZE_T PrivatePageCount;
    } SYSTEM_PROCESS_INFORMATION;
}

// 메타 타입 등록 (Signal/Slot 통신용)
Q_DECLARE_METATYPE(MonitorTypes::SystemMemoryInfo)
Q_DECLARE_METATYPE(MonitorTypes::ProcessBasicInfo)
Q_DECLARE_METATYPE(QList<MonitorTypes::ProcessBasicInfo>)
Q_DECLARE_METATYPE(MonitorTypes::ProcessDetailInfo)

//  [추가됨] 새로운 종합 세트 메타 타입 등록
Q_DECLARE_METATYPE(MonitorTypes::AnalyzedProcessInfo)
Q_DECLARE_METATYPE(QList<MonitorTypes::AnalyzedProcessInfo>)