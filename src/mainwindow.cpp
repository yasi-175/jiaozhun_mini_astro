#include "mainwindow.h"

#include <QDateTime>
#include <QHBoxLayout>
#include <QSerialPortInfo>
#include <QVBoxLayout>
#include <QWidget>

#include <cmath>

static constexpr double EncoderFullScale = 33554432.0;
static constexpr double MotorStepsPerRev = 200.0;
static constexpr double DriverMicrosteps = 256.0;
static constexpr double GearReduction = 100.0;
static constexpr double PulsesPerOutputRev = MotorStepsPerRev * DriverMicrosteps * GearReduction;
static constexpr double ArcsecPerRev = 360.0 * 3600.0;

static double shortestEncoderDelta(uint32_t current, uint32_t previous)
{
    double delta = static_cast<double>(current) - static_cast<double>(previous);
    const double halfScale = EncoderFullScale / 2.0;
    if (delta > halfScale)
        delta -= EncoderFullScale;
    else if (delta < -halfScale)
        delta += EncoderFullScale;
    return delta;
}

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
    m_commandSpeedLabel = new QLabel(tr("Command speed: --"), this);
    m_actualSpeedLabel = new QLabel(tr("Actual speed: --"), this);
    m_positionErrorLabel = new QLabel(tr("Position error: --"), this);
    m_actualIntervalLabel = new QLabel(tr("Actual: --"), this);
    m_readDurationLabel = new QLabel(tr("Read: --"), this);
    values->addWidget(m_decLabel);
    values->addWidget(m_decDegreeLabel);
    values->addWidget(m_commandSpeedLabel);
    values->addWidget(m_actualSpeedLabel);
    values->addWidget(m_positionErrorLabel);
    values->addWidget(m_actualIntervalLabel);
    values->addWidget(m_readDurationLabel);
    values->addStretch(1);
    root->addLayout(values);

    m_decSeries = new QLineSeries(this);
    m_decSeries->setName(tr("TianShanNode_EncoderDEC"));
    m_commandSpeedSeries = new QLineSeries(this);
    m_commandSpeedSeries->setName(tr("Command DEC speed"));
    m_actualSpeedSeries = new QLineSeries(this);
    m_actualSpeedSeries->setName(tr("Actual DEC speed"));
    m_positionErrorSeries = new QLineSeries(this);
    m_positionErrorSeries->setName(tr("Position error"));

    m_encoderChart = new QChart();
    m_encoderChart->legend()->setVisible(true);
    m_encoderChart->addSeries(m_decSeries);
    m_encoderChart->setTitle(tr("TianShanNode_EncoderDEC"));

    m_encoderAxisX = new QValueAxis(this);
    m_encoderAxisX->setTitleText(tr("Time (s)"));
    m_encoderAxisX->setRange(0.0, m_visibleSeconds);
    m_encoderAxisX->setLabelFormat("%.1f");

    m_encoderAxisY = new QValueAxis(this);
    m_encoderAxisY->setTitleText(tr("Encoder value"));
    m_encoderAxisY->setRange(0.0, 1.0);
    m_encoderAxisY->setLabelFormat("%.0f");

    m_encoderChart->addAxis(m_encoderAxisX, Qt::AlignBottom);
    m_encoderChart->addAxis(m_encoderAxisY, Qt::AlignLeft);
    m_decSeries->attachAxis(m_encoderAxisX);
    m_decSeries->attachAxis(m_encoderAxisY);

    m_commandSpeedChart = new QChart();
    m_commandSpeedChart->legend()->setVisible(true);
    m_commandSpeedChart->addSeries(m_commandSpeedSeries);
    m_commandSpeedChart->setTitle(tr("Command DEC Speed"));

    m_commandSpeedAxisX = new QValueAxis(this);
    m_commandSpeedAxisX->setTitleText(tr("Time (s)"));
    m_commandSpeedAxisX->setRange(0.0, m_visibleSeconds);
    m_commandSpeedAxisX->setLabelFormat("%.1f");

    m_commandSpeedAxisY = new QValueAxis(this);
    m_commandSpeedAxisY->setTitleText(tr("Speed (Hz)"));
    m_commandSpeedAxisY->setRange(-1.0, 1.0);
    m_commandSpeedAxisY->setLabelFormat("%.1f");

    m_commandSpeedChart->addAxis(m_commandSpeedAxisX, Qt::AlignBottom);
    m_commandSpeedChart->addAxis(m_commandSpeedAxisY, Qt::AlignLeft);
    m_commandSpeedSeries->attachAxis(m_commandSpeedAxisX);
    m_commandSpeedSeries->attachAxis(m_commandSpeedAxisY);

    m_speedChart = new QChart();
    m_speedChart->legend()->setVisible(true);
    m_speedChart->addSeries(m_actualSpeedSeries);
    m_speedChart->setTitle(tr("Actual DEC Speed"));

    m_speedAxisX = new QValueAxis(this);
    m_speedAxisX->setTitleText(tr("Time (s)"));
    m_speedAxisX->setRange(0.0, m_visibleSeconds);
    m_speedAxisX->setLabelFormat("%.1f");

    m_speedAxisY = new QValueAxis(this);
    m_speedAxisY->setTitleText(tr("Speed (Hz)"));
    m_speedAxisY->setRange(-1.0, 1.0);
    m_speedAxisY->setLabelFormat("%.1f");

    m_speedChart->addAxis(m_speedAxisX, Qt::AlignBottom);
    m_speedChart->addAxis(m_speedAxisY, Qt::AlignLeft);
    m_actualSpeedSeries->attachAxis(m_speedAxisX);
    m_actualSpeedSeries->attachAxis(m_speedAxisY);

    m_errorChart = new QChart();
    m_errorChart->legend()->setVisible(true);
    m_errorChart->addSeries(m_positionErrorSeries);
    m_errorChart->setTitle(tr("Position Error"));

    m_errorAxisX = new QValueAxis(this);
    m_errorAxisX->setTitleText(tr("Time (s)"));
    m_errorAxisX->setRange(0.0, m_visibleSeconds);
    m_errorAxisX->setLabelFormat("%.1f");

    m_errorAxisY = new QValueAxis(this);
    m_errorAxisY->setTitleText(tr("Error (arcsec)"));
    m_errorAxisY->setRange(0.0, 1.0);
    m_errorAxisY->setLabelFormat("%.2f");

    m_errorChart->addAxis(m_errorAxisX, Qt::AlignBottom);
    m_errorChart->addAxis(m_errorAxisY, Qt::AlignLeft);
    m_positionErrorSeries->attachAxis(m_errorAxisX);
    m_positionErrorSeries->attachAxis(m_errorAxisY);

    m_encoderChartView = new QChartView(m_encoderChart, this);
    m_commandSpeedChartView = new QChartView(m_commandSpeedChart, this);
    m_speedChartView = new QChartView(m_speedChart, this);
    m_errorChartView = new QChartView(m_errorChart, this);
    m_encoderChartView->setRenderHint(QPainter::Antialiasing);
    m_commandSpeedChartView->setRenderHint(QPainter::Antialiasing);
    m_speedChartView->setRenderHint(QPainter::Antialiasing);
    m_errorChartView->setRenderHint(QPainter::Antialiasing);
    root->addWidget(m_encoderChartView, 1);
    root->addWidget(m_commandSpeedChartView, 1);
    root->addWidget(m_speedChartView, 1);
    root->addWidget(m_errorChartView, 1);

    setCentralWidget(central);
    resize(1100, 900);
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
        if (!connected)
            setDecSpeedState(0.0, 0.0);
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
    setupGuideUi(root);
}

void MainWindow::setupGuideUi(QVBoxLayout *root)
{
    auto *guideToolbar = new QHBoxLayout();
    guideToolbar->setSpacing(8);

    auto *guideLabel = new QLabel(tr("Guide sim:"), this);

    auto *baseLabel = new QLabel(tr("Base kHz:"), this);
    m_guideBaseSpeedSpinBox = new QDoubleSpinBox(this);
    m_guideBaseSpeedSpinBox->setRange(-40.0, 40.0);
    m_guideBaseSpeedSpinBox->setDecimals(5);
    m_guideBaseSpeedSpinBox->setSingleStep(0.001);
    m_guideBaseSpeedSpinBox->setValue(0.05942);

    auto *deltaLabel = new QLabel(tr("Delta kHz:"), this);
    m_guideDeltaSpeedSpinBox = new QDoubleSpinBox(this);
    m_guideDeltaSpeedSpinBox->setRange(0.001, 40.0);
    m_guideDeltaSpeedSpinBox->setDecimals(5);
    m_guideDeltaSpeedSpinBox->setSingleStep(0.001);
    m_guideDeltaSpeedSpinBox->setValue(0.03500);

    auto *exposureLabel = new QLabel(tr("Exposure:"), this);
    m_guideExposureMsSpinBox = new QSpinBox(this);
    m_guideExposureMsSpinBox->setRange(100, 10000);
    m_guideExposureMsSpinBox->setSingleStep(100);
    m_guideExposureMsSpinBox->setValue(1000);
    m_guideExposureMsSpinBox->setSuffix(tr(" ms"));

    auto *aggrLabel = new QLabel(tr("Agg:"), this);
    m_guideAggressivenessSpinBox = new QSpinBox(this);
    m_guideAggressivenessSpinBox->setRange(1, 200);
    m_guideAggressivenessSpinBox->setValue(100);
    m_guideAggressivenessSpinBox->setSuffix(tr(" %"));

    auto *maxPulseLabel = new QLabel(tr("Max pulse:"), this);
    m_guideMaxPulseMsSpinBox = new QSpinBox(this);
    m_guideMaxPulseMsSpinBox->setRange(10, 10000);
    m_guideMaxPulseMsSpinBox->setSingleStep(50);
    m_guideMaxPulseMsSpinBox->setValue(1000);
    m_guideMaxPulseMsSpinBox->setSuffix(tr(" ms"));

    m_guideStartButton = new QPushButton(tr("Guide Start"), this);
    m_guideStopButton = new QPushButton(tr("Guide Stop"), this);
    m_guideStopButton->setEnabled(false);
    m_guideRmsLabel = new QLabel(tr("DEC RMS: --"), this);
    m_guideRmsLabel->setMinimumWidth(150);
    m_guideStatusLabel = new QLabel(tr("Guide idle"), this);
    m_guideStatusLabel->setMinimumWidth(260);

    guideToolbar->addWidget(guideLabel);
    guideToolbar->addWidget(baseLabel);
    guideToolbar->addWidget(m_guideBaseSpeedSpinBox);
    guideToolbar->addWidget(deltaLabel);
    guideToolbar->addWidget(m_guideDeltaSpeedSpinBox);
    guideToolbar->addWidget(exposureLabel);
    guideToolbar->addWidget(m_guideExposureMsSpinBox);
    guideToolbar->addWidget(aggrLabel);
    guideToolbar->addWidget(m_guideAggressivenessSpinBox);
    guideToolbar->addWidget(maxPulseLabel);
    guideToolbar->addWidget(m_guideMaxPulseMsSpinBox);
    guideToolbar->addWidget(m_guideStartButton);
    guideToolbar->addWidget(m_guideStopButton);
    guideToolbar->addWidget(m_guideRmsLabel);
    guideToolbar->addWidget(m_guideStatusLabel, 1);
    root->addLayout(guideToolbar);

    m_guideExposureTimer = new QTimer(this);
    m_guideExposureTimer->setTimerType(Qt::PreciseTimer);
    connect(m_guideExposureTimer, &QTimer::timeout, this, &MainWindow::runGuideExposure);

    m_guidePulseTimer = new QTimer(this);
    m_guidePulseTimer->setTimerType(Qt::PreciseTimer);
    m_guidePulseTimer->setSingleShot(true);
    connect(m_guidePulseTimer, &QTimer::timeout, this, &MainWindow::finishGuidePulse);

    connect(m_guideStartButton, &QPushButton::clicked, this, &MainWindow::startGuideSimulation);
    connect(m_guideStopButton, &QPushButton::clicked, this, &MainWindow::stopGuideSimulation);
    connect(m_mountController, &MountController::connectionChanged, this, [this](bool connected) {
        if (!connected)
            stopGuideSimulation();
    });
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
        setChartXRange(minVisibleSeconds, currentSeconds);
        updateYAxisForVisibleRange(m_decSeries, m_encoderAxisY, minVisibleSeconds, 10.0);
        updateYAxisForVisibleRange(m_commandSpeedSeries, m_commandSpeedAxisY, minVisibleSeconds, 1.0);
        updateYAxisForVisibleRange(m_actualSpeedSeries, m_speedAxisY, minVisibleSeconds, 1.0);
        updateYAxisForVisibleRange(m_positionErrorSeries, m_errorAxisY, minVisibleSeconds, 0.1);
    }
    else
        setChartXRange(0.0, m_visibleSeconds);
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

void MainWindow::updateGuideStatus(const QString &message)
{
    if (!m_guideStatusLabel)
        return;

    m_guideStatusLabel->setText(QStringLiteral("%1  %2")
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
    setDecSpeedState(0.0, 0.0);
}

void MainWindow::slewDecPositive()
{
    stopGuideSimulation();
    const double speedKHz = selectedMountSpeedKHz();
    sendGuideSpeed(speedKHz, speedKHz);
}

void MainWindow::slewDecNegative()
{
    stopGuideSimulation();
    const double speedKHz = -selectedMountSpeedKHz();
    sendGuideSpeed(speedKHz, speedKHz);
}

void MainWindow::stopDec()
{
    stopGuideSimulation();
    if (m_mountController->stopDec())
        setDecSpeedState(0.0, 0.0);
}

double MainWindow::selectedMountSpeedKHz() const
{
    return m_mountSpeedSpinBox ? m_mountSpeedSpinBox->value() : 1.0;
}

void MainWindow::setDecSpeedState(double commandSpeedKHz, double referenceSpeedKHz)
{
    m_commandedDecSpeedHz = commandSpeedKHz * 1000.0;
    m_referenceDecSpeedHz = referenceSpeedKHz * 1000.0;
}

bool MainWindow::sendGuideSpeed(double commandSpeedKHz, double referenceSpeedKHz)
{
    const double clampedSpeedKHz = qBound(-40.0, commandSpeedKHz, 40.0);
    if (!m_mountController->slewDec(clampedSpeedKHz))
        return false;

    setDecSpeedState(clampedSpeedKHz, referenceSpeedKHz);
    return true;
}

double MainWindow::guideCorrectionArcsecPerSecond() const
{
    const double deltaKHz = m_guideDeltaSpeedSpinBox ? m_guideDeltaSpeedSpinBox->value() : 0.035;
    return deltaKHz * 1000.0 / PulsesPerOutputRev * ArcsecPerRev;
}

void MainWindow::startGuideSimulation()
{
    if (!m_mountController->isConnected()) {
        updateGuideStatus(tr("Connect mount serial first"));
        return;
    }

    const double baseSpeedKHz = m_guideBaseSpeedSpinBox->value();
    if (!sendGuideSpeed(baseSpeedKHz, baseSpeedKHz))
        return;

    m_cumulativePositionErrorCounts = 0.0;
    m_signedPositionErrorArcsec = 0.0;
    m_positionErrorSeries->clear();
    resetGuideRms();
    m_guideActive = true;
    m_guidePulseActive = false;
    m_guideExposureTimer->start(m_guideExposureMsSpinBox->value());
    m_guideStartButton->setEnabled(false);
    m_guideStopButton->setEnabled(true);
    updateGuideStatus(tr("Guide running at base %1 kHz").arg(baseSpeedKHz, 0, 'f', 5));
}

void MainWindow::stopGuideSimulation()
{
    if (!m_guideActive && !m_guidePulseActive)
        return;

    m_guideActive = false;
    m_guidePulseActive = false;
    if (m_guideExposureTimer)
        m_guideExposureTimer->stop();
    if (m_guidePulseTimer)
        m_guidePulseTimer->stop();
    if (m_guideStartButton)
        m_guideStartButton->setEnabled(true);
    if (m_guideStopButton)
        m_guideStopButton->setEnabled(false);

    if (m_mountController->isConnected()) {
        const double baseSpeedKHz = m_guideBaseSpeedSpinBox ? m_guideBaseSpeedSpinBox->value() : 0.05942;
        sendGuideSpeed(baseSpeedKHz, baseSpeedKHz);
    }
    resetGuideRms();
    updateGuideStatus(tr("Guide stopped"));
}

void MainWindow::runGuideExposure()
{
    if (!m_guideActive || m_guidePulseActive || !m_mountController->isConnected())
        return;

    if (m_elapsed.isValid())
        appendGuideErrorSample(m_elapsed.elapsed(), m_signedPositionErrorArcsec);

    const double correctionRateArcsecPerSecond = guideCorrectionArcsecPerSecond();
    if (correctionRateArcsecPerSecond <= 0.0)
        return;

    const double errorArcsec = m_signedPositionErrorArcsec;
    const double aggr = static_cast<double>(m_guideAggressivenessSpinBox->value()) / 100.0;
    int pulseMs = static_cast<int>(qRound(qAbs(errorArcsec) / correctionRateArcsecPerSecond * 1000.0 * aggr));
    pulseMs = qBound(0, pulseMs, m_guideMaxPulseMsSpinBox->value());

    if (pulseMs < 1) {
        updateGuideStatus(tr("Guide idle, error %1 arcsec").arg(errorArcsec, 0, 'f', 3));
        return;
    }

    const double baseSpeedKHz = m_guideBaseSpeedSpinBox->value();
    const double deltaSpeedKHz = m_guideDeltaSpeedSpinBox->value();
    const double correctionSpeedKHz = errorArcsec >= 0.0 ? deltaSpeedKHz : -deltaSpeedKHz;
    const double pulseSpeedKHz = baseSpeedKHz + correctionSpeedKHz;

    if (!sendGuideSpeed(pulseSpeedKHz, baseSpeedKHz))
        return;

    m_guidePulseActive = true;
    m_guidePulseTimer->start(pulseMs);
    updateGuideStatus(tr("Pulse %1 ms at %2 kHz, error %3 arcsec")
                      .arg(pulseMs)
                      .arg(pulseSpeedKHz, 0, 'f', 5)
                      .arg(errorArcsec, 0, 'f', 3));
}

void MainWindow::finishGuidePulse()
{
    if (!m_guideActive || !m_mountController->isConnected()) {
        m_guidePulseActive = false;
        return;
    }

    const double baseSpeedKHz = m_guideBaseSpeedSpinBox->value();
    if (sendGuideSpeed(baseSpeedKHz, baseSpeedKHz))
        updateGuideStatus(tr("Back to base %1 kHz").arg(baseSpeedKHz, 0, 'f', 5));
    m_guidePulseActive = false;
}

void MainWindow::appendSample(const EncoderSample &sample)
{
    const double seconds = static_cast<double>(sample.elapsedMs) / 1000.0;
    m_decSeries->append(seconds, sample.dec);
    m_commandSpeedSeries->append(seconds, m_commandedDecSpeedHz);

    const int maxSamples = qMax(2, static_cast<int>((m_visibleSeconds * 1000.0) / qMax(1, m_intervalMs)) + 10);

    double actualSpeedHz = 0.0;
    double errorArcsec = m_signedPositionErrorArcsec;
    if (m_hasPreviousDerivedSample) {
        const qint64 elapsedDeltaMs = sample.elapsedMs - m_previousElapsedMs;
        if (elapsedDeltaMs > 0) {
            const double dtSeconds = static_cast<double>(elapsedDeltaMs) / 1000.0;
            const double encoderDeltaCounts = shortestEncoderDelta(sample.dec, m_previousDec);

            // DEC+ makes encoder counts decrease and DEC- makes them increase.
            // Negating the encoder delta puts measured speed in the same sign convention as the command.
            actualSpeedHz = -encoderDeltaCounts / EncoderFullScale * PulsesPerOutputRev / dtSeconds;

            const double theoreticalEncoderDeltaCounts =
                    -m_referenceDecSpeedHz / PulsesPerOutputRev * EncoderFullScale * dtSeconds;
            m_cumulativePositionErrorCounts += encoderDeltaCounts - theoreticalEncoderDeltaCounts;
            m_signedPositionErrorArcsec = m_cumulativePositionErrorCounts / EncoderFullScale * ArcsecPerRev;
            errorArcsec = m_signedPositionErrorArcsec;
        }
    }

    m_hasPreviousDerivedSample = true;
    m_previousDec = sample.dec;
    m_previousElapsedMs = sample.elapsedMs;

    m_actualSpeedSeries->append(seconds, actualSpeedHz);
    m_positionErrorSeries->append(seconds, errorArcsec);

    m_decLabel->setText(tr("DEC: %1").arg(sample.dec));
    m_decDegreeLabel->setText(tr("DEC deg: %1").arg(sample.decDegree, 0, 'f', 6));
    m_commandSpeedLabel->setText(tr("Command speed: %1 Hz").arg(m_commandedDecSpeedHz, 0, 'f', 2));
    m_actualSpeedLabel->setText(tr("Actual speed: %1 Hz").arg(actualSpeedHz, 0, 'f', 2));
    m_positionErrorLabel->setText(tr("Position error: %1 arcsec").arg(errorArcsec, 0, 'f', 3));
    m_actualIntervalLabel->setText(tr("Actual: %1 ms").arg(sample.actualIntervalMs));
    m_readDurationLabel->setText(tr("Read: %1 ms").arg(sample.readDurationMs));

    double minVisibleSeconds = 0.0;
    if (seconds > m_visibleSeconds) {
        minVisibleSeconds = seconds - m_visibleSeconds;
        pruneSeries(m_decSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_commandSpeedSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_actualSpeedSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_positionErrorSeries, minVisibleSeconds, maxSamples);
        setChartXRange(minVisibleSeconds, seconds);
    } else {
        pruneSeries(m_decSeries, 0.0, maxSamples);
        pruneSeries(m_commandSpeedSeries, 0.0, maxSamples);
        pruneSeries(m_actualSpeedSeries, 0.0, maxSamples);
        pruneSeries(m_positionErrorSeries, 0.0, maxSamples);
        setChartXRange(0.0, m_visibleSeconds);
    }

    updateYAxisForVisibleRange(m_decSeries, m_encoderAxisY, minVisibleSeconds, 10.0);
    updateYAxisForVisibleRange(m_commandSpeedSeries, m_commandSpeedAxisY, minVisibleSeconds, 1.0);
    updateYAxisForVisibleRange(m_actualSpeedSeries, m_speedAxisY, minVisibleSeconds, 1.0);
    updateYAxisForVisibleRange(m_positionErrorSeries, m_errorAxisY, minVisibleSeconds, 0.1);
}

void MainWindow::updateYAxisForVisibleRange(QLineSeries *series, QValueAxis *axis, double minVisibleSeconds, double minPadding)
{
    if (!series || !axis || series->count() == 0) {
        if (axis)
            axis->setRange(0.0, 1.0);
        return;
    }

    bool hasVisiblePoint = false;
    double minY = 0.0;
    double maxY = 0.0;
    const auto points = series->pointsVector();
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
        axis->setRange(0.0, 1.0);
        return;
    }

    const double padding = qMax(minPadding, (maxY - minY) * 0.08);
    axis->setRange(minY - padding, maxY + padding);
}

void MainWindow::resetChart()
{
    m_decSeries->clear();
    m_commandSpeedSeries->clear();
    m_actualSpeedSeries->clear();
    m_positionErrorSeries->clear();
    m_hasPreviousDerivedSample = false;
    m_previousDec = 0;
    m_previousElapsedMs = 0;
    m_cumulativePositionErrorCounts = 0.0;
    setChartXRange(0.0, m_visibleSeconds);
    m_encoderAxisY->setRange(0.0, 1.0);
    m_commandSpeedAxisY->setRange(-1.0, 1.0);
    m_speedAxisY->setRange(-1.0, 1.0);
    m_errorAxisY->setRange(0.0, 1.0);
    if (m_commandSpeedLabel)
        m_commandSpeedLabel->setText(tr("Command speed: --"));
    if (m_actualSpeedLabel)
        m_actualSpeedLabel->setText(tr("Actual speed: --"));
    if (m_positionErrorLabel)
        m_positionErrorLabel->setText(tr("Position error: --"));
    resetGuideRms();
}

void MainWindow::appendGuideErrorSample(qint64 elapsedMs, double errorArcsec)
{
    m_guideErrorSamples.append(QPointF(static_cast<double>(elapsedMs), errorArcsec));

    const double minKeepMs = static_cast<double>(elapsedMs) - m_visibleSeconds * 1000.0;
    while (!m_guideErrorSamples.isEmpty() && m_guideErrorSamples.first().x() < minKeepMs)
        m_guideErrorSamples.removeFirst();

    if (m_guideRmsLabel)
        m_guideRmsLabel->setText(tr("DEC RMS: %1 arcsec").arg(currentGuideRmsArcsec(), 0, 'f', 3));
}

double MainWindow::currentGuideRmsArcsec() const
{
    if (m_guideErrorSamples.isEmpty())
        return 0.0;

    double sumSquares = 0.0;
    for (const QPointF &sample : m_guideErrorSamples)
        sumSquares += sample.y() * sample.y();

    return std::sqrt(sumSquares / static_cast<double>(m_guideErrorSamples.size()));
}

void MainWindow::resetGuideRms()
{
    m_guideErrorSamples.clear();
    if (m_guideRmsLabel)
        m_guideRmsLabel->setText(tr("DEC RMS: --"));
}

void MainWindow::setChartXRange(double minSeconds, double maxSeconds)
{
    m_encoderAxisX->setRange(minSeconds, maxSeconds);
    m_commandSpeedAxisX->setRange(minSeconds, maxSeconds);
    m_speedAxisX->setRange(minSeconds, maxSeconds);
    m_errorAxisX->setRange(minSeconds, maxSeconds);
}

void MainWindow::pruneSeries(QLineSeries *series, double minVisibleSeconds, int maxSamples)
{
    if (!series)
        return;

    while (series->count() > maxSamples)
        series->remove(0);
    while (series->count() > 0 && series->at(0).x() < minVisibleSeconds)
        series->remove(0);
}
