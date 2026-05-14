#include "mainwindow.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QSerialPortInfo>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(const QString &libraryPath,
                       const QString &deviceName,
                       int intervalMs,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_libraryPath(libraryPath)
    , m_deviceName(deviceName)
    , m_intervalMs(intervalMs)
{
    qRegisterMetaType<EncoderSample>("EncoderSample");
    m_mountController = new MountController(this);
    setupUi();
}

MainWindow::~MainWindow()
{
    stopReading();
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
    }
}

void MainWindow::setupUi()
{
    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(12, 12, 12, 12);
    root->setSpacing(8);

    auto *toolbar = new QHBoxLayout();
    toolbar->setSpacing(8);
    m_startButton = new QPushButton(tr("Start"), this);
    m_stopButton = new QPushButton(tr("Stop"), this);
    m_stopButton->setEnabled(false);
    auto *intervalLabel = new QLabel(tr("Interval ms:"), this);
    m_intervalSpinBox = new QSpinBox(this);
    m_intervalSpinBox->setRange(1, 2000);
    m_intervalSpinBox->setSingleStep(50);
    m_intervalSpinBox->setValue(m_intervalMs);
    m_intervalSpinBox->setSuffix(tr(" ms"));
    auto *visibleSecondsLabel = new QLabel(tr("Window s:"), this);
    m_visibleSecondsSpinBox = new QSpinBox(this);
    m_visibleSecondsSpinBox->setRange(1, 2000);
    m_visibleSecondsSpinBox->setSingleStep(10);
    m_visibleSecondsSpinBox->setValue(static_cast<int>(m_visibleSeconds));
    m_visibleSecondsSpinBox->setSuffix(tr(" s"));
    m_bulkReadCheckBox = new QCheckBox(tr("Bulk DEC"), this);
    m_bulkReadCheckBox->setChecked(true);
    m_triggerCheckBox = new QCheckBox(tr("Trigger C0"), this);
    m_triggerCheckBox->setChecked(true);
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setMinimumWidth(280);
    toolbar->addWidget(m_startButton);
    toolbar->addWidget(m_stopButton);
    toolbar->addWidget(intervalLabel);
    toolbar->addWidget(m_intervalSpinBox);
    toolbar->addWidget(visibleSecondsLabel);
    toolbar->addWidget(m_visibleSecondsSpinBox);
    toolbar->addWidget(m_bulkReadCheckBox);
    toolbar->addWidget(m_triggerCheckBox);
    toolbar->addWidget(m_statusLabel, 1);
    root->addLayout(toolbar);
    setupMountUi(root);

    auto *values = new QHBoxLayout();
    values->setSpacing(16);
    m_decLabel = new QLabel(tr("DEC: --"), this);
    m_decDegreeLabel = new QLabel(tr("DEC deg: --"), this);
    m_actualIntervalLabel = new QLabel(tr("Actual: --"), this);
    m_readDurationLabel = new QLabel(tr("Read: --"), this);
    values->addWidget(m_decLabel);
    values->addWidget(m_decDegreeLabel);
    values->addWidget(m_actualIntervalLabel);
    values->addWidget(m_readDurationLabel);
    values->addStretch(1);
    root->addLayout(values);

    m_decSeries = new QLineSeries(this);
    m_decSeries->setName(tr("TianShanNode_EncoderDEC"));

    m_chart = new QChart();
    m_chart->legend()->setVisible(true);
    m_chart->addSeries(m_decSeries);
    m_chart->setTitle(tr("TianShanNode_EncoderDEC"));

    m_axisX = new QValueAxis(this);
    m_axisX->setTitleText(tr("Time (s)"));
    m_axisX->setRange(0.0, m_visibleSeconds);
    m_axisX->setLabelFormat("%.1f");

    m_axisY = new QValueAxis(this);
    m_axisY->setTitleText(tr("Encoder value"));
    m_axisY->setRange(m_minY, m_maxY);
    m_axisY->setLabelFormat("%.0f");

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);
    m_decSeries->attachAxis(m_axisX);
    m_decSeries->attachAxis(m_axisY);

    m_chartView = new QChartView(m_chart, this);
    m_chartView->setRenderHint(QPainter::Antialiasing);
    root->addWidget(m_chartView, 1);

    setCentralWidget(central);
    resize(1000, 620);
    setWindowTitle(tr("jiaozhun_miniastro Encoder Monitor"));

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startReading);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopReading);
    connect(m_intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::updateReadInterval);
    connect(m_visibleSecondsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::updateVisibleSeconds);
    connect(m_bulkReadCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "setBulkReadEnabled", Qt::QueuedConnection, Q_ARG(bool, checked));
    });
    connect(m_triggerCheckBox, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_worker)
            QMetaObject::invokeMethod(m_worker, "setTriggerEnabled", Qt::QueuedConnection, Q_ARG(bool, checked));
    });

}

void MainWindow::setupMountUi(QVBoxLayout *root)
{
    auto *mountToolbar = new QHBoxLayout();
    mountToolbar->setSpacing(8);

    auto *mountLabel = new QLabel(tr("Mount:"), this);
    m_mountPortComboBox = new QComboBox(this);
    m_mountPortComboBox->setMinimumWidth(180);
    m_refreshPortsButton = new QPushButton(tr("Refresh"), this);

    auto *baudLabel = new QLabel(tr("Baud:"), this);
    m_mountBaudSpinBox = new QSpinBox(this);
    m_mountBaudSpinBox->setRange(1200, 1000000);
    m_mountBaudSpinBox->setSingleStep(9600);
    m_mountBaudSpinBox->setValue(115200);

    m_mountConnectButton = new QPushButton(tr("Connect"), this);
    m_mountDisconnectButton = new QPushButton(tr("Disconnect"), this);
    m_mountDisconnectButton->setEnabled(false);

    auto *speedLabel = new QLabel(tr("DEC kHz:"), this);
    m_mountSpeedSpinBox = new QDoubleSpinBox(this);
    m_mountSpeedSpinBox->setRange(0.01, 40.0);
    m_mountSpeedSpinBox->setDecimals(3);
    m_mountSpeedSpinBox->setSingleStep(0.1);
    m_mountSpeedSpinBox->setValue(1.0);
    m_mountSpeedSpinBox->setSuffix(tr(" kHz"));

    m_decPositiveButton = new QPushButton(tr("DEC +"), this);
    m_decNegativeButton = new QPushButton(tr("DEC -"), this);
    m_decStopButton = new QPushButton(tr("DEC Stop"), this);
    m_decPositiveButton->setEnabled(false);
    m_decNegativeButton->setEnabled(false);
    m_decStopButton->setEnabled(false);

    m_mountStatusLabel = new QLabel(tr("Mount disconnected"), this);
    m_mountStatusLabel->setMinimumWidth(260);

    mountToolbar->addWidget(mountLabel);
    mountToolbar->addWidget(m_mountPortComboBox);
    mountToolbar->addWidget(m_refreshPortsButton);
    mountToolbar->addWidget(baudLabel);
    mountToolbar->addWidget(m_mountBaudSpinBox);
    mountToolbar->addWidget(m_mountConnectButton);
    mountToolbar->addWidget(m_mountDisconnectButton);
    mountToolbar->addWidget(speedLabel);
    mountToolbar->addWidget(m_mountSpeedSpinBox);
    mountToolbar->addWidget(m_decNegativeButton);
    mountToolbar->addWidget(m_decStopButton);
    mountToolbar->addWidget(m_decPositiveButton);
    mountToolbar->addWidget(m_mountStatusLabel, 1);
    root->addLayout(mountToolbar);

    connect(m_refreshPortsButton, &QPushButton::clicked, this, &MainWindow::refreshMountPorts);
    connect(m_mountConnectButton, &QPushButton::clicked, this, &MainWindow::connectMount);
    connect(m_mountDisconnectButton, &QPushButton::clicked, this, &MainWindow::disconnectMount);
    connect(m_decPositiveButton, &QPushButton::clicked, this, &MainWindow::slewDecPositive);
    connect(m_decNegativeButton, &QPushButton::clicked, this, &MainWindow::slewDecNegative);
    connect(m_decStopButton, &QPushButton::clicked, this, &MainWindow::stopDec);
    connect(m_mountController, &MountController::statusChanged, this, &MainWindow::updateMountStatus);
    connect(m_mountController, &MountController::connectionChanged, this, [this](bool connected) {
        m_mountConnectButton->setEnabled(!connected);
        m_mountDisconnectButton->setEnabled(connected);
        m_decPositiveButton->setEnabled(connected);
        m_decNegativeButton->setEnabled(connected);
        m_decStopButton->setEnabled(connected);
        m_mountPortComboBox->setEnabled(!connected);
        m_mountBaudSpinBox->setEnabled(!connected);
        m_refreshPortsButton->setEnabled(!connected);
    });

    refreshMountPorts();
}

void MainWindow::startReading()
{
    if (m_workerThread)
        return;

    resetChart();
    m_elapsed.restart();

    m_workerThread = new QThread(this);
    m_worker = new EncoderWorker();
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::finished, m_worker, &QObject::deleteLater);
    connect(m_worker, &EncoderWorker::sampleReady, this, &MainWindow::appendSample);
    connect(m_worker, &EncoderWorker::statusChanged, this, &MainWindow::updateStatus);
    connect(m_worker, &EncoderWorker::stopped, this, &MainWindow::handleWorkerStopped);
    connect(m_workerThread, &QThread::started, this, [this]() {
        QMetaObject::invokeMethod(m_worker, "start", Qt::QueuedConnection,
                                  Q_ARG(QString, m_libraryPath),
                                  Q_ARG(QString, m_deviceName),
                                  Q_ARG(int, m_intervalMs),
                                  Q_ARG(bool, m_bulkReadCheckBox->isChecked()),
                                  Q_ARG(bool, m_triggerCheckBox->isChecked()));
    });

    m_workerThread->start();

    m_startButton->setEnabled(false);
    m_stopButton->setEnabled(true);
    updateStatus(tr("Opening device..."));
}

void MainWindow::stopReading()
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "stop", Qt::BlockingQueuedConnection);
}

void MainWindow::handleWorkerStopped()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        m_workerThread->deleteLater();
        m_workerThread = nullptr;
        m_worker = nullptr;
    }
    if (m_startButton)
        m_startButton->setEnabled(true);
    if (m_stopButton)
        m_stopButton->setEnabled(false);
    if (m_statusLabel)
        updateStatus(tr("Stopped"));
}

void MainWindow::updateReadInterval(int intervalMs)
{
    m_intervalMs = intervalMs;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, "setIntervalMs", Qt::QueuedConnection, Q_ARG(int, m_intervalMs));
    if (m_workerThread)
        updateStatus(tr("Polling every %1 ms").arg(m_intervalMs));
}

void MainWindow::updateVisibleSeconds(int seconds)
{
    m_visibleSeconds = static_cast<double>(seconds);
    const double currentSeconds = m_elapsed.isValid()
            ? static_cast<double>(m_elapsed.elapsed()) / 1000.0
            : 0.0;
    if (currentSeconds > m_visibleSeconds) {
        const double minVisibleSeconds = currentSeconds - m_visibleSeconds;
        m_axisX->setRange(minVisibleSeconds, currentSeconds);
        updateYAxisForVisibleRange(minVisibleSeconds);
    }
    else
        m_axisX->setRange(0.0, m_visibleSeconds);
}

void MainWindow::updateStatus(const QString &message)
{
    m_statusLabel->setText(QStringLiteral("%1  %2")
                           .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                           .arg(message));
}

void MainWindow::updateMountStatus(const QString &message)
{
    if (!m_mountStatusLabel)
        return;

    m_mountStatusLabel->setText(QStringLiteral("%1  %2")
                                .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                                .arg(message));
}

void MainWindow::refreshMountPorts()
{
    const QString current = m_mountPortComboBox->currentData().toString();
    m_mountPortComboBox->clear();

    const auto ports = QSerialPortInfo::availablePorts();
    for (const QSerialPortInfo &port : ports) {
        QString label = port.portName();
        if (!port.description().isEmpty())
            label += tr(" - %1").arg(port.description());
        m_mountPortComboBox->addItem(label, port.systemLocation());
    }

    if (m_mountPortComboBox->count() == 0)
        m_mountPortComboBox->addItem(tr("No serial ports"), QString());

    const int index = m_mountPortComboBox->findData(current);
    if (index >= 0)
        m_mountPortComboBox->setCurrentIndex(index);
}

void MainWindow::connectMount()
{
    const QString portName = m_mountPortComboBox->currentData().toString();
    if (portName.isEmpty()) {
        updateMountStatus(tr("No mount serial port selected"));
        return;
    }

    m_mountController->connectToPort(portName, m_mountBaudSpinBox->value());
}

void MainWindow::disconnectMount()
{
    m_mountController->disconnectFromPort();
}

void MainWindow::slewDecPositive()
{
    m_mountController->slewDec(selectedMountSpeedKHz());
}

void MainWindow::slewDecNegative()
{
    m_mountController->slewDec(-selectedMountSpeedKHz());
}

void MainWindow::stopDec()
{
    m_mountController->stopDec();
}

double MainWindow::selectedMountSpeedKHz() const
{
    return m_mountSpeedSpinBox ? m_mountSpeedSpinBox->value() : 1.0;
}

void MainWindow::appendSample(const EncoderSample &sample)
{
    const double seconds = static_cast<double>(sample.elapsedMs) / 1000.0;
    m_decSeries->append(seconds, sample.dec);

    const int maxSamples = qMax(2, static_cast<int>((m_visibleSeconds * 1000.0) / qMax(1, m_intervalMs)) + 10);
    while (m_decSeries->count() > maxSamples)
        m_decSeries->remove(0);

    m_decLabel->setText(tr("DEC: %1").arg(sample.dec));
    m_decDegreeLabel->setText(tr("DEC deg: %1").arg(sample.decDegree, 0, 'f', 6));
    m_actualIntervalLabel->setText(tr("Actual: %1 ms").arg(sample.actualIntervalMs));
    m_readDurationLabel->setText(tr("Read: %1 ms").arg(sample.readDurationMs));

    double minVisibleSeconds = 0.0;
    if (seconds > m_visibleSeconds) {
        minVisibleSeconds = seconds - m_visibleSeconds;
        while (m_decSeries->count() > 0 && m_decSeries->at(0).x() < minVisibleSeconds)
            m_decSeries->remove(0);
        m_axisX->setRange(minVisibleSeconds, seconds);
    } else {
        m_axisX->setRange(0.0, m_visibleSeconds);
    }

    updateYAxisForVisibleRange(minVisibleSeconds);
}

void MainWindow::updateYAxisForVisibleRange(double minVisibleSeconds)
{
    if (m_decSeries->count() == 0) {
        m_axisY->setRange(0.0, 1.0);
        return;
    }

    bool hasVisiblePoint = false;
    double minY = 0.0;
    double maxY = 0.0;
    const auto points = m_decSeries->pointsVector();
    for (const QPointF &point : points) {
        if (point.x() < minVisibleSeconds)
            continue;
        if (!hasVisiblePoint) {
            minY = point.y();
            maxY = point.y();
            hasVisiblePoint = true;
        } else {
            minY = qMin(minY, point.y());
            maxY = qMax(maxY, point.y());
        }
    }

    if (!hasVisiblePoint) {
        m_axisY->setRange(0.0, 1.0);
        return;
    }

    m_minY = minY;
    m_maxY = maxY;
    m_hasSample = true;

    const double padding = qMax(10.0, (m_maxY - m_minY) * 0.08);
    m_axisY->setRange(m_minY - padding, m_maxY + padding);
}

void MainWindow::resetChart()
{
    m_decSeries->clear();
    m_minY = 0.0;
    m_maxY = 1.0;
    m_hasSample = false;
    m_axisX->setRange(0.0, m_visibleSeconds);
    m_axisY->setRange(m_minY, m_maxY);
}
