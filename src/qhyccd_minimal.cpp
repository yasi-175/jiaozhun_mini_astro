#include "qhyccd_minimal.h"

#include <QByteArray>

QhyccdMinimal::QhyccdMinimal(const QString &libraryPath)
    : m_library(libraryPath)
{
}

QhyccdMinimal::~QhyccdMinimal()
{
    release();
}

bool QhyccdMinimal::load(QString *errorMessage)
{
    if (m_library.isLoaded())
        return true;

    if (!m_library.load()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("load %1 failed: %2").arg(m_library.fileName(), m_library.errorString());
        return false;
    }

    return resolveSymbols(errorMessage);
}

bool QhyccdMinimal::initialize(QString *errorMessage)
{
    if (!load(errorMessage))
        return false;

    const uint32_t ret = InitQHYCCDResource();
    if (ret != 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("InitQHYCCDResource failed: %1").arg(ret);
        return false;
    }

    m_initialized = true;
    return true;
}

void QhyccdMinimal::release()
{
    if (m_initialized && ReleaseQHYCCDResource)
        ReleaseQHYCCDResource();
    m_initialized = false;

    if (m_library.isLoaded())
        m_library.unload();
}

QStringList QhyccdMinimal::scanDeviceIds(QString *errorMessage) const
{
    QStringList ids;
    if (!ScanQHYCCD || !GetQHYCCDId) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QHYCCD symbols are not loaded");
        return ids;
    }

    const uint32_t count = ScanQHYCCD();
    for (uint32_t index = 0; index < count; ++index) {
        char id[256] = {0};
        const uint32_t ret = GetQHYCCDId(index, id);
        if (ret == 0)
            ids << QString::fromLocal8Bit(id);
    }
    return ids;
}

qhyccd_handle *QhyccdMinimal::openDevice(const QString &preferredName, QString *openedId, QString *errorMessage) const
{
    if (!ScanQHYCCD || !GetQHYCCDId || !OpenQHYCCD) {
        if (errorMessage)
            *errorMessage = QStringLiteral("QHYCCD symbols are not loaded");
        return nullptr;
    }

    const uint32_t count = ScanQHYCCD();
    if (count == 0) {
        if (errorMessage)
            *errorMessage = QStringLiteral("no QHYCCD device found");
        return nullptr;
    }

    for (uint32_t index = 0; index < count; ++index) {
        char id[256] = {0};
        if (GetQHYCCDId(index, id) != 0)
            continue;

        const QString currentId = QString::fromLocal8Bit(id);
        const bool accepted = preferredName.isEmpty() || currentId.startsWith(preferredName);
        if (!accepted)
            continue;

        qhyccd_handle *handle = OpenQHYCCD(id);
        if (!handle)
            continue;

        if (openedId)
            *openedId = currentId;
        return handle;
    }

    if (errorMessage) {
        if (preferredName.isEmpty())
            *errorMessage = QStringLiteral("failed to open any QHYCCD device");
        else
            *errorMessage = QStringLiteral("device %1 not found or could not be opened").arg(preferredName);
    }
    return nullptr;
}

void QhyccdMinimal::closeDevice(qhyccd_handle *handle) const
{
    if (handle && CloseQHYCCD)
        CloseQHYCCD(handle);
}

bool QhyccdMinimal::isLoaded() const
{
    return m_library.isLoaded();
}

bool QhyccdMinimal::resolveSymbols(QString *errorMessage)
{
    auto resolve = [this, errorMessage](const char *name) -> QFunctionPointer {
        QFunctionPointer symbol = m_library.resolve(name);
        if (!symbol && errorMessage && errorMessage->isEmpty())
            *errorMessage = QStringLiteral("resolve symbol %1 failed").arg(QString::fromLatin1(name));
        return symbol;
    };

    InitQHYCCDResource = reinterpret_cast<InitQHYCCDResourceFunc>(resolve("InitQHYCCDResource"));
    ReleaseQHYCCDResource = reinterpret_cast<ReleaseQHYCCDResourceFunc>(resolve("ReleaseQHYCCDResource"));
    ScanQHYCCD = reinterpret_cast<ScanQHYCCDFunc>(resolve("ScanQHYCCD"));
    GetQHYCCDId = reinterpret_cast<GetQHYCCDIdFunc>(resolve("GetQHYCCDId"));
    OpenQHYCCD = reinterpret_cast<OpenQHYCCDFunc>(resolve("OpenQHYCCD"));
    CloseQHYCCD = reinterpret_cast<CloseQHYCCDFunc>(resolve("CloseQHYCCD"));
    SetQHYCCDWriteFPGA = reinterpret_cast<SetQHYCCDWriteFPGAFunc>(resolve("SetQHYCCDWriteFPGA"));
    QHYCCDVendRequestRead = reinterpret_cast<QHYCCDVendRequestReadFunc>(resolve("QHYCCDVendRequestRead"));

    return InitQHYCCDResource && ReleaseQHYCCDResource && ScanQHYCCD && GetQHYCCDId
            && OpenQHYCCD && CloseQHYCCD && SetQHYCCDWriteFPGA && QHYCCDVendRequestRead;
}
