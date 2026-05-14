#ifndef ENCODER_WORKER_H
#define ENCODER_WORKER_H

#include "encoder_reader.h"
#include "qhyccd_minimal.h"

#include <memory>

#include <QElapsedTimer>
#include <QObject>
#include <QTimer>

class EncoderWorker : public QObject
{
    Q_OBJECT

public:
    explicit EncoderWorker(QObject *parent = nullptr);
    ~EncoderWorker() override;

public slots:
    void start(const QString &libraryPath,
               const QString &deviceName,
               int intervalMs,
               bool bulkReadEnabled,
               bool triggerEnabled);
    void stop();
    void setIntervalMs(int intervalMs);
    void setBulkReadEnabled(bool enabled);
    void setTriggerEnabled(bool enabled);

signals:
    void sampleReady(const EncoderSample &sample);
    void statusChanged(const QString &message);
    void stopped();

private slots:
    void pollEncoder();

private:
    std::unique_ptr<QhyccdMinimal> m_sdk;
    std::unique_ptr<EncoderReader> m_reader;
    qhyccd_handle *m_handle = nullptr;
    QTimer *m_timer = nullptr;
    QElapsedTimer m_elapsed;
    int m_intervalMs = 200;
    qint64 m_lastSampleElapsedMs = -1;
};

#endif // ENCODER_WORKER_H
