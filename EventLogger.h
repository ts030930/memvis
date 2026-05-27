#pragma once
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QMutex>
#include <QDateTime>

class EventLogger {
public:
    // 싱글톤 패턴 (어디서든 편하게 EventLogger::getInstance().log() 호출 가능)
    static EventLogger& getInstance() {
        static EventLogger instance;
        return instance;
    }

    // 로그 작성 함수 (UI로도 보낼 수 있게 설계)
    void log(const QString& level, const QString& message);

private:
    EventLogger();
    ~EventLogger();

    // 복사 방지
    EventLogger(const EventLogger&) = delete;
    EventLogger& operator=(const EventLogger&) = delete;

    QFile m_logFile;
    QMutex m_mutex;
};