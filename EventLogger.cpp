#include "EventLogger.h"
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

EventLogger::EventLogger() {
    // 프로그램 실행 경로에 logs 폴더 생성
    QString logDir = QCoreApplication::applicationDirPath() + "/logs";
    QDir().mkpath(logDir);

    // 날짜별 로그 파일 (예: MemVis_Watchdog_2026-04-29.log)
    QString dateStr = QDateTime::currentDateTime().toString("yyyy-MM-dd");
    m_logFile.setFileName(logDir + "/MemVis_Watchdog_" + dateStr + ".log");

    if (!m_logFile.open(QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Failed to open log file!";
    }
}

EventLogger::~EventLogger() {
    if (m_logFile.isOpen()) {
        m_logFile.close();
    }
}

void EventLogger::log(const QString& level, const QString& message) {
    QMutexLocker locker(&m_mutex); // 스레드 안전성 확보

    QString timestamp = QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss");
    QString formattedMessage = QString("[%1] [%2] %3\n").arg(timestamp, level, message);

    if (m_logFile.isOpen()) {
        QTextStream out(&m_logFile);
        out << formattedMessage;
        out.flush(); // 즉시 디스크에 쓰기
    }

    // 디버그 창에도 출력
    qDebug().noquote() << formattedMessage.trimmed();
}