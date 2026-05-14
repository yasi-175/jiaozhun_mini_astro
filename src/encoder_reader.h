#ifndef ENCODER_READER_H
#define ENCODER_READER_H

#include "qhyccd_minimal.h"

#include <QtGlobal>

struct EncoderSample
{
    qint64 elapsedMs = 0;
    uint32_t dec = 0;
    float decRealStep = 0.0f;
    double decDegree = 0.0;
    qint64 readDurationMs = 0;
    qint64 actualIntervalMs = 0;
};

Q_DECLARE_METATYPE(EncoderSample)

class EncoderReader
{
public:
    explicit EncoderReader(QhyccdMinimal *sdk);

    void setBulkReadEnabled(bool enabled);
    void setTriggerEnabled(bool enabled);

    uint32_t ReadturntableEncoder(qhyccd_handle *camhandle,
                                  uint32_t &encoderDEC,
                                  uint32_t &unusedEncoder,
                                  float &decRealStep,
                                  float &unusedRealStep);

    bool read(qhyccd_handle *camhandle, qint64 elapsedMs, EncoderSample *sample, QString *errorMessage = nullptr);

private:
    QhyccdMinimal *m_sdk = nullptr;
    uint8_t m_date[9] = {0};
    uint8_t m_doubleDate[5] = {0};
    uint8_t m_yearFpga[2] = {0};
    bool m_bulkReadEnabled = true;
    bool m_triggerEnabled = true;
};

#endif // ENCODER_READER_H
