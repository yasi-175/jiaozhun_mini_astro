#include "encoder_reader.h"

#include <QString>

static constexpr uint32_t QHYCCD_SUCCESS = 0;
static constexpr double EncoderFullScale = 33554432.0;

EncoderReader::EncoderReader(QhyccdMinimal *sdk)
    : m_sdk(sdk)
{
}

void EncoderReader::setBulkReadEnabled(bool enabled)
{
    m_bulkReadEnabled = enabled;
}

void EncoderReader::setTriggerEnabled(bool enabled)
{
    m_triggerEnabled = enabled;
}

uint32_t EncoderReader::ReadturntableEncoder(qhyccd_handle *camhandle,
                                             uint32_t &encoderDEC,
                                             uint32_t &unusedEncoder,
                                             float &decRealStep,
                                             float &unusedRealStep)
{
    encoderDEC = 0;
    unusedEncoder = 0;
    decRealStep = 0.0f;
    unusedRealStep = 0.0f;

    // Only TianShanNode_EncoderDEC is displayed. Bulk mode reduces four USB
    // control reads to one request; disable it if the device returns bad order.
    if (m_bulkReadEnabled) {
        uint8_t decBytes[4] = {0};
        m_sdk->QHYCCDVendRequestRead(camhandle, 0xbc, 0, 0x39, 4, decBytes);
        encoderDEC = decBytes[0] * 131072 + decBytes[1] * 512 + decBytes[2] * 2 + decBytes[3];
    } else {
        for (uint8_t i = 4; i < 8; ++i)
            m_sdk->QHYCCDVendRequestRead(camhandle, 0xbc, 0, 0x40 - i, 1, m_date + i);

        encoderDEC = m_date[7] * 131072 + m_date[6] * 512 + m_date[5] * 2 + m_date[4];
    }

    if (m_triggerEnabled) {
        m_sdk->SetQHYCCDWriteFPGA(camhandle, 0, 0xC0, 0x00);
        m_sdk->SetQHYCCDWriteFPGA(camhandle, 0, 0xC0, 0x01);
        m_sdk->SetQHYCCDWriteFPGA(camhandle, 0, 0xC0, 0x00);
    }

    if (m_yearFpga[0] == 0)
        m_sdk->QHYCCDVendRequestRead(camhandle, 0xbc, 0, 0xC8, 1, m_yearFpga);

    return QHYCCD_SUCCESS;
}

bool EncoderReader::read(qhyccd_handle *camhandle, qint64 elapsedMs, EncoderSample *sample, QString *errorMessage)
{
    if (!m_sdk || !camhandle || !sample) {
        if (errorMessage)
            *errorMessage = QStringLiteral("encoder reader is not ready");
        return false;
    }

    uint32_t TianShanNode_EncoderDEC = 0;
    uint32_t unusedEncoder = 0;
    float dec_realstep = 0.0f;
    float unusedRealStep = 0.0f;
    const uint32_t ret = ReadturntableEncoder(camhandle,
                                              TianShanNode_EncoderDEC,
                                              unusedEncoder,
                                              dec_realstep,
                                              unusedRealStep);
    if (ret != QHYCCD_SUCCESS) {
        if (errorMessage)
            *errorMessage = QStringLiteral("ReadturntableEncoder failed: %1").arg(ret);
        return false;
    }

    sample->elapsedMs = elapsedMs;
    sample->dec = TianShanNode_EncoderDEC;
    sample->decRealStep = dec_realstep;
    sample->decDegree = static_cast<double>(TianShanNode_EncoderDEC) / EncoderFullScale * 360.0;
    return true;
}
