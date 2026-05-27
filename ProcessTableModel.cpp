#include "ProcessTableModel.h"
#include <algorithm>

ProcessTableModel::ProcessTableModel(QObject* parent)
    : QAbstractTableModel(parent)
{
    m_headers << "PID" << "프로세스 이름" << "스레드 수" << "워킹셋 (MB)"
        << "전용 메모리 (MB)" << "페이지 폴트" << "누수 위험도";
}

int ProcessTableModel::rowCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return m_data.count();
}

int ProcessTableModel::columnCount(const QModelIndex& parent) const {
    Q_UNUSED(parent);
    return m_headers.count();
}

QVariant ProcessTableModel::headerData(int section, Qt::Orientation orientation, int role) const {
    if (orientation == Qt::Horizontal && role == Qt::DisplayRole) {
        return m_headers.value(section);
    }
    return QVariant();
}

QVariant ProcessTableModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_data.count()) return QVariant();

    const auto& analyzed = m_data.at(index.row());
    const auto& info = analyzed.basic;
    int col = index.column();

    // 1. 텍스트 및 숫자 표시 (DisplayRole)
    if (role == Qt::DisplayRole) {
        switch (col) {
        case 0: return (uint)info.pid;
        case 1: return info.name;
        case 2: return (uint)info.threads;
        case 3: return (qulonglong)(info.workingSet / (1024 * 1024));
        case 4: return (qulonglong)(info.privateUsage / (1024 * 1024));
        case 5: return (qulonglong)info.pageFaults;
        case 6: return analyzed.leakScore;
        }
    }
    // 2. 프로세스 아이콘 표시 (DecorationRole)
    else if (role == Qt::DecorationRole && col == 1) {
        if (m_iconCache.contains(info.name)) {
            return m_iconCache.value(info.name);
        }
        else {
            QIcon processIcon = info.path.isEmpty() ? m_iconProvider.icon(QFileIconProvider::Computer)
                : m_iconProvider.icon(QFileInfo(info.path));
            m_iconCache.insert(info.name, processIcon);
            return processIcon;
        }
    }
    // 3. 누수 위험도 배경색 (BackgroundRole)
    else if (role == Qt::BackgroundRole && col == 6) {
        if (analyzed.leakScore >= 80) return QBrush(QColor("#5C2B29"));
        if (analyzed.leakScore >= 50) return QBrush(QColor("#5C4B29"));
    }
    // 4. 누수 위험도 글자색 (ForegroundRole)
    else if (role == Qt::ForegroundRole && col == 6) {
        if (analyzed.leakScore >= 80) return QBrush(QColor("#E06C75"));
        if (analyzed.leakScore >= 50) return QBrush(QColor("#E5C07B"));
    }
    // 5. 우측 정렬 (TextAlignmentRole) - 숫자는 우측 정렬이 예쁩니다.
    else if (role == Qt::TextAlignmentRole) {
        if (col != 1) return (int)(Qt::AlignRight | Qt::AlignVCenter);
    }

    return QVariant();
}

void ProcessTableModel::setProcessList(const QList<MonitorTypes::AnalyzedProcessInfo>& newList) {
    //  이 두 줄이 마법의 핵심입니다! 화면 갱신을 초고속으로 처리합니다.
    beginResetModel();
    m_data = newList;

    // 만약 기존에 사용자가 정렬해둔 상태라면, 데이터를 넣자마자 다시 정렬합니다.
    if (m_sortColumn >= 0) {
        sort(m_sortColumn, m_sortOrder);
    }
    endResetModel();
}

void ProcessTableModel::sort(int column, Qt::SortOrder order) {
    m_sortColumn = column;
    m_sortOrder = order;

    // C++11 람다를 이용한 초고속 정렬 로직 (안전한 비교 연산자로 수정됨)
    std::sort(m_data.begin(), m_data.end(), [column, order](const MonitorTypes::AnalyzedProcessInfo& a, const MonitorTypes::AnalyzedProcessInfo& b) {

        if (order == Qt::AscendingOrder) {
            // 오름차순 정렬
            switch (column) {
            case 0: return a.basic.pid < b.basic.pid;
            case 1: return a.basic.name.compare(b.basic.name, Qt::CaseInsensitive) < 0;
            case 2: return a.basic.threads < b.basic.threads;
            case 3: return a.basic.workingSet < b.basic.workingSet;
            case 4: return a.basic.privateUsage < b.basic.privateUsage;
            case 5: return a.basic.pageFaults < b.basic.pageFaults;
            case 6: return a.leakScore < b.leakScore;
            }
        }
        else {
            // 내림차순 정렬 (!result 대신 명시적인 > 연산 사용)
            switch (column) {
            case 0: return a.basic.pid > b.basic.pid;
            case 1: return a.basic.name.compare(b.basic.name, Qt::CaseInsensitive) > 0;
            case 2: return a.basic.threads > b.basic.threads;
            case 3: return a.basic.workingSet > b.basic.workingSet;
            case 4: return a.basic.privateUsage > b.basic.privateUsage;
            case 5: return a.basic.pageFaults > b.basic.pageFaults;
            case 6: return a.leakScore > b.leakScore;
            }
        }
        return false; // 예외 상황 대비 안전장치
        });

    // 정렬이 끝났음을 UI에 알림
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

DWORD ProcessTableModel::getPidAt(int row) const {
    if (row >= 0 && row < m_data.count()) return m_data.at(row).basic.pid;
    return 0;
}

QString ProcessTableModel::getNameAt(int row) const {
    if (row >= 0 && row < m_data.count()) return m_data.at(row).basic.name;
    return QString();
}