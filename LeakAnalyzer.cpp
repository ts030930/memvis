#include "LeakAnalyzer.h"

LeakAnalyzer::LeakAnalyzer(QObject* parent) : QObject(parent) {}

void LeakAnalyzer::onProcessDataReceived(const QList<MonitorTypes::ProcessBasicInfo>& pList) {
    m_tickCount++;

    //  [추가됨] UI로 보낼 최종 데이터 리스트
    QList<MonitorTypes::AnalyzedProcessInfo> combinedResults;

    for (const auto& info : pList) {
        DWORD pid = info.pid;

        //  [추가됨] UI로 보낼 구조체 세팅
        MonitorTypes::AnalyzedProcessInfo analyzed;
        analyzed.basic = info; // Worker가 수집한 기본 정보 그대로 복사
        analyzed.leakScore = 0;
        analyzed.slope = 0.0;
        analyzed.rSquared = 0.0;

        MonitorTypes::MemoryDataPoint currentData = { m_tickCount, info.privateUsage, info.workingSet, info.pageFaults };
        m_windows[pid].push_back(currentData);

        if (m_windows[pid].size() > maxWindowSize) {
            m_windows[pid].pop_front();
        }

        // 윈도우에 데이터가 10초 이상 쌓였을 때 정밀 분석 가동
        if (m_windows[pid].size() >= 10) {
            analyzed.leakScore = calculateDangerScore(pid, m_windows[pid], analyzed.slope, analyzed.rSquared);
        }

        //  [추가됨] 완성된 데이터를 리스트에 추가
        combinedResults.append(analyzed);
    }

    cleanUpDeadProcesses(pList);

    //  [수정됨] UI 로 종합 결과 전송!
    emit analyzedDataReady(combinedResults);
}

int LeakAnalyzer::calculateDangerScore(DWORD pid, const std::deque<MonitorTypes::MemoryDataPoint>& window, double& outSlope, double& outRSquared) {
    double sumT = 0, sumM = 0, sumT2 = 0, sumM2 = 0, sumTM = 0;
    int N = (int)window.size();

    for (int i = 0; i < N; ++i) {
        double t = i;
        double m = (double)window[i].privateUsage / 1024.0; // KB 단위

        sumT += t;
        sumM += m;
        sumT2 += t * t;
        sumM2 += m * m;
        sumTM += t * m;
    }

    double denominator = (N * sumT2) - (sumT * sumT);
    if (denominator == 0) return 0;

    double slope = ((N * sumTM) - (sumT * sumM)) / denominator;
    outSlope = slope; //  참조로 값 넘겨주기

    if (slope <= 0) {
        outRSquared = 0.0;
        return 0;
    }

    double numeratorR2 = ((N * sumTM) - (sumT * sumM));
    numeratorR2 *= numeratorR2;
    double denominatorR2 = denominator * ((N * sumM2) - (sumM * sumM));

    double rSquared = (denominatorR2 == 0) ? 0 : numeratorR2 / denominatorR2;
    outRSquared = rSquared; //  참조로 값 넘겨주기

    double score = 0;

    // [가중치 A] 선형성 점수
    if (rSquared > 0.7) {
        score += weightAlpha * rSquared;
    }

    // [가중치 B] Commit Ratio 점수 
    const auto& lastData = window.back();
    if (lastData.workingSet > 0) {
        double commitRatio = (double)lastData.privateUsage / (double)lastData.workingSet;
        if (commitRatio >= 1.5) {
            score += std::min(weightBeta, (commitRatio - 1.5) * 10.0);
        }
    }

    // [가중치 C] Page Fault Rate 점수 
    ULONG deltaPageFaults = window.back().pageFaults - window.front().pageFaults;
    double pageFaultsPerSec = (double)deltaPageFaults / N;

    if (pageFaultsPerSec < 5.0) {
        score += weightGamma;
    }

    return std::min(100, (int)score);
}

void LeakAnalyzer::cleanUpDeadProcesses(const QList<MonitorTypes::ProcessBasicInfo>& currentList) {
    QSet<DWORD> currentPids;
    for (const auto& info : currentList) currentPids.insert(info.pid);

    QList<DWORD> trackedPids = m_windows.keys();
    for (DWORD pid : trackedPids) {
        if (!currentPids.contains(pid)) {
            m_windows.remove(pid);
        }
    }
}