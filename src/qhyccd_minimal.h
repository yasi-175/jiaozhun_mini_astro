#ifndef QHYCCD_MINIMAL_H
#define QHYCCD_MINIMAL_H

#include <QLibrary>
#include <QString>
#include <QStringList>

#include <cstdint>

typedef void qhyccd_handle;

class QhyccdMinimal
{
public:
    typedef uint32_t (*InitQHYCCDResourceFunc)(void);
    typedef uint32_t (*ReleaseQHYCCDResourceFunc)(void);
    typedef uint32_t (*ScanQHYCCDFunc)(void);
    typedef uint32_t (*GetQHYCCDIdFunc)(uint32_t, char *);
    typedef qhyccd_handle *(*OpenQHYCCDFunc)(char *);
    typedef uint32_t (*CloseQHYCCDFunc)(qhyccd_handle *);
    typedef uint32_t (*SetQHYCCDWriteFPGAFunc)(qhyccd_handle *, uint8_t, uint8_t, uint8_t);
    typedef uint32_t (*QHYCCDVendRequestReadFunc)(qhyccd_handle *, uint8_t, uint16_t, uint16_t, uint32_t, uint8_t *);

    explicit QhyccdMinimal(const QString &libraryPath = QStringLiteral("/usr/local/lib/libqhyccd.so"));
    ~QhyccdMinimal();

    bool load(QString *errorMessage = nullptr);
    bool initialize(QString *errorMessage = nullptr);
    void release();

    QStringList scanDeviceIds(QString *errorMessage = nullptr) const;
    qhyccd_handle *openDevice(const QString &preferredName, QString *openedId, QString *errorMessage = nullptr) const;
    void closeDevice(qhyccd_handle *handle) const;

    bool isLoaded() const;

    SetQHYCCDWriteFPGAFunc SetQHYCCDWriteFPGA = nullptr;
    QHYCCDVendRequestReadFunc QHYCCDVendRequestRead = nullptr;

private:
    bool resolveSymbols(QString *errorMessage);

    QLibrary m_library;
    bool m_initialized = false;

    InitQHYCCDResourceFunc InitQHYCCDResource = nullptr;
    ReleaseQHYCCDResourceFunc ReleaseQHYCCDResource = nullptr;
    ScanQHYCCDFunc ScanQHYCCD = nullptr;
    GetQHYCCDIdFunc GetQHYCCDId = nullptr;
    OpenQHYCCDFunc OpenQHYCCD = nullptr;
    CloseQHYCCDFunc CloseQHYCCD = nullptr;
};

#endif // QHYCCD_MINIMAL_H
