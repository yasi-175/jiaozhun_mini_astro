#ifndef MOUNT_CONTROLLER_H
#define MOUNT_CONTROLLER_H

#include <QObject>
#include <QSerialPort>
#include <QString>
#include <QtGlobal>

class MountController : public QObject
{
    Q_OBJECT

public:
    explicit MountController(QObject *parent = nullptr);
    ~MountController() override;

    bool isConnected() const;
    QString portName() const;

public slots:
    bool connectToPort(const QString &portName, int baudRate);
    void disconnectFromPort();
    bool slewDec(double speedKHz);
    bool stopDec();

signals:
    void connectionChanged(bool connected);
    void statusChanged(const QString &message);
    void responseReceived(const QString &line);

private slots:
    void readAvailableData();
    void handleSerialError(QSerialPort::SerialPortError error);

private:
    bool sendLine(const QString &line);

    QSerialPort m_serial;
    QByteArray m_readBuffer;
};

#endif // MOUNT_CONTROLLER_H
