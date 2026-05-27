#pragma once
#include <QObject>
#include <QDateTime>
#include <QQueue>
#include <QMap>
#include <QSet>
#include "SystemMonitorTypes.h"

class MemoryWatchdog : public QObject {
    Q_OBJECT
public:
    explicit MemoryWatchdog(QObject* parent = nullptr);

    enum Mode {
        ModeStandard = 0,
        ModeGaming = 1,
        ModeWorkstation = 2
    };

public slots:
    // UI에서 변경한 옵션을 Watchdog에 전달받는 함수들
    void setWatchdogEnabled(bool enabled);
    void setOptimizationMode(int mode);
    void setAllowStandbyPurge(bool allow);
    void setAllowPagefileFlush(bool allow);
    void setAllowHardTrim(bool allow);
    void setThreshold(int percent);

    // Worker와 Analyzer로부터 매초마다 시스템/프로세스 상태를 전달받는 함수들
    void onSystemMemoryUpdated(const MonitorTypes::SystemMemoryInfo& sysInfo);
    void onAnalyzedDataReady(const QList<MonitorTypes::AnalyzedProcessInfo>& analyzedList);

signals:
    // UI 로그 창(textEditLog)으로 메세지를 쏘는 시그널
    void logMessage(const QString& message);

private:
    // ----------------------------------------------------
    // 1. 핵심 판단 및 커널 API 실행 함수 (Phase 3에서 구현할 진짜 로직)
    // ----------------------------------------------------
    void checkAndExecuteLevel1();
    void checkAndExecuteLevel2();
    void checkAndExecuteLevel3(const QList<MonitorTypes::AnalyzedProcessInfo>& analyzedList);
    void checkAndExecuteLevel4();

    // 사용자 입력 상태(마우스/키보드 유휴 시간) 확인 함수
    double getIdleTimeSeconds();

    // ----------------------------------------------------
    // 2. UI 설정 상태 저장 변수들
    // ----------------------------------------------------
    bool m_isEnabled = false;
    bool m_allowStandbyPurge = true;
    bool m_allowPagefileFlush = false;
    bool m_allowHardTrim = true;
    int m_thresholdPercent = 85;

    // ----------------------------------------------------
    // 3. 시스템 상태 감시 및 쿨다운 변수들
    // ----------------------------------------------------
    MonitorTypes::SystemMemoryInfo m_currentSysInfo;
    QDateTime m_lastLevel2Time; // Level 2 (디스크 플러시) 15분 쿨다운 체크용

    // ----------------------------------------------------
    // 4. 스래싱(Thrashing) 방어 및 화이트리스트
    // ----------------------------------------------------
    QMap<DWORD, QQueue<ULONG>> m_thrashingMonitor; // 각 PID별 페이지 폴트 폭증 추적
    QSet<DWORD> m_whitelistPids;                   // 최적화 제외 대상 모음
};