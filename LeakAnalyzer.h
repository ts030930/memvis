#pragma once
#include <QObject>
#include <QHash>
#include <QList>
#include <QSet> //  [추가됨] QSet 오류 해결을 위한 헤더 추가
#include <deque>
#include "SystemMonitorTypes.h"

class LeakAnalyzer : public QObject {
    Q_OBJECT
public:
    explicit LeakAnalyzer(QObject* parent = nullptr);

    double weightAlpha = 40.0;
    double weightBeta = 30.0;
    double weightGamma = 30.0;
    int maxWindowSize = 60;

public slots:
    void onProcessDataReceived(const QList<MonitorTypes::ProcessBasicInfo>& pList);

signals:
    //  [수정됨] 계산 완료 후 기본 정보와 점수가 합쳐진 리스트를 UI로 발송
    void analyzedDataReady(QList<MonitorTypes::AnalyzedProcessInfo> results);

private:
    QHash<DWORD, std::deque<MonitorTypes::MemoryDataPoint>> m_windows;
    qint64 m_tickCount = 0;

    //  [수정됨] 내부 계산용 함수 (리턴값을 점수로)
    int calculateDangerScore(DWORD pid, const std::deque<MonitorTypes::MemoryDataPoint>& window, double& outSlope, double& outRSquared);
    void cleanUpDeadProcesses(const QList<MonitorTypes::ProcessBasicInfo>& currentList);
};