#include "encoder_worker.h"

EncoderWorker::EncoderWorker(QObject *parent)
    : QObject(parent)
{
}

EncoderWorker::~EncoderWorker()
{
    stop();
}

void EncoderWorker::start(const QString &libraryPath,
                          const QString &deviceName,
                          int intervalMs,
                          bool bulkReadEnabled,
                          bool triggerEnabled)
{
    if (m_timer && m_timer->isActive())
        return;

    if (!m_timer) {
        m_timer = new QTimer(this);
        m_timer->setTimerType(Qt::PreciseTimer);
        connect(m_timer, &QTimer::timeout, this, &EncoderWorker::pollEncoder);
    }

    m_sdk = std::make_unique<QhyccdMinimal>(libraryPath);
    m_reader = std::make_unique<EncoderReader>(m_sdk.get());
    m_reader->setBulkReadEnabled(bulkReadEnabled);
    m_reader->setTriggerEnabled(triggerEnabled);
    setIntervalMs(intervalMs);

    QString error;
    if (!m_sdk->initialize(&error)) {
        emit statusChanged(error);
        emit stopped();
        return;
    }

    QString openedId;
    m_handle = m_sdk->openDevice(deviceName, &openedId, &error);
    if (!m_handle) {
        emit statusChanged(error);
        m_sdk->release();
        emit stopped();
        return;
    }

    m_lastSampleElapsedMs = -1;
    m_elapsed.restart();
    m_timer->start(m_intervalMs);
    emit statusChanged(tr("Opened %1, polling every %2 ms").arg(openedId).arg(m_intervalMs));
}

void EncoderWorker::stop()
{
    if (m_timer)
        m_timer->stop();

    if (m_sdk && m_handle) {
        m_sdk->closeDevice(m_handle);
        m_handle = nullptr;
    }
    if (m_sdk)
        m_sdk->release();
    m_reader.reset();
    m_sdk.reset();

    emit stopped();
}

void EncoderWorker::setIntervalMs(int intervalMs)
{
    m_intervalMs = qBound(1, intervalMs, 2000);
    if (m_timer)
        m_timer->setInterval(m_intervalMs);
}

void EncoderWorker::setBulkReadEnabled(bool enabled)
{
    if (m_reader)
        m_reader->setBulkReadEnabled(enabled);
}

void EncoderWorker::setTriggerEnabled(bool enabled)
{
    if (m_reader)
        m_reader->setTriggerEnabled(enabled);
}

void EncoderWorker::pollEncoder()
{
    if (!m_reader || !m_handle)
        return;

    EncoderSample sample;
    QString error;
    const qint64 readStartMs = m_elapsed.elapsed();
    QElapsedTimer readTimer;
    readTimer.start();

    if (!m_reader->read(m_handle, readStartMs, &sample, &error)) {
        emit statusChanged(error);
        stop();
        return;
    }

    sample.readDurationMs = readTimer.elapsed();
    sample.actualIntervalMs = m_lastSampleElapsedMs >= 0 ? readStartMs - m_lastSampleElapsedMs : 0;
    m_lastSampleElapsedMs = readStartMs;
    emit sampleReady(sample);
}
