#pragma once

#include <QtWidgets/QMainWindow>
#include "ui_memvis.h"
#include <QThread>
#include <QtCharts/QChartView>
#include <QtCharts/QSplineSeries>
#include <QtCharts/QChart>
#include <QHash>
#include <QLabel>
#include "SystemMonitorTypes.h"
#include "ProcessTableModel.h" // 
#include "MemoryWatchdog.h" // 
class MemoryWorker;
class LeakAnalyzer;
class MemoryWatchdog;
class MemVis : public QMainWindow
{
    Q_OBJECT

public:
    MemVis(QWidget* parent = nullptr);
    ~MemVis();

public slots:
    void updateSystemUI(MonitorTypes::SystemMemoryInfo info);
    void updateProcessTable(QList<MonitorTypes::AnalyzedProcessInfo> list);
    void updateDetailUI(MonitorTypes::ProcessDetailInfo detail);

private slots:
    void showContextMenu(const QPoint& pos);
    void startDeepAnalysis(DWORD targetPID, QString targetName);
    void killSelectedProcess(DWORD targetPID, QString targetName);

private:
    Ui::MemVisClass ui;
    
    ProcessTableModel* m_processModel;

    QThread* m_monitorThread;
    MemoryWorker* m_workerPointer;
    LeakAnalyzer* m_analyzer;
    MemoryWatchdog* m_watchdog = nullptr;
    QChart* m_usageChart;
    QSplineSeries* m_loadSeries;
    QSplineSeries* m_availSeries;

    QChart* m_commitChart;
    QSplineSeries* m_commitSeries;

    int m_timeTick = 0;
    DWORD m_selectedPID = 0;
    QHash<QString, QLabel*> m_detailLabels;
};