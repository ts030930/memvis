#pragma once

#include <QObject>
#include <QTimer>
#include "SystemMonitorTypes.h" 
#include <QHash>    //  QHash를 사용하기 위해 반드시 추가!
#include <QString>
#include "SystemMonitorTypes.h"
class MemoryWorker : public QObject {
    Q_OBJECT

public:
    explicit MemoryWorker(QObject* parent = nullptr);
    ~MemoryWorker();
    void fetchProcessDetail(DWORD pid); //  함수 선언 추가
    void setTargetPID(DWORD pid) { m_targetPID = pid; }
signals:
    // UI 스레드로 수집된 데이터를 전달하는 신호들
    void systemMemoryUpdated(MonitorTypes::SystemMemoryInfo info);
    void processListUpdated(QList<MonitorTypes::ProcessBasicInfo> list);
    void processDetailUpdated(MonitorTypes::ProcessDetailInfo detail);
    // 권한 에러나 API 실패 시 UI에 알림
    void errorOccurred(QString errorMessage);

public slots:
    // 스레드가 시작될 때 호출될 초기화 및 수집 시작 함수
    void startWork();
    // 모니터링 일시정지/종료
    void stopWork();
    // 사용자가 클릭한 집중 모니터링 프로세스 PID 설정
    void setTargetProcess(unsigned long pid);

private slots:
    // QTimer에 의해 주기적으로 실행될 실제 API 호출 함수들
    double calculateCpuLoad();
    void fetchSystemData();
    void fetchProcessData();
    void onTick();
  
private:
    QTimer* m_timer = nullptr;
    DWORD m_targetPID = 0; // 집중 모니터링 대상
    void updatePathCache(DWORD pid); // 경로 미발견 시 캐시 갱신 함수

    // Native API 함수 포인터
    typedef NTSTATUS(WINAPI* PNtQuerySystemInformation)(
        SYSTEM_INFORMATION_CLASS, PVOID, ULONG, PULONG);
    PNtQuerySystemInformation NtQuerySystemInformation;

    FILETIME m_prevIdleTime = { 0, 0 };
    FILETIME m_prevKernelTime = { 0, 0 };
    FILETIME m_prevUserTime = { 0, 0 };
    bool m_firstCpuCheck = true;

    QHash<DWORD, QString> m_pathCache;
};