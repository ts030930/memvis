
#pragma once
#include <QAbstractTableModel>
#include <QIcon>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHash>
#include <QBrush>
#include <QColor>
#include "SystemMonitorTypes.h"

//  [추가됨] 데이터를 UI 아이템으로 만들지 않고, C++ 구조체 그대로 관리하는 초고속 모델
class ProcessTableModel : public QAbstractTableModel {
    Q_OBJECT
public:
    explicit ProcessTableModel(QObject* parent = nullptr);

    // QAbstractTableModel 필수 구현 함수 4가지
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    int columnCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation, int role = Qt::DisplayRole) const override;

    // 테이블 헤더 클릭 시 정렬을 처리하는 함수
    void sort(int column, Qt::SortOrder order = Qt::AscendingOrder) override;

    // 외부에서 새로운 1초 데이터를 덮어씌울 때 호출
    void setProcessList(const QList<MonitorTypes::AnalyzedProcessInfo>& newList);

    // 우클릭 메뉴(Context Menu)에서 PID와 이름을 가져오기 위한 유틸리티 함수
    DWORD getPidAt(int row) const;
    QString getNameAt(int row) const;

private:
    QList<MonitorTypes::AnalyzedProcessInfo> m_data;
    QStringList m_headers;

    // 아이콘 생성은 UI가 아니라 Model에서 처리하도록 이동 (성능 최적화)
    mutable QHash<QString, QIcon> m_iconCache;
    mutable QFileIconProvider m_iconProvider;

    // 현재 정렬 상태 기억용
    int m_sortColumn = -1;
    Qt::SortOrder m_sortOrder = Qt::AscendingOrder;
};