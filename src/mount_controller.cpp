#include "mount_controller.h"

MountController::MountController(QObject *parent)
    : QObject(parent)
{
    connect(&m_serial, &QSerialPort::readyRead, this, &MountController::readAvailableData);
    connect(&m_serial, &QSerialPort::errorOccurred, this, &MountController::handleSerialError);
}

MountController::~MountController()
{
    disconnectFromPort();
}

bool MountController::isConnected() const
{
    return m_serial.isOpen();
}

QString MountController::portName() const
{
    return m_serial.portName();
}

bool MountController::connectToPort(const QString &portName, int baudRate)
{
    if (m_serial.isOpen())
        m_serial.close();

    m_readBuffer.clear();
    m_serial.setPortName(portName);
    m_serial.setBaudRate(baudRate);
    m_serial.setDataBits(QSerialPort::Data8);
    m_serial.setParity(QSerialPort::NoParity);
    m_serial.setStopBits(QSerialPort::OneStop);
    m_serial.setFlowControl(QSerialPort::NoFlowControl);

    if (!m_serial.open(QIODevice::ReadWrite)) {
        emit statusChanged(tr("Mount serial open failed: %1").arg(m_serial.errorString()));
        emit connectionChanged(false);
        return false;
    }

    emit connectionChanged(true);
    emit statusChanged(tr("Mount connected on %1 @ %2").arg(portName).arg(baudRate));
    return true;
}

void MountController::disconnectFromPort()
{
    if (!m_serial.isOpen())
        return;

    stopDec();
    m_serial.close();
    m_readBuffer.clear();
    emit connectionChanged(false);
    emit statusChanged(tr("Mount disconnected"));
}

bool MountController::slewDec(double speedKHz)
{
    if (!m_serial.isOpen()) {
        emit statusChanged(tr("Mount serial is not connected"));
        return false;
    }

    if (!sendLine(QStringLiteral("MOTOR:MODE,0")))
        return false;

    return sendLine(QStringLiteral("MOTOR:SPEED,%1").arg(speedKHz, 0, 'f', 6));
}

bool MountController::stopDec()
{
    if (!m_serial.isOpen())
        return false;

    bool ok = sendLine(QStringLiteral("MOTOR:SPEED,0"));
    ok = sendLine(QStringLiteral("MOTOR:MODE,0")) && ok;
    return ok;
}

void MountController::readAvailableData()
{
    m_readBuffer += m_serial.readAll();

    while (true) {
        const int newlineIndex = m_readBuffer.indexOf('\n');
        const int carriageReturnIndex = m_readBuffer.indexOf('\r');
        int lineEnd = -1;
        if (newlineIndex >= 0 && carriageReturnIndex >= 0)
            lineEnd = qMin(newlineIndex, carriageReturnIndex);
        else
            lineEnd = qMax(newlineIndex, carriageReturnIndex);

        if (lineEnd < 0)
            break;

        QByteArray line = m_readBuffer.left(lineEnd).trimmed();
        m_readBuffer.remove(0, lineEnd + 1);
        if (!line.isEmpty()) {
            const QString text = QString::fromLocal8Bit(line);
            emit responseReceived(text);
            emit statusChanged(tr("Mount: %1").arg(text));
        }
    }

    if (m_readBuffer.size() > 1024)
        m_readBuffer.clear();
}

void MountController::handleSerialError(QSerialPort::SerialPortError error)
{
    if (error == QSerialPort::NoError)
        return;

    emit statusChanged(tr("Mount serial error: %1").arg(m_serial.errorString()));
    if (error == QSerialPort::ResourceError) {
        m_serial.close();
        emit connectionChanged(false);
    }
}

bool MountController::sendLine(const QString &line)
{
    if (!m_serial.isOpen()) {
        emit statusChanged(tr("Mount serial is not connected"));
        return false;
    }

    const QByteArray payload = line.toLatin1() + '\n';
    const qint64 written = m_serial.write(payload);
    if (written != payload.size()) {
        emit statusChanged(tr("Mount write failed: %1").arg(m_serial.errorString()));
        return false;
    }

    if (!m_serial.waitForBytesWritten(100)) {
        emit statusChanged(tr("Mount write timeout: %1").arg(line));
        return false;
    }

    emit statusChanged(tr("Mount cmd: %1").arg(line));
    return true;
}
