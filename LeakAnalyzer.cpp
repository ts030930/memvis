#include "LeakAnalyzer.h"

LeakAnalyzer::LeakAnalyzer(QObject* parent) : QObject(parent) {}

void LeakAnalyzer::onProcessDataReceived(const QList<MonitorTypes::ProcessBasicInfo>& pList) {
    m_tickCount++;
    QList<MonitorTypes::AnalyzedProcessInfo> combinedResults;

    for (const auto& info : pList) {
        DWORD pid = info.pid;
        qulonglong currentUsage = info.privateUsage;

        // 1. [Stage 2] Phase 1 경량 필터링 상태 업데이트
        auto& shortWindow = m_shortHistory[pid];
        bool isSpike = false;
        if (!shortWindow.empty()) {
            qulonglong prevUsage = shortWindow.back();
            if (currentUsage > prevUsage && (currentUsage - prevUsage) >= m_spikeThreshold) {
                isSpike = true;
            }
        }
        shortWindow.push_back(currentUsage);
        if (shortWindow.size() > m_shortWindowSize) shortWindow.pop_front();

        bool isContinuousIncrease = false;
        if (shortWindow.size() == m_shortWindowSize) {
            if (shortWindow[2] > shortWindow[1] && shortWindow[1] > shortWindow[0]) {
                isContinuousIncrease = true;
            }
        }
        bool passFilter = isContinuousIncrease || isSpike;

        // 2. 🌟 상태 관리 제어 트리거 평가
        bool isAlreadyTracked = m_windows.contains(pid);

        // [핵심 교정] 신규 진입 조건이 충족되었거나, 이미 심층 추적 중인 용의자라면 파이프라인 유지
        if (passFilter || isAlreadyTracked) {
            MonitorTypes::MemoryDataPoint currentData = {
                m_tickCount, info.privateUsage, info.workingSet, info.pageFaults
            };
            m_windows[pid].push_back(currentData);

            if (m_windows[pid].size() > maxWindowSize) {
                m_windows[pid].pop_front();
            }

            // UI 송신 구조체 셋업
            MonitorTypes::AnalyzedProcessInfo analyzed;
            analyzed.basic = info;
            analyzed.leakScore = 0;
            analyzed.slope = 0.0;
            analyzed.rSquared = 0.0;

            // 시계열 표본이 충분히 쌓였을 때 정밀 분석 엔진 가동
            if (m_windows[pid].size() >= 10) {
                analyzed.leakScore = calculateDangerScore(pid, m_windows[pid], analyzed.slope, analyzed.rSquared);

                // 🛑 [v2.0 설계안 반영] 강등(Demote) 조건 검사
                // 기울기가 0 이하라는 것은 메모리 증가세가 멈추었거나 안정화되었음을 의미
                if (analyzed.slope <= 0) {
                    m_demoteCounters[pid]++;

                    // 10초 연속으로 안정 상태가 유지되면 비로소 용의자 선상에서 완전히 제외(강등)
                    if (m_demoteCounters[pid] >= 10) {
                        m_windows.remove(pid);
                        m_demoteCounters.remove(pid);
                        analyzed.leakScore = 0; // 강등되었으므로 위험도 초기화
                    }
                }
                else {
                    // 단 1초라도 다시 증가하면 안정화 카운터를 초기화하여 완벽 방어
                    m_demoteCounters[pid] = 0;
                }
            }

            combinedResults.append(analyzed);
        }
        else {
            // 경량 필터도 통과 못 하고, 기존에 추적 중도 아니던 대다수의 깨끗한 프로세스들
            // 무거운 수학 연산 단 1줄도 타지 않고 즉시 0점 처리 후 파이프라인 패스 (최적화 극대화)
            MonitorTypes::AnalyzedProcessInfo analyzed;
            analyzed.basic = info;
            analyzed.leakScore = 0;
            analyzed.slope = 0.0;
            analyzed.rSquared = 0.0;

            combinedResults.append(analyzed);
        }
    }

    cleanUpDeadProcesses(pList);
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
    outSlope = slope; // 참조를 통한 외부 변수 업데이트

    if (slope <= 0) {
        outRSquared = 0.0;
        return 0;
    }

    double numeratorR2 = ((N * sumTM) - (sumT * sumM));
    numeratorR2 *= numeratorR2;
    double denominatorR2 = denominator * ((N * sumM2) - (sumM * sumM));

    double rSquared = (denominatorR2 == 0) ? 0 : numeratorR2 / denominatorR2;
    outRSquared = rSquared; // 참조를 통한 외부 변수 업데이트

    double score = 0;

    // ---------------------------------------------------------
    // 🎯 [새로 적용해야 할 개선된 점수 산정 알고리즘]
    // ---------------------------------------------------------

    // 1. 선형성 점수 (최대 30점): 얼마나 꾸준히 우상향하는가?
    if (rSquared > 0.5) {
        score += 30.0 * rSquared;
    }

    // 2. 누수 속도 점수 (최대 40점): 얼마나 빠르게 증가하는가?
    // 기준: 초당 1MB(1024 KB) 누수 시 10점, 4MB 이상 누수 시 만점(40점)
    double slopeMB = slope / 1024.0; // 초당 MB 증가량
    double slopeScore = 0.0;

    if (slopeMB >= 1.0) {
        slopeScore = 40.0; // 1MB/s 이상은 즉시 만점 (치명적)
    }
    else if (slopeMB > 0.0) {
        // 0 ~ 1.0 MB/s 구간을 0 ~ 40점에 비례 분배 (예: 0.5MB/s -> 20점)
        slopeScore = slopeMB * 40.0;
    }

    score += slopeScore;

    // 3. 은닉성 및 상태 점수 (최대 30점):
    // 3-1. 조용한 누수인가? (은닉성 +10점)
    ULONG deltaPageFaults = window.back().pageFaults - window.front().pageFaults;
    double pageFaultsPerSec = (double)deltaPageFaults / N;
    if (pageFaultsPerSec < 50.0) {
        score += 10.0;
    }

    // 3-2. 사용하지도 않을 메모리를 쥐고만 있는가? (Commit Ratio +10점)
    const auto& lastData = window.back();
    if (lastData.workingSet > 0) {
        double commitRatio = (double)lastData.privateUsage / (double)lastData.workingSet;
        if (commitRatio >= 1.5) {
            score += 10.0;
        }
    }

    // 🚨 4. 치명적 폭주 가산점 (명백한 타겟팅)
    // 더미 프로그램처럼 초당 5MB(5120 KB) 이상 폭증하는 프로그램은 무조건 +20점 가산
    if (slope >= 5120.0) {
        score += 20.0;
    }

    return std::min(100, (int)score);
}
void LeakAnalyzer::cleanUpDeadProcesses(const QList<MonitorTypes::ProcessBasicInfo>& currentList) {
    QSet<DWORD> currentPids;
    for (const auto& info : currentList) {
        currentPids.insert(info.pid);
    }

    // 1. 심층 슬라이딩 윈도우 해시 맵 청소
    QList<DWORD> trackedPids = m_windows.keys();
    for (DWORD pid : trackedPids) {
        if (!currentPids.contains(pid)) {
            m_windows.remove(pid);
        }
    }

    // 2. 🧹 [추가] 경량 히스토리 차단막 캐시도 함께 청소하여 누수 방지
    QList<DWORD> shortHistoryPids = m_shortHistory.keys();
    for (DWORD pid : shortHistoryPids) {
        if (!currentPids.contains(pid)) {
            m_shortHistory.remove(pid);
        }
    }
}