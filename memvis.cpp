#include "memvis.h"
#include "MemoryWorker.h"
#include "LeakAnalyzer.h" 
#include "MemoryWatchdog.h" 
#include "EventLogger.h"
#include <QThread>
#include <QtCharts/QValueAxis>
#include <QMenu>
#include <QMessageBox>
#include <windows.h>
#include <QDialog>
#include <QFormLayout>
#include <QLabel>
#include <QGroupBox>
#include <QVBoxLayout>
#include <QGraphicsLayout>
#include <QButtonGroup> 
#include <QDateTime>
#include <QInputDialog>
#include <QApplication>
#include <QStyle>
#include <QDialogButtonBox>
#include <QSet>
#include <QFileIconProvider>
#include <QFileInfo>

LeakAnalyzer* m_analyzer;
MemVis::MemVis(QWidget* parent)
    : QMainWindow(parent)
{
    ui.setupUi(this);

    this->setStyleSheet(
        "QWidget { background-color: #23272E; color: #ABB2BF; font-family: 'Segoe UI', 'Malgun Gothic'; }"
        "QFrame#frame { background-color: #2C313C; border-radius: 12px; border: 1px solid #3E4451; }"
        "QLabel { font-size: 13px; font-weight: 600; color: #61AFEF; }"
        "QListWidget { background-color: #2C313C; border: none; border-radius: 8px; padding: 5px; }"
        "QListWidget::item { padding: 12px; border-radius: 6px; margin-bottom: 2px; }"
        "QListWidget::item:hover { background-color: #3E4451; }"
        "QListWidget::item:selected { background-color: #61AFEF; color: #23272E; font-weight: bold; }"
        "QHeaderView::section { background-color: #2C313C; color: #61AFEF; border: 1px solid #3E4451; font-weight: bold; }"
        "QTableView { background-color: #282C34; gridline-color: #3E4451; selection-background-color: #3E4451; border: none; }"
    );

    auto setupHelp = [](QToolButton* btn, QString text) {
        btn->setText("?");
        btn->setFixedSize(18, 18);
        btn->setToolTip(text);
        btn->setStyleSheet("QToolButton { border-radius: 9px; background: #333; color: #bbb; border: 1px solid #555; font-size: 10px; } "
            "QToolButton:hover { background: #555; color: white; }");
        };

    setupHelp(ui.btnHelpAvail, "<b>가용 메모리 (Available):</b><br>시스템이 새로운 프로그램에 즉시 할당해 줄 수 있는 실제 RAM 용량입니다.");
    setupHelp(ui.btnHelpLoad, "<b>메모리 로드율 (Load):</b><br>전체 물리 RAM 중에서 현재 사용 중인 메모리의 백분율입니다.");
    setupHelp(ui.btnHelpPaged, "<b>페이지됨 (Paged Pool):</b><br>하드디스크(페이지 파일)로 옮겨질 수 있는 영역입니다.");
    setupHelp(ui.btnHelpNonPaged, "<b>비페이지됨 (Non-paged Pool):</b><br>항상 RAM에 상주해야 하는 커널 영역입니다.");
    setupHelp(ui.btnHelpCache, "<b>캐시 (System Cache):</b><br>디스크 성능을 높이기 위해 최근에 읽거나 쓴 데이터를 임시로 보관하는 곳입니다.");
    setupHelp(ui.btnHelpCommit, "<b>커밋 차지 (Commit Charge):</b><br>프로세스들에게 할당해 주기로 약속한 전체 메모리 양입니다.");

    qRegisterMetaType<MonitorTypes::SystemMemoryInfo>("MonitorTypes::SystemMemoryInfo");
    qRegisterMetaType<QList<MonitorTypes::ProcessBasicInfo>>("QList<MonitorTypes::ProcessBasicInfo>");
    qRegisterMetaType<MonitorTypes::ProcessDetailInfo>("MonitorTypes::ProcessDetailInfo");
    //  [추가됨] 새로운 타입 등록
    qRegisterMetaType<MonitorTypes::AnalyzedProcessInfo>("MonitorTypes::AnalyzedProcessInfo");
    qRegisterMetaType<QList<MonitorTypes::AnalyzedProcessInfo>>("QList<MonitorTypes::AnalyzedProcessInfo>");

    ui.listMenu->clear();
    ui.listMenu->addItem("대시보드");
    ui.listMenu->addItem("프로세스 관리");
    ui.listMenu->addItem("메모리 최적화");
    ui.listMenu->addItem("설정");

    connect(ui.listMenu, &QListWidget::currentRowChanged, ui.stackedPages, &QStackedWidget::setCurrentIndex);
    ui.listMenu->setCurrentRow(0);
    ui.stackedPages->setCurrentIndex(0);

    //  [수정됨] 7열로 확장하고 '누수 위험도' 헤더 추가
    m_processModel = new ProcessTableModel(this);
    ui.tableViewProcess->setModel(m_processModel);
    ui.tableViewProcess->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    ui.tableViewProcess->setSortingEnabled(true);

    ui.tableViewProcess->setContextMenuPolicy(Qt::CustomContextMenu);
    ui.tableViewProcess->setSelectionBehavior(QAbstractItemView::SelectRows);
    connect(ui.tableViewProcess, &QTableView::customContextMenuRequested, this, &MemVis::showContextMenu);

    // [그래프 초기화 코드 생략없이 유지]
    QString chartBgColor = "#2C313C";
    QString axisColor = "#ABB2BF";

    m_usageChart = new QChart();
    m_usageChart->setBackgroundVisible(false);
    m_usageChart->layout()->setContentsMargins(0, 0, 0, 0);

    m_loadSeries = new QSplineSeries();
    m_loadSeries->setName("로드율 (%)");
    m_loadSeries->setPen(QPen(QColor("#E06C75"), 2.5));

    m_availSeries = new QSplineSeries();
    m_availSeries->setName("가용량 (%)");
    m_availSeries->setPen(QPen(QColor("#98C379"), 2.5));

    m_usageChart->addSeries(m_loadSeries);
    m_usageChart->addSeries(m_availSeries);
    m_usageChart->createDefaultAxes();

    auto usageAxisX = qobject_cast<QValueAxis*>(m_usageChart->axes(Qt::Horizontal).first());
    auto usageAxisY = qobject_cast<QValueAxis*>(m_usageChart->axes(Qt::Vertical).first());
    if (usageAxisX) {
        usageAxisX->setRange(0, 60);
        usageAxisX->setLabelsColor(axisColor);
        usageAxisX->setGridLineColor("#3E4451");
    }
    if (usageAxisY) {
        usageAxisY->setRange(0, 100);
        usageAxisY->setLabelsColor(axisColor);
        usageAxisY->setGridLineColor("#3E4451");
    }
    m_usageChart->legend()->setAlignment(Qt::AlignTop);
    m_usageChart->legend()->setLabelColor(axisColor);

    ui.chartViewUsage->setChart(m_usageChart);
    ui.chartViewUsage->setRenderHint(QPainter::Antialiasing);
    ui.chartViewUsage->setStyleSheet("background: transparent; border: none;");

    m_commitChart = new QChart();
    m_commitChart->setBackgroundVisible(false);
    m_commitChart->layout()->setContentsMargins(0, 0, 0, 0);

    m_commitSeries = new QSplineSeries();
    m_commitSeries->setName("커밋 차지 (GB)");
    m_commitSeries->setPen(QPen(QColor("#61AFEF"), 2.5));

    m_commitChart->addSeries(m_commitSeries);
    m_commitChart->createDefaultAxes();

    auto commitAxisX = qobject_cast<QValueAxis*>(m_commitChart->axes(Qt::Horizontal).first());
    auto commitAxisY = qobject_cast<QValueAxis*>(m_commitChart->axes(Qt::Vertical).first());
    if (commitAxisX) {
        commitAxisX->setRange(0, 60);
        commitAxisX->setLabelsColor(axisColor);
        commitAxisX->setGridLineColor("#3E4451");
    }
    if (commitAxisY) {
        commitAxisY->setLabelsColor(axisColor);
        commitAxisY->setGridLineColor("#3E4451");
    }
    m_commitChart->legend()->setLabelColor(axisColor);

    ui.chartViewCommit->setChart(m_commitChart);
    ui.chartViewCommit->setRenderHint(QPainter::Antialiasing);
    ui.chartViewCommit->setStyleSheet("background: transparent; border: none;");

    // --------------------------------------------------------
    // [4] 백그라운드 스레드 가동 및 데이터 파이프라인 연결
    // --------------------------------------------------------
    m_monitorThread = new QThread(this);
    m_workerPointer = new MemoryWorker();
    m_analyzer = new LeakAnalyzer();
    m_watchdog = new MemoryWatchdog(); //  [추가됨] Watchdog 객체 생성

    // 객체들을 백그라운드 스레드로 이동 (UI 멈춤 방지)
    m_workerPointer->moveToThread(m_monitorThread);
    m_analyzer->moveToThread(m_monitorThread);
    m_watchdog->moveToThread(m_monitorThread); //  [추가됨] Watchdog도 백그라운드로!

    // 스레드 시작 시 Worker 가동
    connect(m_monitorThread, &QThread::started, m_workerPointer, &MemoryWorker::startWork);

    // Worker -> UI (시스템 상태, 프로세스 상세)
    connect(m_workerPointer, &MemoryWorker::systemMemoryUpdated, this, &MemVis::updateSystemUI);
    connect(m_workerPointer, &MemoryWorker::processDetailUpdated, this, &MemVis::updateDetailUI);

    // Worker -> Analyzer (프로세스 목록 분석 전달)
    connect(m_workerPointer, &MemoryWorker::processListUpdated, m_analyzer, &LeakAnalyzer::onProcessDataReceived);
    // Analyzer -> UI (분석된 목록 테이블 업데이트)
    connect(m_analyzer, &LeakAnalyzer::analyzedDataReady, this, &MemVis::updateProcessTable);

    //  [추가됨] 데이터 파이프라인 -> Watchdog (감시자에게 데이터 공급)
    connect(m_workerPointer, &MemoryWorker::systemMemoryUpdated, m_watchdog, &MemoryWatchdog::onSystemMemoryUpdated);
    connect(m_analyzer, &LeakAnalyzer::analyzedDataReady, m_watchdog, &MemoryWatchdog::onAnalyzedDataReady);

    // 스레드 종료 시 메모리 해제 처리
    connect(m_monitorThread, &QThread::finished, m_workerPointer, &QObject::deleteLater);
    connect(m_monitorThread, &QThread::finished, m_analyzer, &QObject::deleteLater);
    connect(m_monitorThread, &QThread::finished, m_watchdog, &QObject::deleteLater); //  [추가됨]
    connect(m_monitorThread, &QThread::finished, m_monitorThread, &QObject::deleteLater);
    // --------------------------------------------------------
    // [5]  메모리 최적화 탭 UI <-> Watchdog 컨트롤 연동
    // --------------------------------------------------------

    // 1. UI 로그 창 연결 (Watchdog에서 emit한 로그를 textEditLog에 출력)
    connect(m_watchdog, &MemoryWatchdog::logMessage, this, [this](const QString& msg) {
        QString timeStr = QDateTime::currentDateTime().toString("[HH:mm:ss] ");
        ui.textEditLog->appendPlainText(timeStr + msg);
        });

    // 2. 라디오 버튼(프리셋 모드) 그룹화 및 연결
    QButtonGroup* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(ui.btnModeStandard, MemoryWatchdog::ModeStandard);
    modeGroup->addButton(ui.btnModeGaming, MemoryWatchdog::ModeGaming);
    modeGroup->addButton(ui.btnModeWorkstation, MemoryWatchdog::ModeWorkstation);

    connect(modeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        // Watchdog에 설정 전달 (비동기 스레드 호출의 안전성을 위해 QMetaObject 사용 권장)
        QMetaObject::invokeMethod(m_watchdog, "setOptimizationMode", Q_ARG(int, id));

        // UI 변경 효과 (UX)
        if (id == MemoryWatchdog::ModeGaming) {
            ui.chkEnablePagefileFlush->setChecked(false); // 게임모드는 디스크 플러시 강제 해제
            ui.sliderThreshold->setValue(70);             // 임계값 70%로 타이트하게 조임
        }
        else if (id == MemoryWatchdog::ModeStandard) {
            ui.chkEnablePagefileFlush->setChecked(true);
            ui.sliderThreshold->setValue(85);
        }
        else { // Workstation
            ui.chkEnablePagefileFlush->setChecked(true);
            ui.sliderThreshold->setValue(95);
        }
        });

    // --------------------------------------------------------
    // [3. 마스터 스위치 연결 (수정됨: 그룹박스 대신 개별 위젯 제어)]
    // --------------------------------------------------------
    connect(ui.chkEnableWatchdog, &QCheckBox::toggled, this, [this](bool checked) {
        QMetaObject::invokeMethod(m_watchdog, "setWatchdogEnabled", Q_ARG(bool, checked));

        // ui.groupBox_2 전체를 끄지 않고, 하위 위젯들만 켜고 끕니다.
        ui.chkEnableStandbyPurge->setEnabled(checked);
        ui.chkEnablePagefileFlush->setEnabled(checked);
        ui.chkEnableHardTrim->setEnabled(checked);
        ui.sliderThreshold->setEnabled(checked);
        });

    connect(ui.chkEnableStandbyPurge, &QCheckBox::toggled, m_watchdog, &MemoryWatchdog::setAllowStandbyPurge, Qt::QueuedConnection);
    connect(ui.chkEnablePagefileFlush, &QCheckBox::toggled, m_watchdog, &MemoryWatchdog::setAllowPagefileFlush, Qt::QueuedConnection);
    connect(ui.chkEnableHardTrim, &QCheckBox::toggled, m_watchdog, &MemoryWatchdog::setAllowHardTrim, Qt::QueuedConnection);

    // 4. 슬라이더 & 라벨 연결
    connect(ui.sliderThreshold, &QSlider::valueChanged, this, [this](int value) {
        QMetaObject::invokeMethod(m_watchdog, "setThreshold", Q_ARG(int, value));
        ui.lblThreshold->setText(QString("자동 최적화 발동 기준: %1%").arg(value));
        });

    // --------------------------------------------------------
    // [화이트리스트 추가/삭제 버튼 로직 (새로 추가됨)]
    // 주의: ui.btnAdd 와 ui.btnRemove 는 Designer에 설정한 버튼 이름에 맞추세요!
    // --------------------------------------------------------
    connect(ui.btnAddWhitelist, &QPushButton::clicked, this, [this]() {
        QDialog dialog(this);
        dialog.setWindowTitle("보호할 프로세스 선택");
        dialog.resize(350, 450);

        dialog.setStyleSheet(
            "QDialog { background-color: #282C34; color: #ABB2BF; }"
            "QListWidget { background-color: #2C313C; border: 1px solid #3E4451; border-radius: 6px; padding: 5px; color: #ABB2BF; }"
            "QListWidget::item { padding: 8px; border-radius: 4px; }"
            "QListWidget::item:hover { background-color: #3E4451; }"
            "QListWidget::item:selected { background-color: #61AFEF; color: #23272E; font-weight: bold; }"
            "QPushButton { background-color: #3E4451; color: white; border-radius: 4px; padding: 6px 12px; }"
            "QPushButton:hover { background-color: #61AFEF; color: #23272E; }"
        );

        QVBoxLayout* layout = new QVBoxLayout(&dialog);
        QLabel* label = new QLabel("현재 실행 중인 프로세스 목록에서 선택하세요:");
        label->setStyleSheet("font-weight: bold; color: #61AFEF; margin-bottom: 5px;");
        layout->addWidget(label);

        QListWidget* listWidget = new QListWidget(&dialog);
        listWidget->setIconSize(QSize(24, 24));
        layout->addWidget(listWidget);

        QSet<QString> uniqueNames;
        int rowCount = m_processModel->rowCount(QModelIndex());
        QFileIconProvider iconProvider;
        QIcon defaultIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

        for (int i = 0; i < rowCount; ++i) {
            QString name = m_processModel->getNameAt(i);
            DWORD pid = m_processModel->getPidAt(i); // 모델에서 PID 가져오기

            if (!name.isEmpty() && !uniqueNames.contains(name)) {
                uniqueNames.insert(name);
                QIcon processIcon;

                // [핵심 해결책] Windows API를 사용하여 PID로부터 프로세스의 전체 절대 경로(Full Path)를 직접 추적
                HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hProcess) {
                    WCHAR pathBuffer[MAX_PATH];
                    DWORD pathSize = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProcess, 0, pathBuffer, &pathSize)) {
                        QString fullPath = QString::fromWCharArray(pathBuffer);
                        // 추적한 진짜 절대 경로를 넘겨서 실제 아이콘 추출
                        processIcon = iconProvider.icon(QFileInfo(fullPath));
                    }
                    CloseHandle(hProcess);
                }

                // 시스템 권한 부족(보안 프로세스) 등으로 아이콘 추출에 실패한 경우만 기본 아이콘 적용
                if (processIcon.isNull()) {
                    processIcon = defaultIcon;
                }

                QListWidgetItem* item = new QListWidgetItem(processIcon, name);
                listWidget->addItem(item);
            }
        }

        listWidget->sortItems();

        QDialogButtonBox* buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, Qt::Horizontal, &dialog);
        layout->addWidget(buttonBox);

        connect(buttonBox, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        connect(listWidget, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

        if (dialog.exec() == QDialog::Accepted) {
            QListWidgetItem* selectedItem = listWidget->currentItem();
            if (selectedItem) {
                QString text = selectedItem->text();
                QIcon iconFromPopup = selectedItem->icon();

                if (ui.listWhitelist->findItems(text, Qt::MatchExactly).isEmpty()) {
                    // 선택한 실제 아이콘을 메인 리스트(ui.listWhitelist)에도 똑같이 적용
                    QListWidgetItem* mainListItem = new QListWidgetItem(iconFromPopup, text);
                    ui.listWhitelist->addItem(mainListItem);

                    QMetaObject::invokeMethod(m_watchdog, "addWhitelist", Q_ARG(QString, text));
                }
            }
        }
        });

    connect(ui.btnRemoveWhitelist, &QPushButton::clicked, this, [this]() {
        QList<QListWidgetItem*> items = ui.listWhitelist->selectedItems();
        for (QListWidgetItem* item : items) {
            QString text = item->text();
            delete item; // UI에서 삭제
            QMetaObject::invokeMethod(m_watchdog, "removeWhitelist", Q_ARG(QString, text));
        }
        });
    // --------------------------------------------------------
// [기본 화이트리스트 항목 추가 (아이콘 포함)]
// --------------------------------------------------------
    QFileIconProvider startupIconProvider;
    QIcon defaultExeIcon = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    // 1. memvis1.exe (자기 자신 프로그램)의 실제 아이콘 가져오기
    QString myAppPath = QCoreApplication::applicationFilePath(); // 내 실행파일 절대경로
    QIcon myAppIcon = startupIconProvider.icon(QFileInfo(myAppPath));
    if (myAppIcon.isNull()) myAppIcon = defaultExeIcon; // 혹시 실패하면 기본 아이콘

    QListWidgetItem* memvisItem = new QListWidgetItem(myAppIcon, "memvis1.exe");
    ui.listWhitelist->addItem(memvisItem);


    // 2. chrome.exe (특정할 수 없는 경로는 기본 실행파일 아이콘 적용)
    QListWidgetItem* chromeItem = new QListWidgetItem(defaultExeIcon, "chrome.exe");
    ui.listWhitelist->addItem(chromeItem);
    // --------------------------------------------------------
    // 5. 초기 UI 상태 세팅 (수정됨)
    // --------------------------------------------------------
    ui.btnModeStandard->setChecked(true);
    ui.chkEnableWatchdog->setChecked(false);

    // 프로그램 시작 시 하위 메뉴들만 비활성화
    ui.chkEnableStandbyPurge->setEnabled(false);
    ui.chkEnablePagefileFlush->setEnabled(false);
    ui.chkEnableHardTrim->setEnabled(false);
    ui.sliderThreshold->setEnabled(false);


    m_monitorThread->start();
}

MemVis::~MemVis()
{
    if (m_monitorThread) {
        m_monitorThread->quit();
        m_monitorThread->wait();
    }
}

// [updateSystemUI 함수는 기존과 100% 동일]
void MemVis::updateSystemUI(MonitorTypes::SystemMemoryInfo info)
{
    double availGB = info.availPhys / (1024.0 * 1024.0 * 1024.0);
    ui.labelAvailRAM->setText(QString("가용 메모리 :%1 GB").arg(availGB, 0, 'f', 2));
    ui.labelLoadPercent->setText(QString("메모리 로드율: %1%").arg(info.memoryLoad));

    double pagedMB = info.pagedPool / (1024.0 * 1024.0);
    double nonPagedMB = info.nonPagedPool / (1024.0 * 1024.0);
    double cacheGB = info.systemCache / (1024.0 * 1024.0 * 1024.0);

    ui.labelPagedPool->setText(QString("페이징: %1 MB").arg(pagedMB, 0, 'f', 0));
    ui.labelNonPagedPool->setText(QString("비페이징: %1 MB").arg(nonPagedMB, 0, 'f', 0));
    ui.labelSystemCache->setText(QString("캐시: %1 GB").arg(cacheGB, 0, 'f', 2));

    static bool isAxisSet = false;
    if (!isAxisSet) {
        auto commitAxisY = qobject_cast<QValueAxis*>(m_commitChart->axes(Qt::Vertical).first());
        if (commitAxisY) {
            double limitGB = info.commitLimit / (1024.0 * 1024.0 * 1024.0);
            commitAxisY->setRange(0, limitGB);
            commitAxisY->setTitleText("용량 (GB)");
        }
        isAxisSet = true;
    }

    m_timeTick++;
    m_loadSeries->append(m_timeTick, info.memoryLoad);
    double availPercent = (double)info.availPhys / info.totalPhys * 100.0;
    m_availSeries->append(m_timeTick, availPercent);

    double commitGB = info.commitCharge / (1024.0 * 1024.0 * 1024.0);
    m_commitSeries->append(m_timeTick, commitGB);

    if (m_timeTick > 60) {
        auto usageX = qobject_cast<QValueAxis*>(m_usageChart->axes(Qt::Horizontal).first());
        if (usageX) usageX->setRange(m_timeTick - 60, m_timeTick);

        auto commitX = qobject_cast<QValueAxis*>(m_commitChart->axes(Qt::Horizontal).first());
        if (commitX) commitX->setRange(m_timeTick - 60, m_timeTick);
    }
}

// [수정됨] 파라미터가 ProcessBasicInfo에서 AnalyzedProcessInfo로 변경되었습니다.
void MemVis::updateProcessTable(QList<MonitorTypes::AnalyzedProcessInfo> list)
{
    m_processModel->setProcessList(list);
}

// [showContextMenu 등 나머지 함수들은 기존 코드와 완전히 동일합니다]
void MemVis::showContextMenu(const QPoint& pos) {
    QModelIndex index = ui.tableViewProcess->indexAt(pos);
    if (!index.isValid()) return;

    ui.tableViewProcess->selectRow(index.row());
    int row = index.row();


    DWORD targetPID = m_processModel->getPidAt(row);
    QString targetName = m_processModel->getNameAt(row);

    QMenu contextMenu("Context menu", this);

    QAction actionAnalyze("🔍 집중 분석 (Deep Analysis)", this);
    QAction actionKill("❌ 프로세스 끝내기 (Kill)", this);

    contextMenu.addAction(&actionAnalyze);
    contextMenu.addSeparator();
    contextMenu.addAction(&actionKill);

    connect(&actionAnalyze, &QAction::triggered, this, [this, targetPID, targetName]() {
        this->startDeepAnalysis(targetPID, targetName);
        });

    connect(&actionKill, &QAction::triggered, this, [this, targetPID, targetName]() {
        this->killSelectedProcess(targetPID, targetName);
        });

    contextMenu.exec(ui.tableViewProcess->viewport()->mapToGlobal(pos));
}

void MemVis::killSelectedProcess(DWORD targetPID, QString targetName) {
    QMessageBox::StandardButton reply;
    reply = QMessageBox::warning(this, "프로세스 종료 경고",
        QString("정말로 '%1' (PID: %2) 프로세스를 강제로 종료하시겠습니까?\n\n저장되지 않은 데이터가 날아갈 수 있습니다.").arg(targetName).arg(targetPID),
        QMessageBox::Yes | QMessageBox::No);

    if (reply == QMessageBox::Yes) {
        HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, targetPID);
        if (hProcess != NULL) {
            if (TerminateProcess(hProcess, 1)) {
                if (m_selectedPID == targetPID) {
                    m_selectedPID = 0;
                    if (m_workerPointer) {
                        m_workerPointer->setTargetPID(0);
                    }
                }
            }
            else {
                QMessageBox::critical(this, "오류", "프로세스를 종료할 수 없습니다. (권한 부족)");
            }
            CloseHandle(hProcess);
        }
        else {
            QMessageBox::critical(this, "오류", "프로세스에 접근할 수 없습니다. (권한 부족)");
        }
    }
}

void MemVis::startDeepAnalysis(DWORD targetPID, QString targetName) {
    m_selectedPID = targetPID;
    m_detailLabels.clear();

    QDialog* dialog = new QDialog(this);
    dialog->setWindowTitle(QString("Deep Analysis - %1 (PID: %2)").arg(targetName).arg(targetPID));
    dialog->resize(450, 550);

    dialog->setStyleSheet("QDialog { background-color: #282C34; } "
        "QGroupBox { color: #61AFEF; font-weight: bold; border: 1px solid #3E4451; border-radius: 6px; margin-top: 10px; padding-top: 15px; } "
        "QLabel { color: #ABB2BF; font-size: 13px; }");

    QVBoxLayout* mainLayout = new QVBoxLayout(dialog);

    auto addRow = [&](QFormLayout* layout, QString title, QString key) {
        QLabel* valLabel = new QLabel("분석 대기 중...");
        layout->addRow(title, valLabel);
        m_detailLabels[key] = valLabel;
        };

    QGroupBox* groupBasic = new QGroupBox("기본 정보 (Basic Info)");
    QFormLayout* formBasic = new QFormLayout(groupBasic);
    addRow(formBasic, "Architecture:", "arch");
    addRow(formBasic, "Threads & Handles:", "threads_handles");
    mainLayout->addWidget(groupBasic);

    QGroupBox* groupVirtual = new QGroupBox("가상 메모리 (Virtual Memory)");
    QFormLayout* formVirtual = new QFormLayout(groupVirtual);
    addRow(formVirtual, "Private Usage:", "private");
    addRow(formVirtual, "Working Set:", "ws");
    addRow(formVirtual, "Peak Working Set:", "peak_ws");
    addRow(formVirtual, "Page Faults:", "faults");
    mainLayout->addWidget(groupVirtual);

    QGroupBox* groupVAD = new QGroupBox("메모리 구조 해부 (VAD Analysis)");
    QFormLayout* formVAD = new QFormLayout(groupVAD);
    addRow(formVAD, "Data / Heap (PRIVATE):", "vadPrivate");
    addRow(formVAD, "Code / Image (IMAGE):", "vadImage");
    addRow(formVAD, "Shared (MAPPED):", "vadMapped");
    mainLayout->addWidget(groupVAD);

    connect(dialog, &QDialog::finished, this, [this]() {
        this->m_selectedPID = 0;
        if (this->m_workerPointer) this->m_workerPointer->setTargetPID(0);
        });

    if (m_workerPointer) m_workerPointer->setTargetPID(targetPID);

    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->show();
}

QString formatBytes(SIZE_T bytes) {
    double mb = bytes / (1024.0 * 1024.0);
    if (mb > 1024) return QString("%1 GB").arg(mb / 1024.0, 0, 'f', 2);
    return QString("%1 MB").arg(mb, 0, 'f', 2);
}

void MemVis::updateDetailUI(MonitorTypes::ProcessDetailInfo detail) {
    if (m_selectedPID == 0 || m_selectedPID != detail.pid) return;

    if (m_detailLabels.contains("arch"))
        m_detailLabels["arch"]->setText(detail.architecture);

    if (m_detailLabels.contains("threads_handles"))
        m_detailLabels["threads_handles"]->setText(QString("Threads: %1 / Handles: %2").arg(detail.threads).arg(detail.handles));

    if (m_detailLabels.contains("private")) m_detailLabels["private"]->setText(formatBytes(detail.privateUsage));
    if (m_detailLabels.contains("ws"))      m_detailLabels["ws"]->setText(formatBytes(detail.workingSet));
    if (m_detailLabels.contains("peak_ws")) m_detailLabels["peak_ws"]->setText(formatBytes(detail.peakWorkingSet));
    if (m_detailLabels.contains("faults"))  m_detailLabels["faults"]->setText(QString::number(detail.pageFaults));

    if (m_detailLabels.contains("vadPrivate")) m_detailLabels["vadPrivate"]->setText(formatBytes(detail.memPrivate));
    if (m_detailLabels.contains("vadImage"))   m_detailLabels["vadImage"]->setText(formatBytes(detail.memImage));
    if (m_detailLabels.contains("vadMapped"))  m_detailLabels["vadMapped"]->setText(formatBytes(detail.memMapped));
}