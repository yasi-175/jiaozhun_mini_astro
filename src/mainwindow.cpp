#include "mainwindow.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QHBoxLayout>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSerialPortInfo>
#include <QPen>
#include <QTextStream>
#include <QThread>
#include <QVBoxLayout>
#include <QWidget>
#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <numeric>

static constexpr double EncoderFullScale = 33554432.0;
static constexpr double MotorStepsPerRev = 200.0;
static constexpr double DriverMicrosteps = 256.0;
static constexpr double GearReduction = 100.0;
static constexpr double PulsesPerOutputRev = MotorStepsPerRev * DriverMicrosteps * GearReduction;
static constexpr double MtCalStopSteps = 5120000.0;
static constexpr double ArcsecPerRev = 360.0 * 3600.0;
static constexpr uint32_t MtRawFullScale = 1u << 21;
static constexpr uint32_t TamaFullScale = 1u << 25;

static double angle25ToDegree(uint32_t value25)
{
    return static_cast<double>(value25 & (TamaFullScale - 1u)) / EncoderFullScale * 360.0;
}

static double shortestArcsecFrom25(uint32_t value25, uint32_t reference25)
{
    int32_t diff = static_cast<int32_t>(value25 & (TamaFullScale - 1u))
            - static_cast<int32_t>(reference25 & (TamaFullScale - 1u));
    const int32_t half = static_cast<int32_t>(TamaFullScale >> 1);
    const int32_t full = static_cast<int32_t>(TamaFullScale);
    if (diff >= half)
        diff -= full;
    if (diff < -half)
        diff += full;
    return static_cast<double>(diff) / EncoderFullScale * ArcsecPerRev;
}

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
    m_clearButton = new QPushButton(tr("Clear"), this);
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
    m_showCommandSpeedChartCheckBox = new QCheckBox(tr("Speed chart"), this);
    m_showCommandSpeedChartCheckBox->setChecked(true);
    m_showPositionErrorChartCheckBox = new QCheckBox(tr("Error chart"), this);
    m_showPositionErrorChartCheckBox->setChecked(true);
    m_showMtCompareChartCheckBox = new QCheckBox(tr("MT chart"), this);
    m_showMtCompareChartCheckBox->setChecked(true);
    m_showHysteresisChartCheckBox = new QCheckBox(tr("Hys chart"), this);
    m_showHysteresisChartCheckBox->setChecked(false);
    m_showTamagawaErrorCheckBox = new QCheckBox(tr("Tamagawa"), this);
    m_showTamagawaErrorCheckBox->setChecked(true);
    m_showMtRawErrorCheckBox = new QCheckBox(tr("MT raw"), this);
    m_showMtRawErrorCheckBox->setChecked(true);
    m_showMtFilteredErrorCheckBox = new QCheckBox(tr("MT filtered"), this);
    m_showMtFilteredErrorCheckBox->setChecked(true);
    m_showMtPeakMarkerCheckBox = new QCheckBox(tr("Peak marker"), this);
    m_showMtPeakMarkerCheckBox->setChecked(true);
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setMinimumWidth(280);
    toolbar->addWidget(m_startButton);
    toolbar->addWidget(m_stopButton);
    toolbar->addWidget(m_clearButton);
    toolbar->addWidget(intervalLabel);
    toolbar->addWidget(m_intervalSpinBox);
    toolbar->addWidget(visibleSecondsLabel);
    toolbar->addWidget(m_visibleSecondsSpinBox);
    toolbar->addWidget(m_bulkReadCheckBox);
    toolbar->addWidget(m_triggerCheckBox);
    toolbar->addWidget(m_showCommandSpeedChartCheckBox);
    toolbar->addWidget(m_showPositionErrorChartCheckBox);
    toolbar->addWidget(m_showMtCompareChartCheckBox);
    toolbar->addWidget(m_showHysteresisChartCheckBox);
    toolbar->addWidget(m_statusLabel, 1);
    root->addLayout(toolbar);

    auto *errorSeriesToolbar = new QHBoxLayout();
    errorSeriesToolbar->setSpacing(12);
    errorSeriesToolbar->addWidget(new QLabel(tr("Position lines:"), this));
    errorSeriesToolbar->addWidget(m_showTamagawaErrorCheckBox);
    errorSeriesToolbar->addWidget(m_showMtRawErrorCheckBox);
    errorSeriesToolbar->addWidget(m_showMtFilteredErrorCheckBox);
    errorSeriesToolbar->addWidget(m_showMtPeakMarkerCheckBox);
    errorSeriesToolbar->addStretch(1);
    root->addLayout(errorSeriesToolbar);

    setupMountUi(root);

    auto *values = new QVBoxLayout();
    values->setSpacing(4);
    auto *valuesRow1 = new QHBoxLayout();
    valuesRow1->setSpacing(16);
    auto *valuesRow2 = new QHBoxLayout();
    valuesRow2->setSpacing(16);
    m_decLabel = new QLabel(tr("DEC: --"), this);
    m_decDegreeLabel = new QLabel(tr("DEC deg: --"), this);
    m_commandSpeedLabel = new QLabel(tr("Command speed: --"), this);
    m_actualSpeedLabel = new QLabel(tr("Actual speed: --"), this);
    m_positionErrorLabel = new QLabel(tr("Position error: --"), this);
    m_actualIntervalLabel = new QLabel(tr("Actual: --"), this);
    m_readDurationLabel = new QLabel(tr("Read: --"), this);
    valuesRow1->addWidget(m_decLabel);
    valuesRow1->addWidget(m_decDegreeLabel);
    valuesRow1->addWidget(m_commandSpeedLabel);
    valuesRow1->addStretch(1);
    valuesRow2->addWidget(m_actualSpeedLabel);
    valuesRow2->addWidget(m_positionErrorLabel);
    valuesRow2->addWidget(m_actualIntervalLabel);
    valuesRow2->addWidget(m_readDurationLabel);
    valuesRow2->addStretch(1);
    values->addLayout(valuesRow1);
    values->addLayout(valuesRow2);
    root->addLayout(values);

    m_commandSpeedSeries = new QLineSeries(this);
    m_commandSpeedSeries->setName(tr("Command DEC speed"));
    m_positionErrorSeries = new QLineSeries(this);
    m_positionErrorSeries->setName(tr("Tamagawa position error"));
    m_mtRawOffsetPositionErrorSeries = new QLineSeries(this);
    m_mtRawOffsetPositionErrorSeries->setName(tr("MT raw+offset position error"));
    m_mtPhaseFilteredSeries = new QLineSeries(this);
    m_mtPhaseFilteredSeries->setName(tr("MT raw big-period filtered"));
    QPen mtPhaseFilteredPen(Qt::darkMagenta);
    mtPhaseFilteredPen.setWidth(2);
    m_mtPhaseFilteredSeries->setPen(mtPhaseFilteredPen);
    m_mtPhasePeakMarkerSeries = new QScatterSeries(this);
    m_mtPhasePeakMarkerSeries->setName(tr("MT phase peak marker"));
    m_mtPhasePeakMarkerSeries->setMarkerShape(QScatterSeries::MarkerShapeCircle);
    m_mtPhasePeakMarkerSeries->setMarkerSize(12.0);
    m_mtPhasePeakMarkerSeries->setColor(Qt::red);
    m_mtPhasePeakMarkerSeries->setBorderColor(Qt::red);
    m_tamaDegreeSeries = new QLineSeries(this);
    m_tamaDegreeSeries->setName(tr("Tamagawa deg"));
    m_mtRawOffsetDegreeSeries = new QLineSeries(this);
    m_mtRawOffsetDegreeSeries->setName(tr("MT raw+offset deg"));
    m_hysPosToNegCorrSeries = new QLineSeries(this);
    m_hysPosToNegCorrSeries->setName(tr("+->- avg dCorr"));
    m_hysNegToPosCorrSeries = new QLineSeries(this);
    m_hysNegToPosCorrSeries->setName(tr("-->+ avg dCorr"));
    m_hysPosToNegRawSeries = new QLineSeries(this);
    m_hysPosToNegRawSeries->setName(tr("+->- avg dRaw"));
    m_hysNegToPosRawSeries = new QLineSeries(this);
    m_hysNegToPosRawSeries->setName(tr("-->+ avg dRaw"));
    m_hysCurrentCorrSeries = new QLineSeries(this);
    m_hysCurrentCorrSeries->setName(tr("current dCorr"));
    m_hysCurrentRawSeries = new QLineSeries(this);
    m_hysCurrentRawSeries->setName(tr("current dRaw"));
    m_hysCurrentFitSeries = new QLineSeries(this);
    m_hysCurrentFitSeries->setName(tr("current fit dCorr"));

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

    m_errorChart = new QChart();
    m_errorChart->legend()->setVisible(true);
    m_errorChart->addSeries(m_positionErrorSeries);
    m_errorChart->addSeries(m_mtRawOffsetPositionErrorSeries);
    m_errorChart->addSeries(m_mtPhaseFilteredSeries);
    m_errorChart->addSeries(m_mtPhasePeakMarkerSeries);
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
    m_mtRawOffsetPositionErrorSeries->attachAxis(m_errorAxisX);
    m_mtRawOffsetPositionErrorSeries->attachAxis(m_errorAxisY);
    m_mtPhaseFilteredSeries->attachAxis(m_errorAxisX);
    m_mtPhaseFilteredSeries->attachAxis(m_errorAxisY);
    m_mtPhasePeakMarkerSeries->attachAxis(m_errorAxisX);
    m_mtPhasePeakMarkerSeries->attachAxis(m_errorAxisY);

    m_mtCompareChart = new QChart();
    m_mtCompareChart->legend()->setVisible(true);
    m_mtCompareChart->addSeries(m_tamaDegreeSeries);
    m_mtCompareChart->addSeries(m_mtRawOffsetDegreeSeries);
    m_mtCompareChart->setTitle(tr("MT6835 Calibration Compare"));

    m_mtCompareAxisX = new QValueAxis(this);
    m_mtCompareAxisX->setTitleText(tr("Time (s)"));
    m_mtCompareAxisX->setRange(0.0, m_visibleSeconds);
    m_mtCompareAxisX->setLabelFormat("%.1f");

    m_mtCompareAxisY = new QValueAxis(this);
    m_mtCompareAxisY->setTitleText(tr("Degree"));
    m_mtCompareAxisY->setRange(0.0, 360.0);
    m_mtCompareAxisY->setLabelFormat("%.4f");

    m_mtCompareChart->addAxis(m_mtCompareAxisX, Qt::AlignBottom);
    m_mtCompareChart->addAxis(m_mtCompareAxisY, Qt::AlignLeft);
    m_tamaDegreeSeries->attachAxis(m_mtCompareAxisX);
    m_tamaDegreeSeries->attachAxis(m_mtCompareAxisY);
    m_mtRawOffsetDegreeSeries->attachAxis(m_mtCompareAxisX);
    m_mtRawOffsetDegreeSeries->attachAxis(m_mtCompareAxisY);

    m_hysteresisChart = new QChart();
    m_hysteresisChart->legend()->setVisible(true);
    m_hysteresisChart->addSeries(m_hysPosToNegCorrSeries);
    m_hysteresisChart->addSeries(m_hysNegToPosCorrSeries);
    m_hysteresisChart->addSeries(m_hysPosToNegRawSeries);
    m_hysteresisChart->addSeries(m_hysNegToPosRawSeries);
    m_hysteresisChart->addSeries(m_hysCurrentCorrSeries);
    m_hysteresisChart->addSeries(m_hysCurrentRawSeries);
    m_hysteresisChart->addSeries(m_hysCurrentFitSeries);
    m_hysteresisChart->setTitle(tr("Direction Hysteresis Transition"));

    m_hysteresisAxisX = new QValueAxis(this);
    m_hysteresisAxisX->setTitleText(tr("Steps since reverse"));
    m_hysteresisAxisX->setRange(0.0, 20000.0);
    m_hysteresisAxisX->setLabelFormat("%.0f");

    m_hysteresisAxisY = new QValueAxis(this);
    m_hysteresisAxisY->setTitleText(tr("Delta error (arcsec)"));
    m_hysteresisAxisY->setRange(-1000.0, 1000.0);
    m_hysteresisAxisY->setLabelFormat("%.1f");

    m_hysteresisChart->addAxis(m_hysteresisAxisX, Qt::AlignBottom);
    m_hysteresisChart->addAxis(m_hysteresisAxisY, Qt::AlignLeft);
    m_hysPosToNegCorrSeries->attachAxis(m_hysteresisAxisX);
    m_hysPosToNegCorrSeries->attachAxis(m_hysteresisAxisY);
    m_hysNegToPosCorrSeries->attachAxis(m_hysteresisAxisX);
    m_hysNegToPosCorrSeries->attachAxis(m_hysteresisAxisY);
    m_hysPosToNegRawSeries->attachAxis(m_hysteresisAxisX);
    m_hysPosToNegRawSeries->attachAxis(m_hysteresisAxisY);
    m_hysNegToPosRawSeries->attachAxis(m_hysteresisAxisX);
    m_hysNegToPosRawSeries->attachAxis(m_hysteresisAxisY);
    m_hysCurrentCorrSeries->attachAxis(m_hysteresisAxisX);
    m_hysCurrentCorrSeries->attachAxis(m_hysteresisAxisY);
    m_hysCurrentRawSeries->attachAxis(m_hysteresisAxisX);
    m_hysCurrentRawSeries->attachAxis(m_hysteresisAxisY);
    m_hysCurrentFitSeries->attachAxis(m_hysteresisAxisX);
    m_hysCurrentFitSeries->attachAxis(m_hysteresisAxisY);

    m_commandSpeedChartView = new QChartView(m_commandSpeedChart, this);
    m_errorChartView = new QChartView(m_errorChart, this);
    m_mtCompareChartView = new QChartView(m_mtCompareChart, this);
    m_hysteresisChartView = new QChartView(m_hysteresisChart, this);
    m_commandSpeedChartView->setRenderHint(QPainter::Antialiasing);
    m_errorChartView->setRenderHint(QPainter::Antialiasing);
    m_mtCompareChartView->setRenderHint(QPainter::Antialiasing);
    m_hysteresisChartView->setRenderHint(QPainter::Antialiasing);
    root->addWidget(m_commandSpeedChartView, 1);
    root->addWidget(m_errorChartView, 1);
    root->addWidget(m_mtCompareChartView, 1);
    root->addWidget(m_hysteresisChartView, 1);
    m_hysteresisChartView->setVisible(false);
    updateErrorSeriesVisibility();

    setCentralWidget(central);
    resize(1100, 900);
    setWindowTitle(tr("jiaozhun_miniastro Encoder Monitor"));

    connect(m_startButton, &QPushButton::clicked, this, &MainWindow::startReading);
    connect(m_stopButton, &QPushButton::clicked, this, &MainWindow::stopReading);
    connect(m_clearButton, &QPushButton::clicked, this, &MainWindow::clearChartData);
    connect(m_intervalSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::updateReadInterval);
    connect(m_visibleSecondsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &MainWindow::updateVisibleSeconds);
    connect(m_showCommandSpeedChartCheckBox, &QCheckBox::toggled, this, &MainWindow::updateChartVisibility);
    connect(m_showPositionErrorChartCheckBox, &QCheckBox::toggled, this, &MainWindow::updateChartVisibility);
    connect(m_showMtCompareChartCheckBox, &QCheckBox::toggled, this, &MainWindow::updateChartVisibility);
    connect(m_showHysteresisChartCheckBox, &QCheckBox::toggled, this, &MainWindow::updateChartVisibility);
    connect(m_showTamagawaErrorCheckBox, &QCheckBox::toggled, this, &MainWindow::updateErrorSeriesVisibility);
    connect(m_showMtRawErrorCheckBox, &QCheckBox::toggled, this, &MainWindow::updateErrorSeriesVisibility);
    connect(m_showMtFilteredErrorCheckBox, &QCheckBox::toggled, this, &MainWindow::updateErrorSeriesVisibility);
    connect(m_showMtPeakMarkerCheckBox, &QCheckBox::toggled, this, &MainWindow::updateErrorSeriesVisibility);
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
    m_mountSpeedSpinBox->setRange(0.00001, 40.0);
    m_mountSpeedSpinBox->setDecimals(5);
    m_mountSpeedSpinBox->setSingleStep(0.00001);
    m_mountSpeedSpinBox->setValue(0.05942);
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
    connect(m_mountController, &MountController::responseReceived, this, &MainWindow::handleMountResponse);
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
    auto *pecToolbar = new QHBoxLayout();
    pecToolbar->setSpacing(8);
    auto *mtCalToolbar = new QHBoxLayout();
    mtCalToolbar->setSpacing(8);
    auto *backlashToolbar = new QHBoxLayout();
    backlashToolbar->setSpacing(8);

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
    m_pecCyclesSpinBox = new QSpinBox(this);
    m_pecCyclesSpinBox->setRange(1, 30);
    m_pecCyclesSpinBox->setValue(3);
    m_pecStartButton = new QPushButton(tr("PEC Train"), this);
    m_pecStopButton = new QPushButton(tr("PEC Stop"), this);
    m_pecUploadButton = new QPushButton(tr("PEC Upload"), this);
    m_pecEnableButton = new QPushButton(tr("PEC开"), this);
    m_pecDisableButton = new QPushButton(tr("PEC关"), this);
    m_pecStatusButton = new QPushButton(tr("PEC状态"), this);
    m_gotoPhaseTestButton = new QPushButton(tr("GOTO相位测试"), this);
    m_pecStopButton->setEnabled(false);
    m_pecUploadButton->setEnabled(false);
    m_mtPhasePeakBinSpinBox = new QSpinBox(this);
    m_mtPhasePeakBinSpinBox->setRange(0, PecBins - 1);
    m_mtPhasePeakBinSpinBox->setValue(0);
    m_mtPhaseScanButton = new QPushButton(tr("MT相位扫描"), this);
    m_mtPhaseStopButton = new QPushButton(tr("MT相位停止"), this);
    m_mtPhaseStopButton->setEnabled(false);
    m_mtPhaseStatusLabel = new QLabel(tr("MT phase idle"), this);
    m_mtPhaseStatusLabel->setMinimumWidth(420);
    m_pecStatusLabel = new QLabel(tr("PEC idle"), this);
    m_pecStatusLabel->setMinimumWidth(240);
    m_mtCalSpeedSpinBox = new QDoubleSpinBox(this);
    m_mtCalSpeedSpinBox->setRange(0.01, 40.0);
    m_mtCalSpeedSpinBox->setDecimals(3);
    m_mtCalSpeedSpinBox->setSingleStep(0.1);
    m_mtCalSpeedSpinBox->setValue(1.000);
    m_mtCalSpeedSpinBox->setSuffix(tr(" kHz"));
    m_mtCalStartButton = new QPushButton(tr("MT Cal"), this);
    m_mtCalStopButton = new QPushButton(tr("MT Stop"), this);
    m_mtCalUploadButton = new QPushButton(tr("MT Upload"), this);
    m_mtCalStopButton->setEnabled(false);
    m_mtCalUploadButton->setEnabled(false);
    m_mtCalStatusLabel = new QLabel(tr("MT cal idle"), this);
    m_mtCalStatusLabel->setMinimumWidth(360);
    m_mtCompareStatsLabel = new QLabel(tr("MT compare: --"), this);
    m_mtCompareStatsLabel->setMinimumWidth(760);
    m_hysteresisClearButton = new QPushButton(tr("Hys Clear"), this);
    m_hysteresisAutoButton = new QPushButton(tr("Hys Auto"), this);
    m_hysteresisAutoStopButton = new QPushButton(tr("Hys Stop"), this);
    m_hysteresisAutoStopButton->setEnabled(false);
    m_hysteresisStatusLabel = new QLabel(tr("Hys idle"), this);
    m_hysteresisStatusLabel->setMinimumWidth(260);
    m_backlashSpeedSpinBox = new QDoubleSpinBox(this);
    m_backlashSpeedSpinBox->setRange(0.01, 1.0);
    m_backlashSpeedSpinBox->setDecimals(3);
    m_backlashSpeedSpinBox->setSingleStep(0.01);
    m_backlashSpeedSpinBox->setValue(0.100);
    m_backlashSpeedSpinBox->setSuffix(tr(" kHz"));
    m_backlashCyclesSpinBox = new QSpinBox(this);
    m_backlashCyclesSpinBox->setRange(1, 50);
    m_backlashCyclesSpinBox->setValue(10);
    m_backlashSettleMsSpinBox = new QSpinBox(this);
    m_backlashSettleMsSpinBox->setRange(500, 10000);
    m_backlashSettleMsSpinBox->setSingleStep(500);
    m_backlashSettleMsSpinBox->setValue(2500);
    m_backlashSettleMsSpinBox->setSuffix(tr(" ms"));
    m_backlashStartButton = new QPushButton(tr("Backlash Test"), this);
    m_backlashStopButton = new QPushButton(tr("Backlash Stop"), this);
    m_backlashStopButton->setEnabled(false);
    m_backlashStatusLabel = new QLabel(tr("Backlash idle"), this);
    m_backlashStatusLabel->setMinimumWidth(360);

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

    pecToolbar->addWidget(new QLabel(tr("PEC cycles:"), this));
    pecToolbar->addWidget(m_pecCyclesSpinBox);
    pecToolbar->addWidget(m_pecStartButton);
    pecToolbar->addWidget(m_pecStopButton);
    pecToolbar->addWidget(m_pecUploadButton);
    pecToolbar->addWidget(m_pecEnableButton);
    pecToolbar->addWidget(m_pecDisableButton);
    pecToolbar->addWidget(m_pecStatusButton);
    pecToolbar->addWidget(m_gotoPhaseTestButton);
    pecToolbar->addWidget(new QLabel(tr("Peak bin:"), this));
    pecToolbar->addWidget(m_mtPhasePeakBinSpinBox);
    pecToolbar->addWidget(m_mtPhaseScanButton);
    pecToolbar->addWidget(m_mtPhaseStopButton);
    pecToolbar->addWidget(m_pecStatusLabel, 1);
    pecToolbar->addWidget(m_mtPhaseStatusLabel);
    root->addLayout(pecToolbar);

    mtCalToolbar->addWidget(new QLabel(tr("MT cal speed:"), this));
    mtCalToolbar->addWidget(m_mtCalSpeedSpinBox);
    mtCalToolbar->addWidget(m_mtCalStartButton);
    mtCalToolbar->addWidget(m_mtCalStopButton);
    mtCalToolbar->addWidget(m_mtCalUploadButton);
    mtCalToolbar->addWidget(m_hysteresisClearButton);
    mtCalToolbar->addWidget(m_hysteresisAutoButton);
    mtCalToolbar->addWidget(m_hysteresisAutoStopButton);
    mtCalToolbar->addWidget(m_mtCalStatusLabel, 1);
    mtCalToolbar->addWidget(m_hysteresisStatusLabel);
    root->addLayout(mtCalToolbar);
    root->addWidget(m_mtCompareStatsLabel);

    backlashToolbar->addWidget(new QLabel(tr("Backlash speed:"), this));
    backlashToolbar->addWidget(m_backlashSpeedSpinBox);
    backlashToolbar->addWidget(new QLabel(tr("Cycles:"), this));
    backlashToolbar->addWidget(m_backlashCyclesSpinBox);
    backlashToolbar->addWidget(new QLabel(tr("Settle:"), this));
    backlashToolbar->addWidget(m_backlashSettleMsSpinBox);
    backlashToolbar->addWidget(m_backlashStartButton);
    backlashToolbar->addWidget(m_backlashStopButton);
    backlashToolbar->addWidget(m_backlashStatusLabel, 1);
    root->addLayout(backlashToolbar);

    m_guideExposureTimer = new QTimer(this);
    m_guideExposureTimer->setTimerType(Qt::PreciseTimer);
    connect(m_guideExposureTimer, &QTimer::timeout, this, &MainWindow::runGuideExposure);

    m_guidePulseTimer = new QTimer(this);
    m_guidePulseTimer->setTimerType(Qt::PreciseTimer);
    m_guidePulseTimer->setSingleShot(true);
    connect(m_guidePulseTimer, &QTimer::timeout, this, &MainWindow::finishGuidePulse);

    m_backlashTimer = new QTimer(this);
    m_backlashTimer->setTimerType(Qt::PreciseTimer);
    connect(m_backlashTimer, &QTimer::timeout, this, &MainWindow::runBacklashStep);

    m_hysteresisAutoTimer = new QTimer(this);
    m_hysteresisAutoTimer->setTimerType(Qt::PreciseTimer);
    connect(m_hysteresisAutoTimer, &QTimer::timeout, this, &MainWindow::runHysteresisAutoStep);

    m_gotoPhaseTestTimer = new QTimer(this);
    m_gotoPhaseTestTimer->setTimerType(Qt::PreciseTimer);
    connect(m_gotoPhaseTestTimer, &QTimer::timeout, this, &MainWindow::runGotoPhaseTestStep);

    connect(m_guideStartButton, &QPushButton::clicked, this, &MainWindow::startGuideSimulation);
    connect(m_guideStopButton, &QPushButton::clicked, this, &MainWindow::stopGuideSimulation);
    connect(m_pecStartButton, &QPushButton::clicked, this, &MainWindow::startPecTraining);
    connect(m_pecStopButton, &QPushButton::clicked, this, &MainWindow::stopPecTraining);
    connect(m_pecUploadButton, &QPushButton::clicked, this, &MainWindow::uploadPecTable);
    connect(m_pecEnableButton, &QPushButton::clicked, this, &MainWindow::enablePecPlayback);
    connect(m_pecDisableButton, &QPushButton::clicked, this, &MainWindow::disablePecPlayback);
    connect(m_pecStatusButton, &QPushButton::clicked, this, &MainWindow::queryPecStatus);
    connect(m_gotoPhaseTestButton, &QPushButton::clicked, this, &MainWindow::startGotoPhaseTest);
    connect(m_mtPhaseScanButton, &QPushButton::clicked, this, &MainWindow::startMtPhaseScan);
    connect(m_mtPhaseStopButton, &QPushButton::clicked, this, &MainWindow::stopMtPhaseScan);
    connect(m_mtCalStartButton, &QPushButton::clicked, this, &MainWindow::startMtCalibration);
    connect(m_mtCalStopButton, &QPushButton::clicked, this, &MainWindow::stopMtCalibration);
    connect(m_mtCalUploadButton, &QPushButton::clicked, this, &MainWindow::uploadMtCalibration);
    connect(m_hysteresisClearButton, &QPushButton::clicked, this, &MainWindow::clearHysteresisData);
    connect(m_hysteresisAutoButton, &QPushButton::clicked, this, &MainWindow::startHysteresisAutoTest);
    connect(m_hysteresisAutoStopButton, &QPushButton::clicked, this, &MainWindow::stopHysteresisAutoTest);
    connect(m_backlashStartButton, &QPushButton::clicked, this, &MainWindow::startBacklashTest);
    connect(m_backlashStopButton, &QPushButton::clicked, this, &MainWindow::stopBacklashTest);
    connect(m_mountController, &MountController::connectionChanged, this, [this](bool connected) {
        if (!connected) {
            stopGuideSimulation();
            stopHysteresisAutoTest();
            if (m_gotoPhaseTestTimer)
                m_gotoPhaseTestTimer->stop();
            m_gotoPhaseTestState = GotoPhaseTestState::Idle;
            if (m_gotoPhaseTestButton)
                m_gotoPhaseTestButton->setEnabled(true);
        }
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

void MainWindow::clearChartData()
{
    resetChart();
    if (m_elapsed.isValid())
        m_elapsed.restart();
    updateStatus(tr("Chart and position error cleared"));
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
        updateYAxisForVisibleRange(m_commandSpeedSeries, m_commandSpeedAxisY, minVisibleSeconds, 1.0);
        updateErrorYAxisForVisibleRange(minVisibleSeconds);
        updateMtCompareYAxis(minVisibleSeconds);
    }
    else
        setChartXRange(0.0, m_visibleSeconds);
}

void MainWindow::updateChartVisibility()
{
    const bool showSpeed = m_showCommandSpeedChartCheckBox && m_showCommandSpeedChartCheckBox->isChecked();
    const bool showError = m_showPositionErrorChartCheckBox && m_showPositionErrorChartCheckBox->isChecked();
    bool showMt = m_showMtCompareChartCheckBox && m_showMtCompareChartCheckBox->isChecked();
    const bool showHys = m_showHysteresisChartCheckBox && m_showHysteresisChartCheckBox->isChecked();

    if (!showSpeed && !showError && !showMt && !showHys && m_showMtCompareChartCheckBox) {
        QSignalBlocker blocker(m_showMtCompareChartCheckBox);
        m_showMtCompareChartCheckBox->setChecked(true);
        showMt = true;
    }

    if (m_commandSpeedChartView)
        m_commandSpeedChartView->setVisible(showSpeed);
    if (m_errorChartView)
        m_errorChartView->setVisible(showError);
    if (m_mtCompareChartView)
        m_mtCompareChartView->setVisible(showMt);
    if (m_hysteresisChartView)
        m_hysteresisChartView->setVisible(showHys);
}

void MainWindow::updateErrorSeriesVisibility()
{
    if (m_positionErrorSeries)
        m_positionErrorSeries->setVisible(!m_showTamagawaErrorCheckBox || m_showTamagawaErrorCheckBox->isChecked());
    if (m_mtRawOffsetPositionErrorSeries)
        m_mtRawOffsetPositionErrorSeries->setVisible(!m_showMtRawErrorCheckBox || m_showMtRawErrorCheckBox->isChecked());
    if (m_mtPhaseFilteredSeries)
        m_mtPhaseFilteredSeries->setVisible(!m_showMtFilteredErrorCheckBox || m_showMtFilteredErrorCheckBox->isChecked());
    if (m_mtPhasePeakMarkerSeries)
        m_mtPhasePeakMarkerSeries->setVisible(!m_showMtPeakMarkerCheckBox || m_showMtPeakMarkerCheckBox->isChecked());

    const double currentSeconds = m_elapsed.isValid()
            ? static_cast<double>(m_elapsed.elapsed()) / 1000.0
            : 0.0;
    const double minVisibleSeconds = qMax(0.0, currentSeconds - m_visibleSeconds);
    updateErrorYAxisForVisibleRange(minVisibleSeconds);
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

void MainWindow::handleMountResponse(const QString &line)
{
    m_mountResponses.append(line);
    while (m_mountResponses.size() > 200)
        m_mountResponses.removeFirst();

    if (line.startsWith(QStringLiteral("PEC:STATUS")) || line.startsWith(QStringLiteral("PEC:EN"))) {
        static const QRegularExpression pecStatusRe(QStringLiteral("(?:^|,)en=(\\d+)"));
        static const QRegularExpression pecEnRe(QStringLiteral("^PEC:EN,(\\d+)"));
        QRegularExpressionMatch match = pecStatusRe.match(line);
        if (!match.hasMatch())
            match = pecEnRe.match(line);
        if (match.hasMatch()) {
            m_pecPlaybackKnown = true;
            m_pecPlaybackEnabled = match.captured(1).toInt() != 0;
            if (m_pecStatusLabel)
                m_pecStatusLabel->setText(m_pecPlaybackEnabled ? tr("PEC enabled") : tr("PEC disabled"));
        }
    }

    handleCalStatusResponse(line);
    handleMtRawResponse(line);
    handleMtMonitorRawResponse(line);
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

    if (m_mountController->connectToPort(portName, m_mountBaudSpinBox->value())) {
        m_mountResponses.clear();
        m_mountController->sendCommand(QStringLiteral("CAL:STATUS"));
        m_mountController->sendCommand(QStringLiteral("PEC:STATUS"));
    }
}

void MainWindow::disconnectMount()
{
    m_mountController->disconnectFromPort();
    setDecSpeedState(0.0, 0.0);
    m_pecPlaybackKnown = false;
    m_pecPlaybackEnabled = false;
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
    m_mtRawOffsetPositionErrorSeries->clear();
    m_hasPreviousMtErrorSample = false;
    m_previousMtRawOffset25 = 0;
    m_previousMtErrorElapsedMs = 0;
    m_mtRawOffsetCumulativePositionErrorCounts = 0.0;
    m_mtRawOffsetPositionErrorArcsec = 0.0;
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

void MainWindow::startHysteresisAutoTest()
{
    if (!m_mountController->isConnected()) {
        updateMountStatus(tr("Connect mount serial first"));
        return;
    }
    if (!m_worker) {
        updateMountStatus(tr("Start encoder reading first"));
        return;
    }
    if (m_mtCalTraining || m_mtUploadActive) {
        updateMountStatus(tr("Stop MT calibration/upload before Hys Auto"));
        return;
    }

    stopGuideSimulation();
    if (m_backlashActive)
        stopBacklashTest();

    clearHysteresisData();
    if (m_showHysteresisChartCheckBox) {
        m_showHysteresisChartCheckBox->setChecked(true);
        updateChartVisibility();
    }

    m_hysAutoSpeedKHz = qAbs(selectedMountSpeedKHz());
    if (m_hysAutoSpeedKHz < 0.001)
        m_hysAutoSpeedKHz = 1.0;
    m_hysAutoRecords.clear();
    m_hysAutoSegmentIndex = 0;
    m_hysAutoActive = true;
    if (m_hysteresisAutoButton)
        m_hysteresisAutoButton->setEnabled(false);
    if (m_hysteresisAutoStopButton)
        m_hysteresisAutoStopButton->setEnabled(true);

    updateMountStatus(tr("Hys Auto started: %1 cycles, %2 min/segment, speed %3 kHz")
                      .arg(HysAutoCycles)
                      .arg(HysAutoSegmentMs / 60000)
                      .arg(m_hysAutoSpeedKHz, 0, 'f', 3));
    if (!beginHysteresisAutoSegment(-1))
        stopHysteresisAutoTest();
}

void MainWindow::stopHysteresisAutoTest()
{
    if (!m_hysAutoActive)
        return;

    recordHysteresisAutoSegment(m_hysAutoCurrentDir);
    finishHysteresisTransition();

    m_hysAutoActive = false;
    if (m_hysteresisAutoTimer)
        m_hysteresisAutoTimer->stop();
    if (m_mountController->isConnected())
        m_mountController->stopDec();
    setDecSpeedState(0.0, 0.0);
    if (m_hysteresisAutoButton)
        m_hysteresisAutoButton->setEnabled(true);
    if (m_hysteresisAutoStopButton)
        m_hysteresisAutoStopButton->setEnabled(false);

    exportHysteresisAutoReport();
    updateHysteresisStatus();
}

bool MainWindow::beginHysteresisAutoSegment(int dir)
{
    if (!m_hysAutoActive || !m_mountController->isConnected())
        return false;

    resetHysteresisCurrentTransition();
    m_hysAutoCurrentDir = dir >= 0 ? 1 : -1;
    m_hysCurrentTransitionDir = 0;
    m_hysTransitionDir = 0;
    m_hysStepsSinceReverse = 0.0;
    m_hysHaveStartError = false;
    m_hysAutoSegmentStartMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;

    const double speedKHz = m_hysAutoSpeedKHz * static_cast<double>(m_hysAutoCurrentDir);
    if (!sendGuideSpeed(speedKHz, speedKHz)) {
        updateMountStatus(tr("Hys Auto speed command failed"));
        return false;
    }

    if (m_hysteresisAutoTimer)
        m_hysteresisAutoTimer->start(1000);

    const QString dirText = m_hysAutoCurrentDir < 0 ? tr("reverse") : tr("forward");
    if (m_hysteresisStatusLabel)
        m_hysteresisStatusLabel->setText(tr("Hys Auto segment %1/%2 %3 started")
                                         .arg(m_hysAutoSegmentIndex + 1)
                                         .arg(HysAutoSegments)
                                         .arg(dirText));
    return true;
}

void MainWindow::runHysteresisAutoStep()
{
    if (!m_hysAutoActive)
        return;

    const qint64 nowMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    const qint64 elapsedMs = nowMs - m_hysAutoSegmentStartMs;
    const int remainSeconds = qMax<qint64>(0, (HysAutoSegmentMs - elapsedMs + 999) / 1000);
    if (m_hysteresisStatusLabel) {
        const QString measuredText = m_hysCurrentTransitionDir == 0
                ? QStringLiteral("wait")
                : (m_hysCurrentTransitionDir < 0 ? QStringLiteral("-") : QStringLiteral("+"));
        m_hysteresisStatusLabel->setText(
                    tr("Hys Auto %1/%2 cmd=%3 meas=%4 elapsed=%5s left=%6s step=%7 samples=%8")
                    .arg(m_hysAutoSegmentIndex + 1)
                    .arg(HysAutoSegments)
                    .arg(m_hysAutoCurrentDir < 0 ? "-" : "+")
                    .arg(measuredText)
                    .arg(qMax<qint64>(0, elapsedMs / 1000))
                    .arg(remainSeconds)
                    .arg(m_hysStepsSinceReverse, 0, 'f', 0)
                    .arg(m_hysCurrentPoints.size()));
    }

    if (elapsedMs < HysAutoSegmentMs)
        return;

    recordHysteresisAutoSegment(m_hysAutoCurrentDir);
    finishHysteresisTransition();

    ++m_hysAutoSegmentIndex;
    if (m_hysAutoSegmentIndex >= HysAutoSegments) {
        m_hysAutoActive = false;
        if (m_hysteresisAutoTimer)
            m_hysteresisAutoTimer->stop();
        if (m_mountController->isConnected())
            m_mountController->stopDec();
        setDecSpeedState(0.0, 0.0);
        if (m_hysteresisAutoButton)
            m_hysteresisAutoButton->setEnabled(true);
        if (m_hysteresisAutoStopButton)
            m_hysteresisAutoStopButton->setEnabled(false);
        exportHysteresisAutoReport();
        updateHysteresisStatus();
        return;
    }

    const int nextDir = (m_hysAutoSegmentIndex % 2 == 0) ? -1 : 1;
    beginHysteresisAutoSegment(nextDir);
}

void MainWindow::appendSample(const EncoderSample &sample)
{
    const double seconds = static_cast<double>(sample.elapsedMs) / 1000.0;
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

    processBacklashSample(sample, actualSpeedHz);
    processMtCalSample(sample);
    if (!m_pecTraining)
        updateHysteresisTracking(sample);
    requestMtMonitorSample(sample);

    m_hasPreviousDerivedSample = true;
    m_previousDec = sample.dec;
    m_previousElapsedMs = sample.elapsedMs;

    m_positionErrorSeries->append(seconds, errorArcsec);

    if (m_pecTraining && m_referenceDecSpeedHz != 0.0) {
        const double trainRefHz = qAbs(m_pecTrainRefSpeedHz) > 0.0 ? m_pecTrainRefSpeedHz : m_referenceDecSpeedHz;
        const double absRefHz = qAbs(trainRefHz);
        const qint64 nowMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;

        if (m_pecTrainState == PecTrainState::Prescan) {
            const double stepsMoved = qMax(0.0, seconds - m_pecPrescanStartSeconds) * absRefHz;
            const int phaseStep = static_cast<int>(std::fmod(stepsMoved, static_cast<double>(PecPeriodSteps)));
            const int bin = qBound(0,
                                   static_cast<int>(phaseStep * static_cast<double>(PecBins) / PecPeriodSteps),
                                   PecBins - 1);
            m_pecCurrentBin = bin;
            if (m_pecPrescanErrorSum.size() == PecBins && m_pecPrescanCount.size() == PecBins) {
                m_pecPrescanErrorSum[bin] += errorArcsec;
                ++m_pecPrescanCount[bin];
            }
            m_pecPrescanLastPhaseStep = phaseStep;

            if (stepsMoved >= static_cast<double>(PecPeriodSteps)) {
                double peakBinFloat = 0.0;
                double peakToPeak = 0.0;
                if (analyzePecPrescanPeak(&m_pecPrescanPeakBin, &peakBinFloat, &peakToPeak)) {
                    const double peakSteps = peakBinFloat / static_cast<double>(PecBins)
                            * static_cast<double>(PecPeriodSteps);
                    m_pecAlignTargetSteps = static_cast<double>(PecPeriodSteps) + peakSteps;
                    m_pecTrainState = PecTrainState::AlignWait;
                    if (m_pecStatusLabel)
                        m_pecStatusLabel->setText(tr("PEC align peak bin %1 (%2), pp %3\", waiting next peak")
                                                  .arg(m_pecPrescanPeakBin)
                                                  .arg(peakBinFloat, 0, 'f', 1)
                                                  .arg(peakToPeak, 0, 'f', 1));
                } else {
                    updateGuideStatus(tr("PEC prescan failed to find peak"));
                    stopPecTraining();
                    return;
                }
            } else if (nowMs - m_pecLastStatusUpdateMs >= 1000) {
                m_pecLastStatusUpdateMs = nowMs;
                updatePecStatus();
            }
        } else if (m_pecTrainState == PecTrainState::AlignWait) {
            const double stepsMoved = qMax(0.0, seconds - m_pecPrescanStartSeconds) * absRefHz;
            m_pecCurrentBin = qBound(0,
                                     static_cast<int>(std::fmod(stepsMoved, static_cast<double>(PecPeriodSteps))
                                                      * static_cast<double>(PecBins) / PecPeriodSteps),
                                     PecBins - 1);
            if (stepsMoved >= m_pecAlignTargetSteps) {
                beginFormalPecTraining(seconds);
            } else if (nowMs - m_pecLastStatusUpdateMs >= 1000) {
                m_pecLastStatusUpdateMs = nowMs;
                updatePecStatus();
            }
        } else if (m_pecTrainState == PecTrainState::Training) {
            const double stepsMoved = qMax(0.0, seconds - m_pecStartSeconds) * absRefHz;
            const int phaseStep = static_cast<int>(std::fmod(stepsMoved, static_cast<double>(PecPeriodSteps)));
            if (phaseStep < m_pecLastPhaseStep)
                finalizePecCycle(false);
            m_pecLastPhaseStep = phaseStep;

            const int bin = qBound(0,
                                   static_cast<int>(phaseStep * static_cast<double>(PecBins) / PecPeriodSteps),
                                   PecBins - 1);
            m_pecCurrentBin = bin;
            if (m_pecCycleErrorSum.size() == PecBins && m_pecCycleCount.size() == PecBins) {
                m_pecCycleErrorSum[bin] += errorArcsec;
                ++m_pecCycleCount[bin];
            }

            if (nowMs - m_pecLastStatusUpdateMs >= 1000 || m_pecCyclesDone >= m_pecCyclesSpinBox->value()) {
                m_pecLastStatusUpdateMs = nowMs;
                updatePecStatus();
            }
            if (m_pecCyclesDone >= m_pecCyclesSpinBox->value())
                stopPecTraining();
        }
    }

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
        pruneSeries(m_commandSpeedSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_positionErrorSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_mtRawOffsetPositionErrorSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_tamaDegreeSeries, minVisibleSeconds, maxSamples);
        pruneSeries(m_mtRawOffsetDegreeSeries, minVisibleSeconds, maxSamples);
        setChartXRange(minVisibleSeconds, seconds);
    } else {
        pruneSeries(m_commandSpeedSeries, 0.0, maxSamples);
        pruneSeries(m_positionErrorSeries, 0.0, maxSamples);
        pruneSeries(m_mtRawOffsetPositionErrorSeries, 0.0, maxSamples);
        pruneSeries(m_tamaDegreeSeries, 0.0, maxSamples);
        pruneSeries(m_mtRawOffsetDegreeSeries, 0.0, maxSamples);
        setChartXRange(0.0, m_visibleSeconds);
    }

    if (m_lastChartAxisUpdateMs < 0 || sample.elapsedMs - m_lastChartAxisUpdateMs >= ChartAxisUpdateIntervalMs) {
        updateYAxisForVisibleRange(m_commandSpeedSeries, m_commandSpeedAxisY, minVisibleSeconds, 1.0);
        updateErrorYAxisForVisibleRange(minVisibleSeconds);
        updateMtCompareYAxis(minVisibleSeconds);
        m_lastChartAxisUpdateMs = sample.elapsedMs;
    }
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

void MainWindow::updateErrorYAxisForVisibleRange(double minVisibleSeconds)
{
    if (!m_errorAxisY)
        return;

    bool hasVisiblePoint = false;
    double minY = 0.0;
    double maxY = 0.0;
    const QXYSeries *seriesList[] = {
        m_positionErrorSeries,
        m_mtRawOffsetPositionErrorSeries,
        m_mtPhaseFilteredSeries,
        m_mtPhasePeakMarkerSeries,
    };

    for (const QXYSeries *series : seriesList) {
        if (!series || !series->isVisible() || series->count() == 0)
            continue;
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
    }

    if (!hasVisiblePoint) {
        m_errorAxisY->setRange(0.0, 1.0);
        return;
    }

    const double padding = qMax(0.1, (maxY - minY) * 0.08);
    m_errorAxisY->setRange(minY - padding, maxY + padding);
}

void MainWindow::updateMtCompareYAxis(double minVisibleSeconds)
{
    if (!m_mtCompareAxisY)
        return;

    const QLineSeries *seriesList[] = {
        m_tamaDegreeSeries,
        m_mtRawOffsetDegreeSeries,
    };

    bool hasVisiblePoint = false;
    double minY = 0.0;
    double maxY = 0.0;
    for (const QLineSeries *series : seriesList) {
        if (!series)
            continue;
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
    }

    if (!hasVisiblePoint) {
        m_mtCompareAxisY->setRange(0.0, 360.0);
        return;
    }

    const double padding = qMax(0.01, (maxY - minY) * 0.08);
    m_mtCompareAxisY->setRange(minY - padding, maxY + padding);
}

void MainWindow::updateMtCompareStatsLabel()
{
    if (!m_mtCompareStatsLabel)
        return;

    struct Stats {
        int n = 0;
        double bias = 0.0;
        double rms = 0.0;
        double peakToPeak = 0.0;
        double debiasedRms = 0.0;
        double debiasedPeakToPeak = 0.0;
        double slopeArcsecPerSecond = 0.0;
        double detrendedRms = 0.0;
        double detrendedPeakToPeak = 0.0;
    };

    const auto computeStats = [](const QVector<QPointF> &samples) {
        Stats stats;
        stats.n = samples.size();
        if (samples.isEmpty())
            return stats;

        double sum = 0.0;
        double sumSq = 0.0;
        double minValue = samples.first().y();
        double maxValue = samples.first().y();
        for (const QPointF &sample : samples) {
            const double value = sample.y();
            sum += value;
            sumSq += value * value;
            minValue = qMin(minValue, value);
            maxValue = qMax(maxValue, value);
        }

        stats.bias = sum / static_cast<double>(samples.size());
        stats.rms = std::sqrt(sumSq / static_cast<double>(samples.size()));
        stats.peakToPeak = maxValue - minValue;

        double debiasedSumSq = 0.0;
        double minDebiased = samples.first().y() - stats.bias;
        double maxDebiased = minDebiased;
        for (const QPointF &sample : samples) {
            const double value = sample.y() - stats.bias;
            debiasedSumSq += value * value;
            minDebiased = qMin(minDebiased, value);
            maxDebiased = qMax(maxDebiased, value);
        }
        stats.debiasedRms = std::sqrt(debiasedSumSq / static_cast<double>(samples.size()));
        stats.debiasedPeakToPeak = maxDebiased - minDebiased;

        if (samples.size() >= 2) {
            const double x0 = samples.first().x();
            double sumX = 0.0;
            double sumXX = 0.0;
            double sumXY = 0.0;
            for (const QPointF &sample : samples) {
                const double x = sample.x() - x0;
                const double y = sample.y();
                sumX += x;
                sumXX += x * x;
                sumXY += x * y;
            }
            const double n = static_cast<double>(samples.size());
            const double denom = n * sumXX - sumX * sumX;
            double intercept = stats.bias;
            if (qAbs(denom) > 1e-9) {
                stats.slopeArcsecPerSecond = (n * sumXY - sumX * sum) / denom;
                intercept = (sum - stats.slopeArcsecPerSecond * sumX) / n;
            }

            double detrendedSumSq = 0.0;
            double minDetrended = samples.first().y() - intercept;
            double maxDetrended = minDetrended;
            for (const QPointF &sample : samples) {
                const double x = sample.x() - x0;
                const double value = sample.y() - (intercept + stats.slopeArcsecPerSecond * x);
                detrendedSumSq += value * value;
                minDetrended = qMin(minDetrended, value);
                maxDetrended = qMax(maxDetrended, value);
            }
            stats.detrendedRms = std::sqrt(detrendedSumSq / n);
            stats.detrendedPeakToPeak = maxDetrended - minDetrended;
        }
        return stats;
    };

    const Stats raw = computeStats(m_mtRawOffsetErrorArcsecSamples);
    if (raw.n == 0) {
        m_mtCompareStatsLabel->setText(tr("MT compare: --"));
        return;
    }

    m_mtCompareStatsLabel->setText(
                tr("MT compare n=%1 | raw+off rms=%2\" pp=%3\" bias=%4\" slope=%5\"/s detr=%6\"/%7\"")
                .arg(raw.n)
                .arg(raw.rms, 0, 'f', 2)
                .arg(raw.peakToPeak, 0, 'f', 2)
                .arg(raw.bias, 0, 'f', 2)
                .arg(raw.slopeArcsecPerSecond, 0, 'f', 2)
                .arg(raw.detrendedRms, 0, 'f', 2)
                .arg(raw.detrendedPeakToPeak, 0, 'f', 2));
}

void MainWindow::resetChart()
{
    m_commandSpeedSeries->clear();
    m_positionErrorSeries->clear();
    m_mtRawOffsetPositionErrorSeries->clear();
    m_mtPhaseFilteredSeries->clear();
    m_mtPhasePeakMarkerSeries->clear();
    m_tamaDegreeSeries->clear();
    m_mtRawOffsetDegreeSeries->clear();
    m_mtRawOffsetErrorArcsecSamples.clear();
    m_hasPreviousDerivedSample = false;
    m_previousDec = 0;
    m_previousElapsedMs = 0;
    m_mtMonitorRawPending = false;
    m_mtMonitorLastRequestMs = -1;
    m_mtCompareLastStatsUpdateMs = -1;
    m_lastChartAxisUpdateMs = -1;
    resetHysteresisCurrentTransition();
    m_cumulativePositionErrorCounts = 0.0;
    m_signedPositionErrorArcsec = 0.0;
    m_hasPreviousMtErrorSample = false;
    m_previousMtRawOffset25 = 0;
    m_previousMtErrorElapsedMs = 0;
    m_mtRawOffsetCumulativePositionErrorCounts = 0.0;
    m_mtRawOffsetPositionErrorArcsec = 0.0;
    setChartXRange(0.0, m_visibleSeconds);
    m_commandSpeedAxisY->setRange(-1.0, 1.0);
    m_errorAxisY->setRange(0.0, 1.0);
    m_mtCompareAxisY->setRange(0.0, 360.0);
    if (m_commandSpeedLabel)
        m_commandSpeedLabel->setText(tr("Command speed: --"));
    if (m_actualSpeedLabel)
        m_actualSpeedLabel->setText(tr("Actual speed: --"));
    if (m_positionErrorLabel)
        m_positionErrorLabel->setText(tr("Position error: --"));
    if (m_mtCompareStatsLabel)
        m_mtCompareStatsLabel->setText(tr("MT compare: --"));
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

void MainWindow::startPecTraining()
{
    if (!m_elapsed.isValid() || !m_worker) {
        updateGuideStatus(tr("Start encoder reading before PEC training"));
        return;
    }

    if (qAbs(m_referenceDecSpeedHz) < 1.0) {
        updateGuideStatus(tr("Start mount tracking before PEC training"));
        return;
    }

    resetPecTraining();
    resetHysteresisCurrentTransition();
    m_mtMonitorRawPending = false;
    m_mtMonitorLastRequestMs = -1;
    m_pecTraining = true;
    m_pecTrainState = PecTrainState::Prescan;
    m_pecTableReady = false;
    m_pecPrescanStartSeconds = static_cast<double>(m_elapsed.elapsed()) / 1000.0;
    m_pecStartSeconds = 0.0;
    m_pecTrainRefSpeedHz = m_referenceDecSpeedHz;
    m_pecLastStatusUpdateMs = 0;
    m_pecStartButton->setEnabled(false);
    m_pecStopButton->setEnabled(true);
    m_pecUploadButton->setEnabled(false);
    updatePecStatus();
}

void MainWindow::stopPecTraining()
{
    if (!m_pecTraining && m_pecErrorSum.isEmpty())
        return;

    const bool wasFormalTraining = m_pecTraining && m_pecTrainState == PecTrainState::Training;
    if (wasFormalTraining)
        finalizePecCycle(true);
    m_pecTraining = false;
    m_pecTrainState = PecTrainState::Idle;
    m_pecStopButton->setEnabled(false);
    m_pecStartButton->setEnabled(true);
    m_pecTableReady = wasFormalTraining && buildPecTrimTable();
    m_pecUploadButton->setEnabled(m_pecTableReady);
    updatePecStatus();
}

void MainWindow::enablePecPlayback()
{
    if (!m_mountController->isConnected()) {
        updateMountStatus(tr("Connect mount serial first"));
        return;
    }
    if (m_mountController->sendCommand(QStringLiteral("PEC:ENABLE,1"))) {
        m_mountController->sendCommand(QStringLiteral("PEC:STATUS"));
        updateMountStatus(tr("PEC enable requested"));
    }
}

void MainWindow::disablePecPlayback()
{
    if (!m_mountController->isConnected()) {
        updateMountStatus(tr("Connect mount serial first"));
        return;
    }
    if (m_mountController->sendCommand(QStringLiteral("PEC:DISABLE"))) {
        m_mountController->sendCommand(QStringLiteral("PEC:STATUS"));
        updateMountStatus(tr("PEC disable requested"));
    }
}

void MainWindow::queryPecStatus()
{
    if (!m_mountController->isConnected()) {
        updateMountStatus(tr("Connect mount serial first"));
        return;
    }
    if (m_mountController->sendCommand(QStringLiteral("PEC:STATUS")))
        updateMountStatus(tr("Querying PEC status"));
}

void MainWindow::startGotoPhaseTest()
{
    if (!m_mountController->isConnected()) {
        updateMountStatus(tr("Connect mount serial first"));
        return;
    }
    if (!m_worker) {
        updateMountStatus(tr("Start encoder reading first"));
        return;
    }
    if (m_mtPhaseScanActive || m_pecTraining || m_mtCalTraining || m_backlashActive || m_hysAutoActive) {
        updateMountStatus(tr("Stop active test/training before GOTO phase test"));
        return;
    }

    stopGuideSimulation();
    m_gotoPhaseTestLog.clear();
    m_gotoPhaseTestPolls = 0;
    m_gotoPhaseTestState = GotoPhaseTestState::FirstMove;
    if (m_gotoPhaseTestButton)
        m_gotoPhaseTestButton->setEnabled(false);
    if (m_mtPhaseStatusLabel)
        m_mtPhaseStatusLabel->setText(tr("GOTO phase test starting"));
    runGotoPhaseTestStep();
}

void MainWindow::runGotoPhaseTestStep()
{
    if (m_gotoPhaseTestState == GotoPhaseTestState::Idle)
        return;

    auto finishWithError = [this](const QString &message) {
        if (m_gotoPhaseTestTimer)
            m_gotoPhaseTestTimer->stop();
        if (m_mountController->isConnected())
            m_mountController->stopDec();
        setDecSpeedState(0.0, 0.0);
        m_gotoPhaseTestState = GotoPhaseTestState::Idle;
        if (m_gotoPhaseTestButton)
            m_gotoPhaseTestButton->setEnabled(true);
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(message);
        updateMountStatus(message);
    };

    auto statusSummary = [this](const QString &tag) -> QString {
        m_mountResponses.clear();
        if (!m_mountController->sendCommand(QStringLiteral("PEC:STATUS"))
                || !waitForMountResponse(QStringLiteral("PEC:STATUS"), 1500))
            return QStringLiteral("%1 status fail").arg(tag);

        static const QRegularExpression phaseRe(QStringLiteral("(?:^|,)phase=(\\d+)"));
        static const QRegularExpression idxRe(QStringLiteral("(?:^|,)idx=(\\d+)"));
        for (int i = m_mountResponses.size() - 1; i >= 0; --i) {
            const QString &line = m_mountResponses.at(i);
            if (!line.startsWith(QStringLiteral("PEC:STATUS")))
                continue;
            const auto phaseMatch = phaseRe.match(line);
            const auto idxMatch = idxRe.match(line);
            if (phaseMatch.hasMatch() && idxMatch.hasMatch()) {
                return QStringLiteral("%1 idx=%2 phase=%3")
                        .arg(tag)
                        .arg(idxMatch.captured(1))
                        .arg(phaseMatch.captured(1));
            }
        }
        return QStringLiteral("%1 status parse fail").arg(tag);
    };

    auto waitForStepDone = [this]() -> bool {
        for (int i = m_mountResponses.size() - 1; i >= 0; --i) {
            if (m_mountResponses.at(i).startsWith(QStringLiteral("STEPTEST:DONE")))
                return true;
        }
        return false;
    };

    switch (m_gotoPhaseTestState) {
    case GotoPhaseTestState::FirstMove: {
        m_gotoPhaseTestLog.append(statusSummary(QStringLiteral("before")));
        m_mountResponses.clear();
        if (!m_mountController->sendCommand(QStringLiteral("STEPTEST:MOVE,-150000,5.000"))) {
            finishWithError(tr("GOTO phase test failed to start -150000"));
            return;
        }
        m_gotoPhaseTestPolls = 0;
        m_gotoPhaseTestState = GotoPhaseTestState::WaitFirstDone;
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(tr("GOTO phase test moving -150000 steps"));
        m_gotoPhaseTestTimer->start(500);
        break;
    }
    case GotoPhaseTestState::WaitFirstDone: {
        ++m_gotoPhaseTestPolls;
        if (!waitForStepDone()) {
            if ((m_gotoPhaseTestPolls % 4) == 0)
                m_mountController->sendCommand(QStringLiteral("STEPTEST:STATUS"));
            if (m_gotoPhaseTestPolls > 180) {
                finishWithError(tr("GOTO phase test timeout on -150000"));
                return;
            }
            break;
        }
        m_gotoPhaseTestLog.append(statusSummary(QStringLiteral("after -150000")));
        m_mountResponses.clear();
        m_gotoPhaseTestState = GotoPhaseTestState::SecondMove;
        runGotoPhaseTestStep();
        break;
    }
    case GotoPhaseTestState::SecondMove: {
        if (!m_mountController->sendCommand(QStringLiteral("STEPTEST:MOVE,150000,5.000"))) {
            finishWithError(tr("GOTO phase test failed to start +150000"));
            return;
        }
        m_gotoPhaseTestPolls = 0;
        m_gotoPhaseTestState = GotoPhaseTestState::WaitSecondDone;
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(tr("GOTO phase test moving +150000 steps"));
        m_gotoPhaseTestTimer->start(500);
        break;
    }
    case GotoPhaseTestState::WaitSecondDone: {
        ++m_gotoPhaseTestPolls;
        if (!waitForStepDone()) {
            if ((m_gotoPhaseTestPolls % 4) == 0)
                m_mountController->sendCommand(QStringLiteral("STEPTEST:STATUS"));
            if (m_gotoPhaseTestPolls > 180) {
                finishWithError(tr("GOTO phase test timeout on +150000"));
                return;
            }
            break;
        }
        m_gotoPhaseTestLog.append(statusSummary(QStringLiteral("after +150000")));
        m_gotoPhaseTestState = GotoPhaseTestState::StartMtScan;
        runGotoPhaseTestStep();
        break;
    }
    case GotoPhaseTestState::StartMtScan: {
        if (m_gotoPhaseTestTimer)
            m_gotoPhaseTestTimer->stop();
        const QString summary = m_gotoPhaseTestLog.join(QStringLiteral(" | "));
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(tr("GOTO phase test done: %1; starting MT scan").arg(summary));
        updateMountStatus(tr("GOTO phase test done; starting MT phase scan"));
        m_gotoPhaseTestState = GotoPhaseTestState::Idle;
        if (m_gotoPhaseTestButton)
            m_gotoPhaseTestButton->setEnabled(true);
        startMtPhaseScan();
        break;
    }
    case GotoPhaseTestState::Idle:
        break;
    }
}

void MainWindow::startMtPhaseScan()
{
    if (!m_mountController->isConnected()) {
        updateMountStatus(tr("Connect mount serial first"));
        return;
    }
    if (!m_worker) {
        updateMountStatus(tr("Start encoder reading first"));
        return;
    }
    if (m_mtCalTraining || m_mtUploadActive || m_pecTraining) {
        updateMountStatus(tr("Stop MT/PEC training before MT phase scan"));
        return;
    }

    int refIdx = 0;
    bool refEnabled = false;
    const bool refOk = queryPecStatusSnapshot(&refIdx, &refEnabled);

    stopGuideSimulation();
    if (m_backlashActive)
        stopBacklashTest();
    if (m_hysAutoActive)
        stopHysteresisAutoTest();

    m_mtPhaseScanSamples.clear();
    m_mtPhaseScanActive = true;
    m_mtPhaseScanStartMs = -1;
    m_mtPhaseScanLastStatusMs = -1;
    m_mtPhaseScanSpeedHz = MtPhaseScanSpeedKHz * 1000.0;
    m_mtPhaseRefValid = refOk;
    m_mtPhaseRefIdx = refIdx;
    m_mtPhaseRestorePecEnabled = refOk && refEnabled;
    m_hasPreviousMtErrorSample = false;
    m_mtMonitorRawPending = false;
    m_mtMonitorLastRequestMs = -1;
    m_mtRawOffsetCumulativePositionErrorCounts = 0.0;
    m_mtRawOffsetPositionErrorArcsec = 0.0;

    if (m_mtPhaseScanButton)
        m_mtPhaseScanButton->setEnabled(false);
    if (m_mtPhaseStopButton)
        m_mtPhaseStopButton->setEnabled(true);

    m_mountController->sendCommand(QStringLiteral("PEC:DISABLE"));
    m_mountController->sendCommand(QStringLiteral("PEC:STATUS"));

    if (sendGuideSpeed(MtPhaseScanSpeedKHz, MtPhaseScanSpeedKHz)) {
        const double periodSeconds = static_cast<double>(PecPeriodSteps) / qAbs(m_mtPhaseScanSpeedHz);
        const double scanCycles = static_cast<double>(MtPhaseScanDurationMs) / 1000.0 / periodSeconds;
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(tr("MT phase verify 0/%1s at %2 kHz, ref_idx=%3, period=%4s cycles=%5")
                                          .arg(MtPhaseScanDurationMs / 1000)
                                          .arg(MtPhaseScanSpeedKHz, 0, 'f', 3)
                                          .arg(refOk ? QString::number(refIdx) : QStringLiteral("--"))
                                          .arg(periodSeconds, 0, 'f', 1)
                                          .arg(scanCycles, 0, 'f', 2));
        updateMountStatus(refOk ? tr("MT phase verify started")
                                : tr("MT phase verify started without PEC idx snapshot"));
    } else {
        finishMtPhaseScan(true);
    }
}

void MainWindow::stopMtPhaseScan()
{
    finishMtPhaseScan(true);
}

void MainWindow::startMtCalibration()
{
    if (!m_mountController->isConnected()) {
        updateMtCalStatus(tr("Connect mount serial first"));
        return;
    }

    if (!m_worker) {
        updateMtCalStatus(tr("Start encoder reading first"));
        return;
    }

    stopGuideSimulation();
    m_mtCalRaw21.clear();
    m_mtCalTama25.clear();
    m_mtCalLut.clear();
    m_mtCalTableReady = false;
    m_mtCalRawPending = false;
    m_mtMonitorRawPending = false;
    m_mtCalPendingTama25 = 0;
    m_mtCalPendingElapsedMs = 0;
    m_mtCalLastRequestMs = -1;
    m_mtCalStartMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    m_mtCalHaveLastTama = false;
    m_mtCalLastTama25 = 0;
    m_mtCalActualSteps = 0.0;
    m_mtCalRawTimeouts = 0;
    m_mtCalRawCoverage = QVector<bool>(MtCalBins, false);
    m_mtCalRawCoverageCount = 0;
    m_mtCalHaveLastRawBin = false;
    m_mtCalLastRawBin = 0;
    m_mtCalStopArmed = false;
    const double speedKHz = m_mtCalSpeedSpinBox ? m_mtCalSpeedSpinBox->value() : 0.100;
    m_mtCalSpeedHz = qAbs(speedKHz) * 1000.0;
    if (!sendGuideSpeed(speedKHz, speedKHz)) {
        updateMtCalStatus(tr("MT cal speed command failed"));
        return;
    }
    m_mtCalTraining = true;
    m_mtCalStartButton->setEnabled(false);
    m_mtCalStopButton->setEnabled(true);
    m_mtCalUploadButton->setEnabled(false);
    updateMtCalStatus(tr("Collecting 0/%1 steps at %2 kHz")
                      .arg(static_cast<qint64>(MtCalStopSteps))
                      .arg(speedKHz, 0, 'f', 3));
}

void MainWindow::stopMtCalibration()
{
    if (!m_mtCalTraining && m_mtCalRaw21.isEmpty())
        return;

    m_mtCalTraining = false;
    m_mtCalRawPending = false;
    if (m_mountController->isConnected())
        m_mountController->stopDec();
    setDecSpeedState(0.0, 0.0);
    m_mtCalStartButton->setEnabled(true);
    m_mtCalStopButton->setEnabled(false);
    m_mtCalTableReady = buildMtCalTable();
    m_mtCalUploadButton->setEnabled(m_mtCalTableReady);
}

void MainWindow::uploadMtCalibration()
{
    if (!m_mtCalTableReady && !buildMtCalTable()) {
        updateMtCalStatus(tr("MT cal table is not ready"));
        return;
    }
    if (!m_mountController->isConnected()) {
        updateMtCalStatus(tr("Connect mount serial first"));
        return;
    }

    m_mtUploadActive = true;
    m_mtMonitorRawPending = false;
    m_mountResponses.clear();
    m_mountController->sendCommand(QStringLiteral("CAL:UPLOAD,ABORT"));
    waitForMountResponse(QStringLiteral("CAL:UPLOAD,ABORT,OK"), 1000);

    m_mountResponses.clear();
    if (!m_mountController->sendCommand(QStringLiteral("CAL:UPLOAD,BEGIN,%1,%2,%3")
                                        .arg(m_mtCalOffset25)
                                        .arg(m_mtCalDirInverted)
                                        .arg(MtCalBins))
            || !waitForMountResponse(QStringLiteral("CAL:UPLOAD,BEGIN,OK"), 2000)) {
        updateMtCalStatus(tr("MT cal upload begin failed"));
        m_mtUploadActive = false;
        return;
    }

    for (int i = 0; i < MtCalBins; ++i) {
        bool binAccepted = false;
        for (int attempt = 0; attempt < 5 && !binAccepted; ++attempt) {
            m_mountResponses.clear();
            if (!m_mountController->sendCommand(QStringLiteral("CAL:UPLOAD,BIN,%1,%2")
                                                .arg(i)
                                                .arg(m_mtCalLut.value(i)))) {
                updateMtCalStatus(tr("MT upload stopped at bin %1").arg(i));
                m_mtUploadActive = false;
                return;
            }
            if (waitForMountResponse(QStringLiteral("CAL:UPLOAD,BIN,OK,idx=%1").arg(i), 2000)) {
                binAccepted = true;
                break;
            }

            m_mountResponses.clear();
            if (m_mountController->sendCommand(QStringLiteral("CAL:STATUS"))
                    && waitForMountResponse(QStringLiteral("CAL:STATUS"), 1000)) {
                static const QRegularExpression upRe(QStringLiteral("up=(\\d+)/(\\d+)"));
                for (const QString &response : qAsConst(m_mountResponses)) {
                    const auto match = upRe.match(response);
                    if (!match.hasMatch())
                        continue;
                    const int uploaded = match.captured(1).toInt();
                    if (uploaded >= i + 1) {
                        binAccepted = true;
                    }
                    break;
                }
            }

            if (!binAccepted) {
                updateMtCalStatus(tr("Retry MT bin %1 (%2/5)").arg(i).arg(attempt + 2));
                QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
                QThread::msleep(20);
            }
        }

        if (!binAccepted) {
            updateMtCalStatus(tr("MT upload failed at bin %1 after retries").arg(i));
            m_mountController->sendCommand(QStringLiteral("CAL:UPLOAD,ABORT"));
            m_mtUploadActive = false;
            return;
        }

        if ((i % 128) == 127 || i == MtCalBins - 1) {
            updateMtCalStatus(tr("Uploading MT LUT %1/%2").arg(i + 1).arg(MtCalBins));
        } else if ((i % 16) == 0) {
            QCoreApplication::processEvents();
        }
    }

    m_mountResponses.clear();
    if (m_mountController->sendCommand(QStringLiteral("CAL:UPLOAD,COMMIT"))
            && waitForMountResponse(QStringLiteral("CAL:UPLOAD,COMMIT,OK"), 5000)) {
        m_mtMonitorOffset25 = m_mtCalOffset25;
        m_mtMonitorDirInverted = m_mtCalDirInverted;
        m_mtMonitorHasCalStatus = true;
        updateMtCalStatus(tr("MT calibration uploaded"));
    } else {
        updateMtCalStatus(tr("MT calibration commit failed"));
    }
    m_mtUploadActive = false;
}

void MainWindow::uploadPecTable()
{
    if (!m_pecTableReady && !buildPecTrimTable()) {
        updateGuideStatus(tr("PEC table is not ready"));
        return;
    }

    if (!m_mountController->isConnected()) {
        updateGuideStatus(tr("Connect mount serial before PEC upload"));
        return;
    }

    const double refHz = qAbs(m_pecTrainRefSpeedHz) > 0.0 ? m_pecTrainRefSpeedHz : m_referenceDecSpeedHz;
    if (qAbs(refHz) < 1.0) {
        updateGuideStatus(tr("PEC upload needs a valid reference speed"));
        return;
    }

    const int dir = refHz >= 0.0 ? 1 : -1;
    const double refKHz = qAbs(refHz) / 1000.0;
    const int cycles = qMax(1, m_pecCyclesDone);
    m_mountResponses.clear();
    m_mountController->sendCommand(QStringLiteral("PEC:UPLOAD,ABORT"));
    waitForMountResponse(QStringLiteral("PEC:UPLOAD,ABORT,OK"), 1000);
    m_mountResponses.clear();
    if (!m_mountController->sendCommand(QStringLiteral("PEC:UPLOAD,BEGIN,%1,%2,%3")
                                        .arg(refKHz, 0, 'f', 6)
                                        .arg(cycles)
                                        .arg(dir))) {
        return;
    }
    if (!waitForMountResponse(QStringLiteral("PEC:UPLOAD,BEGIN,OK"), 2000)) {
        updateGuideStatus(tr("PEC upload begin no ACK"));
        return;
    }

    for (int i = 0; i < PecBins; ++i) {
        const QString line = QStringLiteral("PEC:UPLOAD,BIN,%1,%2")
                .arg(i)
                .arg(m_pecTrimSps.value(i), 0, 'f', 4);
        if (!m_mountController->sendCommand(line)) {
            updateGuideStatus(tr("PEC upload stopped at bin %1").arg(i));
            return;
        }
        if ((i % 32) == 31 || i == PecBins - 1) {
            if (!waitForMountResponse(QStringLiteral("PEC:UPLOAD,BIN,OK,idx=%1").arg(i), 3000)) {
                updateGuideStatus(tr("PEC upload no ACK at bin %1").arg(i));
                m_mountController->sendCommand(QStringLiteral("PEC:UPLOAD,ABORT"));
                return;
            }
        } else if ((i % 8) == 0) {
            QCoreApplication::processEvents();
            QThread::msleep(5);
        }
    }

    const double nowSeconds = m_elapsed.isValid() ? static_cast<double>(m_elapsed.elapsed()) / 1000.0 : m_pecStartSeconds;
    const double stepsMoved = qMax(0.0, nowSeconds - m_pecStartSeconds) * qAbs(refHz);
    const int phaseStep = static_cast<int>(std::fmod(stepsMoved, static_cast<double>(PecPeriodSteps)));
    const int phaseBin = qBound(0,
                                static_cast<int>(phaseStep * static_cast<double>(PecBins) / PecPeriodSteps),
                                PecBins - 1);

    if (m_mountController->sendCommand(QStringLiteral("PEC:UPLOAD,COMMIT,%1").arg(phaseBin))
            && waitForMountResponse(QStringLiteral("PEC:UPLOAD,COMMIT,OK"), 5000)) {
        updateGuideStatus(tr("PEC uploaded, phase bin %1").arg(phaseBin));
        if (m_pecStatusLabel)
            m_pecStatusLabel->setText(tr("PEC uploaded, phase %1").arg(phaseBin));
    } else {
        updateGuideStatus(tr("PEC upload commit failed; check mount response"));
        if (m_pecStatusLabel)
            m_pecStatusLabel->setText(tr("PEC upload failed"));
    }
}

void MainWindow::startBacklashTest()
{
    if (!m_mountController->isConnected()) {
        updateBacklashStatus(tr("Connect mount serial first"));
        return;
    }

    if (!m_worker) {
        updateBacklashStatus(tr("Start encoder reading first"));
        return;
    }

    stopGuideSimulation();
    m_backlashActive = true;
    m_backlashState = BacklashState::Idle;
    m_backlashCyclesDone = 0;
    m_backlashCurrentDir = 1;
    m_backlashConsecutive = 0;
    m_backlashPosToNegSteps.clear();
    m_backlashNegToPosSteps.clear();
    m_backlashStartButton->setEnabled(false);
    m_backlashStopButton->setEnabled(true);
    updateReadInterval(qMin(m_intervalMs, 50));
    beginBacklashSettling(m_backlashCurrentDir);
}

void MainWindow::stopBacklashTest()
{
    if (!m_backlashActive)
        return;

    m_backlashActive = false;
    m_backlashState = BacklashState::Idle;
    if (m_backlashTimer)
        m_backlashTimer->stop();
    if (m_mountController->isConnected())
        m_mountController->stopDec();
    setDecSpeedState(0.0, 0.0);
    m_backlashStartButton->setEnabled(true);
    m_backlashStopButton->setEnabled(false);
    updateBacklashStatus(tr("Backlash stopped"));
}

void MainWindow::runBacklashStep()
{
    if (!m_backlashActive)
        return;

    if (m_backlashState == BacklashState::Settling) {
        beginBacklashMeasurement(-m_backlashCurrentDir);
    }
}

void MainWindow::beginBacklashSettling(int dir)
{
    m_backlashCurrentDir = dir >= 0 ? 1 : -1;
    m_backlashState = BacklashState::Settling;
    m_backlashStateStartMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    const double speedKHz = m_backlashSpeedSpinBox->value() * static_cast<double>(m_backlashCurrentDir);
    sendGuideSpeed(speedKHz, speedKHz);
    m_backlashTimer->start(m_backlashSettleMsSpinBox->value());
    updateBacklashStatus(tr("Settling dir %1, cycle %2/%3")
                         .arg(m_backlashCurrentDir > 0 ? "+" : "-")
                         .arg(m_backlashCyclesDone + 1)
                         .arg(m_backlashCyclesSpinBox->value()));
}

void MainWindow::beginBacklashMeasurement(int dir)
{
    m_backlashCurrentDir = dir >= 0 ? 1 : -1;
    m_backlashState = BacklashState::Measuring;
    m_backlashConsecutive = 0;
    m_backlashReverseMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    m_backlashReverseDec = m_previousDec;
    const double speedKHz = m_backlashSpeedSpinBox->value() * static_cast<double>(m_backlashCurrentDir);
    sendGuideSpeed(speedKHz, speedKHz);
    updateBacklashStatus(tr("Measuring %1 after reversal").arg(m_backlashCurrentDir > 0 ? "+" : "-"));
}

void MainWindow::processBacklashSample(const EncoderSample &sample, double actualSpeedHz)
{
    if (!m_backlashActive || m_backlashState != BacklashState::Measuring)
        return;
    if (m_backlashReverseMs <= 0)
        return;

    const double movedArcsec = shortestEncoderDelta(sample.dec, m_backlashReverseDec) / EncoderFullScale * ArcsecPerRev;
    const bool directionMatched = (m_backlashCurrentDir > 0 && actualSpeedHz > 0.0)
            || (m_backlashCurrentDir < 0 && actualSpeedHz < 0.0);
    if (directionMatched && qAbs(movedArcsec) >= m_backlashMoveThresholdArcsec) {
        ++m_backlashConsecutive;
    } else {
        m_backlashConsecutive = 0;
    }

    if (m_backlashConsecutive < 3)
        return;

    const qint64 elapsedMs = sample.elapsedMs - m_backlashReverseMs;
    if (elapsedMs <= 0)
        return;

    const double steps = static_cast<double>(elapsedMs) / 1000.0
            * m_backlashSpeedSpinBox->value() * 1000.0;
    recordBacklashResult(steps);

    if (m_backlashCurrentDir > 0)
        ++m_backlashCyclesDone;

    if (m_backlashCyclesDone >= m_backlashCyclesSpinBox->value()) {
        m_backlashActive = false;
        m_backlashState = BacklashState::Idle;
        m_backlashTimer->stop();
        if (m_mountController->isConnected())
            m_mountController->stopDec();
        setDecSpeedState(0.0, 0.0);
        m_backlashStartButton->setEnabled(true);
        m_backlashStopButton->setEnabled(false);
        updateBacklashStatus(tr("Done +->- avg %1 steps, -->+ avg %2 steps")
                             .arg(m_backlashPosToNegSteps.isEmpty() ? 0.0
                                  : std::accumulate(m_backlashPosToNegSteps.cbegin(), m_backlashPosToNegSteps.cend(), 0.0)
                                    / m_backlashPosToNegSteps.size(), 0, 'f', 1)
                             .arg(m_backlashNegToPosSteps.isEmpty() ? 0.0
                                  : std::accumulate(m_backlashNegToPosSteps.cbegin(), m_backlashNegToPosSteps.cend(), 0.0)
                                    / m_backlashNegToPosSteps.size(), 0, 'f', 1));
        return;
    }

    beginBacklashSettling(m_backlashCurrentDir);
}

void MainWindow::recordBacklashResult(double steps)
{
    if (m_backlashCurrentDir < 0)
        m_backlashPosToNegSteps.append(steps);
    else
        m_backlashNegToPosSteps.append(steps);

    updateBacklashStatus(tr("%1 backlash %2 steps")
                         .arg(m_backlashCurrentDir < 0 ? "+->-" : "-->+")
                         .arg(steps, 0, 'f', 1));
}

void MainWindow::updateBacklashStatus(const QString &message)
{
    if (m_backlashStatusLabel)
        m_backlashStatusLabel->setText(QStringLiteral("%1  %2")
                                       .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                                       .arg(message));
}

void MainWindow::updateMtCalStatus(const QString &message)
{
    if (m_mtCalStatusLabel)
        m_mtCalStatusLabel->setText(QStringLiteral("%1  %2")
                                    .arg(QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss")))
                                    .arg(message));
}

void MainWindow::handleMtRawResponse(const QString &line)
{
    if (!m_mtCalRawPending || !line.startsWith(QStringLiteral("MT:RAW")))
        return;

    static const QRegularExpression rawRe(QStringLiteral("raw21=(\\d+)"));
    const auto rawMatch = rawRe.match(line);
    if (!rawMatch.hasMatch())
        return;

    const uint32_t raw21 = rawMatch.captured(1).toUInt() & (MtRawFullScale - 1u);
    m_mtCalRaw21.append(raw21);
    m_mtCalTama25.append(m_mtCalPendingTama25 & (TamaFullScale - 1u));
    m_mtCalRawPending = false;

    const int rawBin = static_cast<int>((raw21 & (MtRawFullScale - 1u)) >> 8);
    if (!m_mtCalHaveLastRawBin) {
        markMtCalRawCoverage(rawBin);
        m_mtCalHaveLastRawBin = true;
        m_mtCalLastRawBin = rawBin;
    } else {
        int delta = rawBin - m_mtCalLastRawBin;
        if (delta > MtCalBins / 2)
            delta -= MtCalBins;
        else if (delta < -MtCalBins / 2)
            delta += MtCalBins;

        const int step = delta >= 0 ? 1 : -1;
        int bin = m_mtCalLastRawBin;
        markMtCalRawCoverage(bin);
        for (int i = 0; i < qAbs(delta); ++i) {
            bin = (bin + step + MtCalBins) % MtCalBins;
            markMtCalRawCoverage(bin);
        }
        m_mtCalLastRawBin = rawBin;
    }

    if ((m_mtCalRaw21.size() % 20) == 0) {
        updateMtCalStatus(tr("Collecting %1/%2 steps, samples %3, rawcov %4/%5, raw %6")
                          .arg(static_cast<qint64>(m_mtCalActualSteps))
                          .arg(static_cast<qint64>(MtCalStopSteps))
                          .arg(m_mtCalRaw21.size())
                          .arg(m_mtCalRawCoverageCount)
                          .arg(MtCalBins)
                          .arg(raw21));
    }
}

void MainWindow::handleMtMonitorRawResponse(const QString &line)
{
    if (!m_mtMonitorRawPending || m_mtCalRawPending || !line.startsWith(QStringLiteral("MT:RAW")))
        return;

    static const QRegularExpression rawRe(QStringLiteral("raw21=(\\d+)"));
    const auto rawMatch = rawRe.match(line);
    if (!rawMatch.hasMatch())
        return;

    const uint32_t raw21 = rawMatch.captured(1).toUInt() & (MtRawFullScale - 1u);
    appendMtMonitorSample(m_mtMonitorPendingTama25, raw21, m_mtMonitorPendingElapsedMs);
    m_mtMonitorRawPending = false;
}

void MainWindow::handleCalStatusResponse(const QString &line)
{
    if (!line.startsWith(QStringLiteral("CAL:STATUS")))
        return;

    static const QRegularExpression offsetRe(QStringLiteral("offset25=(-?\\d+)"));
    static const QRegularExpression dirRe(QStringLiteral("dir_inv=(\\d+)"));
    const auto offsetMatch = offsetRe.match(line);
    const auto dirMatch = dirRe.match(line);
    if (!offsetMatch.hasMatch() || !dirMatch.hasMatch())
        return;

    m_mtMonitorOffset25 = offsetMatch.captured(1).toInt();
    m_mtMonitorDirInverted = dirMatch.captured(1).toInt() != 0 ? 1 : 0;
    m_mtMonitorHasCalStatus = true;
}

void MainWindow::requestMtMonitorSample(const EncoderSample &sample)
{
    if (!m_mountController->isConnected() || m_mtCalTraining || m_mtUploadActive)
        return;
    if (m_mtCalRawPending)
        return;

    if (m_mtMonitorRawPending) {
        const qint64 pendingTimeoutMs = m_mtPhaseScanActive
                ? qMax<qint64>(150, m_intervalMs * 3)
                : qMax<qint64>(600, MtMonitorMinIntervalMs + m_intervalMs * 2);
        if (sample.elapsedMs - m_mtMonitorPendingElapsedMs > pendingTimeoutMs)
            m_mtMonitorRawPending = false;
        else
            return;
    }

    const qint64 minRequestIntervalMs = m_mtPhaseScanActive
            ? qMax<qint64>(50, m_intervalMs)
            : qMax<qint64>(MtMonitorMinIntervalMs, m_intervalMs);
    if (m_mtMonitorLastRequestMs >= 0 && sample.elapsedMs - m_mtMonitorLastRequestMs < minRequestIntervalMs)
        return;

    if (!m_mtMonitorHasCalStatus) {
        m_mountController->sendCommand(QStringLiteral("CAL:STATUS"));
        m_mtMonitorLastRequestMs = sample.elapsedMs;
        return;
    }

    if (m_mountController->sendCommand(QStringLiteral("MT:RAW"))) {
        m_mtMonitorRawPending = true;
        m_mtMonitorPendingTama25 = sample.dec & (TamaFullScale - 1u);
        m_mtMonitorPendingElapsedMs = sample.elapsedMs;
        m_mtMonitorLastRequestMs = sample.elapsedMs;
    }
}

void MainWindow::appendMtMonitorSample(uint32_t tama25, uint32_t raw21, qint64 elapsedMs)
{
    const double seconds = static_cast<double>(elapsedMs) / 1000.0;
    const uint32_t rawOffset25 = mtRawOffsetOnly25(raw21);

    if (m_hasPreviousMtErrorSample) {
        const qint64 elapsedDeltaMs = elapsedMs - m_previousMtErrorElapsedMs;
        if (elapsedDeltaMs > 0) {
            const double dtSeconds = static_cast<double>(elapsedDeltaMs) / 1000.0;
            const double theoreticalEncoderDeltaCounts =
                    -m_referenceDecSpeedHz / PulsesPerOutputRev * EncoderFullScale * dtSeconds;

            const double rawOffsetDeltaCounts = shortestEncoderDelta(rawOffset25, m_previousMtRawOffset25);
            m_mtRawOffsetCumulativePositionErrorCounts += rawOffsetDeltaCounts - theoreticalEncoderDeltaCounts;
            m_mtRawOffsetPositionErrorArcsec =
                    m_mtRawOffsetCumulativePositionErrorCounts / EncoderFullScale * ArcsecPerRev;

            if (m_mtRawOffsetPositionErrorSeries)
                m_mtRawOffsetPositionErrorSeries->append(seconds, m_mtRawOffsetPositionErrorArcsec);

            if (m_pecTraining && m_pecTrainState == PecTrainState::Training
                    && qAbs(m_pecTrainRefSpeedHz) > 0.0
                    && m_pecMtCycleSum.size() == PecBins
                    && m_pecMtCycleCount.size() == PecBins) {
                const double stepsMoved = qMax(0.0, seconds - m_pecStartSeconds) * qAbs(m_pecTrainRefSpeedHz);
                const int phaseStep = static_cast<int>(std::fmod(stepsMoved, static_cast<double>(PecPeriodSteps)));
                const int bin = qBound(0,
                                       static_cast<int>(phaseStep * static_cast<double>(PecBins) / PecPeriodSteps),
                                       PecBins - 1);
                m_pecMtCycleSum[bin] += m_mtRawOffsetPositionErrorArcsec;
                ++m_pecMtCycleCount[bin];
            }

            if (m_mtPhaseScanActive) {
                if (m_mtPhaseScanStartMs < 0) {
                    m_mtPhaseScanStartMs = elapsedMs;
                    m_mtPhaseScanLastStatusMs = -1;
                }
                const double scanSeconds = static_cast<double>(elapsedMs - m_mtPhaseScanStartMs) / 1000.0;
                const double scanSteps = qMax(0.0, scanSeconds * qAbs(m_mtPhaseScanSpeedHz));
                m_mtPhaseScanSamples.append({seconds, scanSteps, m_mtRawOffsetPositionErrorArcsec});

                if (m_mtPhaseScanLastStatusMs < 0 || elapsedMs - m_mtPhaseScanLastStatusMs >= 1000) {
                    m_mtPhaseScanLastStatusMs = elapsedMs;
                    const int elapsedSec = qMax<qint64>(0, (elapsedMs - m_mtPhaseScanStartMs) / 1000);
                    const double periodSeconds = static_cast<double>(PecPeriodSteps) / qAbs(m_mtPhaseScanSpeedHz);
                    const double cyclesDone = static_cast<double>(elapsedSec) / periodSeconds;
                    if (m_mtPhaseStatusLabel)
                        m_mtPhaseStatusLabel->setText(tr("MT phase scanning %1/%2s cycles=%3 samples=%4")
                                                      .arg(elapsedSec)
                                                      .arg(MtPhaseScanDurationMs / 1000)
                                                      .arg(cyclesDone, 0, 'f', 2)
                                                      .arg(m_mtPhaseScanSamples.size()));
                }

                if (elapsedMs - m_mtPhaseScanStartMs >= MtPhaseScanDurationMs)
                    finishMtPhaseScan(false);
            }

            if (m_lastChartAxisUpdateMs < 0 || elapsedMs - m_lastChartAxisUpdateMs >= ChartAxisUpdateIntervalMs) {
                const double minVisibleSeconds = qMax(0.0, seconds - m_visibleSeconds);
                updateErrorYAxisForVisibleRange(minVisibleSeconds);
                m_lastChartAxisUpdateMs = elapsedMs;
            }
        }
    }
    m_hasPreviousMtErrorSample = true;
    m_previousMtRawOffset25 = rawOffset25;
    m_previousMtErrorElapsedMs = elapsedMs;

    m_tamaDegreeSeries->append(seconds, angle25ToDegree(tama25));
    m_mtRawOffsetDegreeSeries->append(seconds, angle25ToDegree(rawOffset25));

    m_mtRawOffsetErrorArcsecSamples.append(QPointF(seconds, shortestArcsecFrom25(rawOffset25, tama25)));

    const double minKeepSeconds = seconds - m_visibleSeconds;
    while (!m_mtRawOffsetErrorArcsecSamples.isEmpty() && m_mtRawOffsetErrorArcsecSamples.first().x() < minKeepSeconds)
        m_mtRawOffsetErrorArcsecSamples.removeFirst();

    if (m_mtCompareLastStatsUpdateMs < 0 || elapsedMs - m_mtCompareLastStatsUpdateMs >= MtStatsUpdateIntervalMs) {
        updateMtCompareStatsLabel();
        m_mtCompareLastStatsUpdateMs = elapsedMs;
    }
}

void MainWindow::finishMtPhaseScan(bool aborted)
{
    if (!m_mtPhaseScanActive)
        return;

    m_mtPhaseScanActive = false;
    if (m_mountController->isConnected())
        m_mountController->stopDec();
    setDecSpeedState(0.0, 0.0);

    if (m_mtPhaseScanButton)
        m_mtPhaseScanButton->setEnabled(true);
    if (m_mtPhaseStopButton)
        m_mtPhaseStopButton->setEnabled(false);

    if (aborted) {
        if (m_mtPhaseRestorePecEnabled && m_mountController->isConnected()) {
            m_mountController->sendCommand(QStringLiteral("PEC:ENABLE,1"));
            m_mountController->sendCommand(QStringLiteral("PEC:STATUS"));
        }
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(tr("MT phase scan aborted, samples=%1")
                                          .arg(m_mtPhaseScanSamples.size()));
        return;
    }

    int peakBin = -1;
    double peakBinFloat = 0.0;
    double peakToPeakArcsec = 0.0;
    double residualRmsArcsec = 0.0;
    int coverageBins = 0;
    int stableFirst = 0;
    int stableLast = m_mtPhaseScanSamples.size();
    mtPhaseStableSampleRange(&stableFirst, &stableLast);
    double templateCorrelation = 0.0;
    bool usedTemplate = false;
    if (!analyzeMtPhaseScan(&peakBin, &peakBinFloat, &peakToPeakArcsec, &residualRmsArcsec,
                            &coverageBins, &templateCorrelation, &usedTemplate)) {
        if (m_mtPhaseStatusLabel)
            m_mtPhaseStatusLabel->setText(tr("MT phase scan failed: coverage %1/%2 used=%3/%4")
                                          .arg(coverageBins)
                                          .arg(PecBins)
                                          .arg(qMax(0, stableLast - stableFirst))
                                          .arg(m_mtPhaseScanSamples.size()));
        return;
    }
    updateMtPhaseFilteredSeries();
    updateMtPhasePeakMarkers(peakBinFloat);

    const int targetPeakBin = m_mtPhasePeakBinSpinBox ? m_mtPhasePeakBinSpinBox->value() : 0;
    const double lastScanSteps = m_mtPhaseScanSamples.isEmpty() ? 0.0 : m_mtPhaseScanSamples.last().steps;
    const double currentPhaseSteps = std::fmod(qMax(0.0, lastScanSteps),
                                               static_cast<double>(PecPeriodSteps));
    const double currentScanBin = currentPhaseSteps * static_cast<double>(PecBins) / static_cast<double>(PecPeriodSteps);
    int currentPecBin = static_cast<int>(std::lround(static_cast<double>(targetPeakBin) + currentScanBin - peakBinFloat));
    currentPecBin %= PecBins;
    if (currentPecBin < 0)
        currentPecBin += PecBins;

    int actualIdx = 0;
    bool actualEnabled = false;
    const bool statusOk = queryPecStatusSnapshot(&actualIdx, &actualEnabled);
    (void)actualEnabled;

    bool restoreOk = true;
    if (m_mtPhaseRestorePecEnabled && m_mountController->isConnected()) {
        restoreOk = m_mountController->sendCommand(QStringLiteral("PEC:ENABLE,1"));
        if (restoreOk)
            restoreOk = waitForMountResponse(QStringLiteral("PEC:EN,1"), 3000);
        m_mountController->sendCommand(QStringLiteral("PEC:STATUS"));
    }

    double expectedPeakBin = 0.0;
    int peakDiffBins = 0;
    if (m_mtPhaseRefValid) {
        expectedPeakBin = std::fmod(static_cast<double>(targetPeakBin - m_mtPhaseRefIdx),
                                    static_cast<double>(PecBins));
        if (expectedPeakBin < 0.0)
            expectedPeakBin += PecBins;
        peakDiffBins = circularBinDiff(static_cast<int>(std::lround(peakBinFloat)),
                                       static_cast<int>(std::lround(expectedPeakBin)),
                                       PecBins);
    }
    const int currentDiffBins = statusOk
            ? circularBinDiff(currentPecBin, actualIdx, PecBins)
            : 0;

    if (m_mtPhaseStatusLabel) {
        m_mtPhaseStatusLabel->setText(
                    tr("MT phase verify top=%1(%2) exp=%3 peak_err=%4 cur=%5 actual=%6 cur_diff=%7 cov=%8/%9 used=%10/%11 pp=%12 rms=%13 tpl=%14 corr=%15 restore=%16 status=%17")
                    .arg(peakBin)
                    .arg(peakBinFloat, 0, 'f', 1)
                    .arg(m_mtPhaseRefValid ? QString::number(expectedPeakBin, 'f', 1) : QStringLiteral("--"))
                    .arg(m_mtPhaseRefValid ? QString::number(peakDiffBins) : QStringLiteral("--"))
                    .arg(currentPecBin)
                    .arg(statusOk ? QString::number(actualIdx) : QStringLiteral("--"))
                    .arg(statusOk ? QString::number(currentDiffBins) : QStringLiteral("--"))
                    .arg(coverageBins)
                    .arg(PecBins)
                    .arg(qMax(0, stableLast - stableFirst))
                    .arg(m_mtPhaseScanSamples.size())
                    .arg(peakToPeakArcsec, 0, 'f', 1)
                    .arg(residualRmsArcsec, 0, 'f', 1)
                    .arg(usedTemplate ? "1" : "0")
                    .arg(templateCorrelation, 0, 'f', 3)
                    .arg((!m_mtPhaseRestorePecEnabled || restoreOk) ? "OK" : "FAIL")
                    .arg(statusOk ? "OK" : "FAIL"));
    }
}

bool MainWindow::mtPhaseStableSampleRange(int *firstIndex, int *lastIndex) const
{
    const int sampleCount = m_mtPhaseScanSamples.size();
    int first = 0;
    int last = sampleCount;
    if (sampleCount > 0) {
        first = qBound(0, static_cast<int>(std::floor(sampleCount * MtPhaseTrimFraction)), sampleCount - 1);
        last = qBound(first + 1,
                      sampleCount - static_cast<int>(std::floor(sampleCount * MtPhaseTrimFraction)),
                      sampleCount);

        const double minSpanSteps = static_cast<double>(PecPeriodSteps) * MtPhaseMinUsedCycles;
        double spanSteps = m_mtPhaseScanSamples.at(last - 1).steps - m_mtPhaseScanSamples.at(first).steps;
        if (spanSteps < minSpanSteps) {
            first = 0;
            last = sampleCount;
            spanSteps = sampleCount > 1
                    ? m_mtPhaseScanSamples.last().steps - m_mtPhaseScanSamples.first().steps
                    : 0.0;
        }
        if (spanSteps <= 0.0) {
            first = 0;
            last = 0;
        }
    }

    if (firstIndex)
        *firstIndex = first;
    if (lastIndex)
        *lastIndex = last;
    return last > first;
}

bool MainWindow::matchMtPhaseTemplate(const QVector<double> &scanCurve, double *peakBinFloat,
                                      double *correlation, int *shiftBins) const
{
    if (!m_pecMtTemplateReady || m_pecMtTemplate.size() != PecBins || scanCurve.size() != PecBins)
        return false;

    double scanSum = 0.0;
    double scanSumSq = 0.0;
    double templateSum = 0.0;
    double templateSumSq = 0.0;
    for (int i = 0; i < PecBins; ++i) {
        const double s = scanCurve[i];
        const double t = m_pecMtTemplate[i];
        scanSum += s;
        scanSumSq += s * s;
        templateSum += t;
        templateSumSq += t * t;
    }

    const double scanMean = scanSum / static_cast<double>(PecBins);
    const double templateMean = templateSum / static_cast<double>(PecBins);
    const double scanVar = qMax(0.0, scanSumSq / static_cast<double>(PecBins) - scanMean * scanMean);
    const double templateVar = qMax(0.0, templateSumSq / static_cast<double>(PecBins) - templateMean * templateMean);
    if (scanVar <= 1e-9 || templateVar <= 1e-9)
        return false;

    const double scanStd = std::sqrt(scanVar);
    const double templateStd = std::sqrt(templateVar);
    double bestCorr = -2.0;
    int bestShift = 0;
    for (int shift = 0; shift < PecBins; ++shift) {
        double dot = 0.0;
        for (int i = 0; i < PecBins; ++i) {
            int templateIndex = (i - shift) % PecBins;
            if (templateIndex < 0)
                templateIndex += PecBins;
            dot += (scanCurve[i] - scanMean) * (m_pecMtTemplate[templateIndex] - templateMean);
        }
        const double corr = dot / (static_cast<double>(PecBins) * scanStd * templateStd);
        if (corr > bestCorr) {
            bestCorr = corr;
            bestShift = shift;
        }
    }

    if (correlation)
        *correlation = bestCorr;
    if (shiftBins)
        *shiftBins = bestShift;
    if (bestCorr < 0.25)
        return false;

    int templatePeak = 0;
    for (int i = 1; i < PecBins; ++i) {
        if (m_pecMtTemplate[i] > m_pecMtTemplate[templatePeak])
            templatePeak = i;
    }

    double matchedPeak = static_cast<double>(templatePeak + bestShift);
    matchedPeak = std::fmod(matchedPeak, static_cast<double>(PecBins));
    if (matchedPeak < 0.0)
        matchedPeak += PecBins;

    if (peakBinFloat)
        *peakBinFloat = matchedPeak;
    return true;
}

bool MainWindow::analyzeMtPhaseScan(int *peakBin, double *peakBinFloat, double *peakToPeakArcsec,
                                    double *residualRmsArcsec, int *coverageBins,
                                    double *templateCorrelation, bool *usedTemplate) const
{
    if (coverageBins)
        *coverageBins = 0;
    if (templateCorrelation)
        *templateCorrelation = 0.0;
    if (usedTemplate)
        *usedTemplate = false;
    if (m_mtPhaseScanSamples.size() < PecBins / 2)
        return false;

    int firstIndex = 0;
    int lastIndex = 0;
    if (!mtPhaseStableSampleRange(&firstIndex, &lastIndex) || lastIndex - firstIndex < PecBins / 2)
        return false;

    const double firstStep = m_mtPhaseScanSamples.at(firstIndex).steps;
    const double lastStep = m_mtPhaseScanSamples.at(lastIndex - 1).steps;
    const double spanSteps = qMax(1.0, lastStep - firstStep);
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    const int usedSamples = lastIndex - firstIndex;
    for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
        const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
        const double x = (sample.steps - firstStep) / spanSteps;
        const double y = sample.rawErrorArcsec;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double denom = static_cast<double>(usedSamples) * sumXX - sumX * sumX;
    const double slope = qAbs(denom) > 1e-9
            ? (static_cast<double>(usedSamples) * sumXY - sumX * sumY) / denom
            : 0.0;
    const double intercept = usedSamples > 0
            ? (sumY - slope * sumX) / static_cast<double>(usedSamples)
            : 0.0;
    const auto trendFor = [firstStep, spanSteps, intercept, slope](double steps) {
        return intercept + slope * ((steps - firstStep) / spanSteps);
    };

    QVector<double> sum(PecBins, 0.0);
    QVector<int> count(PecBins, 0);
    for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
        const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
        double phaseSteps = std::fmod(sample.steps, static_cast<double>(PecPeriodSteps));
        if (phaseSteps < 0.0)
            phaseSteps += PecPeriodSteps;
        const int bin = qBound(0,
                               static_cast<int>(phaseSteps * static_cast<double>(PecBins) / static_cast<double>(PecPeriodSteps)),
                               PecBins - 1);
        sum[bin] += sample.rawErrorArcsec - trendFor(sample.steps);
        ++count[bin];
    }

    QVector<double> folded(PecBins, 0.0);
    int filled = 0;
    for (int i = 0; i < PecBins; ++i) {
        if (count[i] > 0) {
            folded[i] = sum[i] / static_cast<double>(count[i]);
            ++filled;
        }
    }
    if (coverageBins)
        *coverageBins = filled;
    if (filled < PecBins * 3 / 4)
        return false;

    const auto fillMissingBins = [](QVector<double> *values, const QVector<int> &counts) {
        for (int i = 0; i < counts.size(); ++i) {
            if (counts[i] > 0)
                continue;
            int prev = -1;
            int next = -1;
            for (int k = 1; k < counts.size(); ++k) {
                const int p = (i + counts.size() - k) % counts.size();
                if (counts[p] > 0) {
                    prev = p;
                    break;
                }
            }
            for (int k = 1; k < counts.size(); ++k) {
                const int n = (i + k) % counts.size();
                if (counts[n] > 0) {
                    next = n;
                    break;
                }
            }
            if (prev >= 0 && next >= 0)
                (*values)[i] = ((*values)[prev] + (*values)[next]) * 0.5;
        }
    };
    const auto smoothBins = [](const QVector<double> &input, int halfWindow, int passes) {
        QVector<double> smooth = input;
        for (int pass = 0; pass < passes; ++pass) {
            QVector<double> next(smooth.size(), 0.0);
            for (int i = 0; i < smooth.size(); ++i) {
                double acc = 0.0;
                int n = 0;
                for (int k = -halfWindow; k <= halfWindow; ++k) {
                    int idx = (i + k) % smooth.size();
                    if (idx < 0)
                        idx += smooth.size();
                    acc += smooth[idx];
                    ++n;
                }
                next[i] = acc / static_cast<double>(n);
            }
            smooth = next;
        }
        return smooth;
    };
    const auto refinedPeakFromSmooth = [](const QVector<double> &smooth, double *peakToPeakOut) {
        int maxBin = 0;
        int minBin = 0;
        for (int i = 1; i < smooth.size(); ++i) {
            if (smooth[i] > smooth[maxBin])
                maxBin = i;
            if (smooth[i] < smooth[minBin])
                minBin = i;
        }

        const double peakToPeak = smooth[maxBin] - smooth[minBin];
        if (peakToPeakOut)
            *peakToPeakOut = peakToPeak;
        const double topThreshold = smooth[maxBin] - peakToPeak * 0.18;
        double weightedBinSum = 0.0;
        double weightSum = 0.0;
        for (int offset = -smooth.size() / 3; offset <= smooth.size() / 3; ++offset) {
            int idx = (maxBin + offset) % smooth.size();
            if (idx < 0)
                idx += smooth.size();
            if (smooth[idx] < topThreshold)
                continue;

            const double weight = qMax(0.0, smooth[idx] - topThreshold);
            const double shapedWeight = weight * weight * weight;
            weightedBinSum += static_cast<double>(maxBin + offset) * shapedWeight;
            weightSum += shapedWeight;
        }

        double refined = weightSum > 0.0
                ? weightedBinSum / weightSum
                : static_cast<double>(maxBin);
        refined = std::fmod(refined, static_cast<double>(smooth.size()));
        if (refined < 0.0)
            refined += smooth.size();
        return refined;
    };
    fillMissingBins(&folded, count);

    QVector<double> smooth = smoothBins(folded, MtPhaseSmoothHalfWindowBins, 3);

    double peakToPeak = 0.0;
    double refined = refinedPeakFromSmooth(smooth, &peakToPeak);

    struct PeakCandidate {
        double bin = 0.0;
        double weight = 0.0;
    };
    QVector<PeakCandidate> candidates;
    const int firstCycle = static_cast<int>(std::floor(firstStep / static_cast<double>(PecPeriodSteps)));
    const int lastCycle = static_cast<int>(std::floor(lastStep / static_cast<double>(PecPeriodSteps)));
    for (int cycle = firstCycle; cycle <= lastCycle; ++cycle) {
        const double cycleStart = static_cast<double>(cycle) * static_cast<double>(PecPeriodSteps);
        const double cycleEnd = cycleStart + static_cast<double>(PecPeriodSteps);
        QVector<double> cycleSum(PecBins, 0.0);
        QVector<int> cycleCount(PecBins, 0);
        for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
            const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
            if (sample.steps < cycleStart || sample.steps >= cycleEnd)
                continue;
            const double phaseSteps = sample.steps - cycleStart;
            const int bin = qBound(0,
                                   static_cast<int>(phaseSteps * static_cast<double>(PecBins)
                                                    / static_cast<double>(PecPeriodSteps)),
                                   PecBins - 1);
            cycleSum[bin] += sample.rawErrorArcsec - trendFor(sample.steps);
            ++cycleCount[bin];
        }

        QVector<double> cycleFolded(PecBins, 0.0);
        int cycleFilled = 0;
        for (int i = 0; i < PecBins; ++i) {
            if (cycleCount[i] > 0) {
                cycleFolded[i] = cycleSum[i] / static_cast<double>(cycleCount[i]);
                ++cycleFilled;
            }
        }
        if (cycleFilled < PecBins * 7 / 10)
            continue;
        fillMissingBins(&cycleFolded, cycleCount);
        QVector<double> cycleSmooth = smoothBins(cycleFolded, MtPhaseSmoothHalfWindowBins, 3);

        double cyclePeakToPeak = 0.0;
        const double cyclePeak = refinedPeakFromSmooth(cycleSmooth, &cyclePeakToPeak);
        double cycleResidualSq = 0.0;
        int cycleResidualN = 0;
        for (int i = 0; i < PecBins; ++i) {
            if (cycleCount[i] <= 0)
                continue;
            const double d = cycleFolded[i] - cycleSmooth[i];
            cycleResidualSq += d * d;
            ++cycleResidualN;
        }
        const double cycleRms = cycleResidualN > 0
                ? std::sqrt(cycleResidualSq / static_cast<double>(cycleResidualN))
                : 0.0;
        if (cyclePeakToPeak < 30.0)
            continue;
        const double quality = cyclePeakToPeak / qMax(8.0, cycleRms + 1.0);
        candidates.append({cyclePeak, quality * static_cast<double>(cycleFilled) / static_cast<double>(PecBins)});
    }

    if (candidates.size() >= 2) {
        const auto circularDiffDouble = [](double a, double b, double bins) {
            double diff = std::fmod(a - b, bins);
            if (diff > bins * 0.5)
                diff -= bins;
            if (diff < -bins * 0.5)
                diff += bins;
            return diff;
        };
        int bestRef = 0;
        double bestScore = -1.0;
        for (int i = 0; i < candidates.size(); ++i) {
            double score = 0.0;
            for (const PeakCandidate &candidate : candidates) {
                if (qAbs(circularDiffDouble(candidate.bin, candidates[i].bin, PecBins)) <= 45.0)
                    score += candidate.weight;
            }
            if (score > bestScore) {
                bestScore = score;
                bestRef = i;
            }
        }

        const double pi = std::acos(-1.0);
        double sx = 0.0;
        double sy = 0.0;
        double sw = 0.0;
        int clustered = 0;
        for (const PeakCandidate &candidate : candidates) {
            if (qAbs(circularDiffDouble(candidate.bin, candidates[bestRef].bin, PecBins)) > 45.0)
                continue;
            const double angle = candidate.bin / static_cast<double>(PecBins) * 2.0 * pi;
            sx += std::cos(angle) * candidate.weight;
            sy += std::sin(angle) * candidate.weight;
            sw += candidate.weight;
            ++clustered;
        }
        if (clustered >= 2 && sw > 0.0) {
            double cycleMean = std::atan2(sy, sx) / (2.0 * pi) * static_cast<double>(PecBins);
            if (cycleMean < 0.0)
                cycleMean += PecBins;

            double clusterSq = 0.0;
            for (const PeakCandidate &candidate : candidates) {
                if (qAbs(circularDiffDouble(candidate.bin, candidates[bestRef].bin, PecBins)) > 45.0)
                    continue;
                const double d = circularDiffDouble(candidate.bin, cycleMean, PecBins);
                clusterSq += d * d * candidate.weight;
            }
            const double clusterRms = std::sqrt(clusterSq / sw);
            if (clusterRms <= 18.0) {
                const double foldedAngle = refined / static_cast<double>(PecBins) * 2.0 * pi;
                const double cycleAngle = cycleMean / static_cast<double>(PecBins) * 2.0 * pi;
                const double foldedWeight = qAbs(circularDiffDouble(refined, cycleMean, PecBins)) <= 35.0
                        ? sw * 0.35
                        : sw * 0.10;
                const double mx = std::cos(cycleAngle) * sw + std::cos(foldedAngle) * foldedWeight;
                const double my = std::sin(cycleAngle) * sw + std::sin(foldedAngle) * foldedWeight;
                refined = std::atan2(my, mx) / (2.0 * pi) * static_cast<double>(PecBins);
                if (refined < 0.0)
                    refined += PecBins;
            }
        }
    }

    refined = std::fmod(refined, static_cast<double>(PecBins));
    if (refined < 0.0)
        refined += PecBins;
    double matchedCorrelation = 0.0;
    int matchedShift = 0;
    const bool templateMatched = matchMtPhaseTemplate(smooth, &refined, &matchedCorrelation, &matchedShift);
    (void)matchedShift;
    if (templateCorrelation)
        *templateCorrelation = matchedCorrelation;
    if (usedTemplate)
        *usedTemplate = templateMatched;
    refined = std::fmod(refined, static_cast<double>(PecBins));
    if (refined < 0.0)
        refined += PecBins;
    const int topBin = static_cast<int>(std::lround(refined)) % PecBins;

    double residualSq = 0.0;
    int residualN = 0;
    for (int i = 0; i < PecBins; ++i) {
        if (count[i] <= 0)
            continue;
        const double d = folded[i] - smooth[i];
        residualSq += d * d;
        ++residualN;
    }

    if (peakBin)
        *peakBin = topBin;
    if (peakBinFloat)
        *peakBinFloat = refined;
    if (peakToPeakArcsec)
        *peakToPeakArcsec = peakToPeak;
    if (residualRmsArcsec)
        *residualRmsArcsec = residualN > 0 ? std::sqrt(residualSq / static_cast<double>(residualN)) : 0.0;
    return true;
}

bool MainWindow::queryPecStatusSnapshot(int *idx, bool *enabled)
{
    if (!m_mountController->isConnected())
        return false;

    static const QRegularExpression idxRe(QStringLiteral("(?:^|,)idx=(\\d+)"));
    static const QRegularExpression enRe(QStringLiteral("(?:^|,)en=(\\d+)"));
    for (int attempt = 0; attempt < 3; ++attempt) {
        m_mountResponses.clear();
        if (!m_mountController->sendCommand(QStringLiteral("PEC:STATUS")))
            return false;
        if (!waitForMountResponse(QStringLiteral("PEC:STATUS"), 1500)) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            QThread::msleep(80);
            continue;
        }

        for (int i = m_mountResponses.size() - 1; i >= 0; --i) {
            const QString &line = m_mountResponses.at(i);
            if (!line.startsWith(QStringLiteral("PEC:STATUS")))
                continue;
            const auto idxMatch = idxRe.match(line);
            const auto enMatch = enRe.match(line);
            if (!idxMatch.hasMatch())
                break;
            if (idx)
                *idx = idxMatch.captured(1).toInt() % PecBins;
            if (enabled)
                *enabled = enMatch.hasMatch() && enMatch.captured(1).toInt() != 0;
            return true;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        QThread::msleep(80);
    }
    return false;
}

int MainWindow::circularBinDiff(int a, int b, int bins)
{
    if (bins <= 0)
        return 0;
    int diff = (a - b) % bins;
    if (diff > bins / 2)
        diff -= bins;
    if (diff < -bins / 2)
        diff += bins;
    return diff;
}

void MainWindow::updateMtPhaseFilteredSeries()
{
    if (!m_mtPhaseFilteredSeries)
        return;

    m_mtPhaseFilteredSeries->clear();
    if (m_mtPhaseScanSamples.size() < PecBins / 2)
        return;

    int firstIndex = 0;
    int lastIndex = 0;
    if (!mtPhaseStableSampleRange(&firstIndex, &lastIndex) || lastIndex - firstIndex < PecBins / 2)
        return;

    const double firstStep = m_mtPhaseScanSamples.at(firstIndex).steps;
    const double lastStep = m_mtPhaseScanSamples.at(lastIndex - 1).steps;
    const double spanSteps = qMax(1.0, lastStep - firstStep);
    double sumX = 0.0;
    double sumY = 0.0;
    double sumXX = 0.0;
    double sumXY = 0.0;
    const int usedSamples = lastIndex - firstIndex;
    for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
        const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
        const double x = (sample.steps - firstStep) / spanSteps;
        const double y = sample.rawErrorArcsec;
        sumX += x;
        sumY += y;
        sumXX += x * x;
        sumXY += x * y;
    }
    const double denom = static_cast<double>(usedSamples) * sumXX - sumX * sumX;
    const double slope = qAbs(denom) > 1e-9
            ? (static_cast<double>(usedSamples) * sumXY - sumX * sumY) / denom
            : 0.0;
    const double intercept = usedSamples > 0
            ? (sumY - slope * sumX) / static_cast<double>(usedSamples)
            : 0.0;
    const auto trendFor = [firstStep, spanSteps, intercept, slope](double steps) {
        return intercept + slope * ((steps - firstStep) / spanSteps);
    };

    QVector<double> sum(PecBins, 0.0);
    QVector<int> count(PecBins, 0);
    for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
        const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
        double phaseSteps = std::fmod(sample.steps, static_cast<double>(PecPeriodSteps));
        if (phaseSteps < 0.0)
            phaseSteps += PecPeriodSteps;
        const int bin = qBound(0,
                               static_cast<int>(phaseSteps * static_cast<double>(PecBins) / static_cast<double>(PecPeriodSteps)),
                               PecBins - 1);
        sum[bin] += sample.rawErrorArcsec - trendFor(sample.steps);
        ++count[bin];
    }

    QVector<double> folded(PecBins, 0.0);
    for (int i = 0; i < PecBins; ++i) {
        if (count[i] > 0)
            folded[i] = sum[i] / static_cast<double>(count[i]);
    }

    for (int i = 0; i < PecBins; ++i) {
        if (count[i] > 0)
            continue;

        int prev = -1;
        int next = -1;
        for (int k = 1; k < PecBins; ++k) {
            const int p = (i + PecBins - k) % PecBins;
            if (count[p] > 0) {
                prev = p;
                break;
            }
        }
        for (int k = 1; k < PecBins; ++k) {
            const int n = (i + k) % PecBins;
            if (count[n] > 0) {
                next = n;
                break;
            }
        }
        if (prev >= 0 && next >= 0)
            folded[i] = (folded[prev] + folded[next]) * 0.5;
    }

    QVector<double> smooth = folded;
    for (int pass = 0; pass < 3; ++pass) {
        QVector<double> next(PecBins, 0.0);
        for (int i = 0; i < PecBins; ++i) {
            double acc = 0.0;
            int n = 0;
            for (int k = -MtPhaseSmoothHalfWindowBins; k <= MtPhaseSmoothHalfWindowBins; ++k) {
                int idx = (i + k) % PecBins;
                if (idx < 0)
                    idx += PecBins;
                acc += smooth[idx];
                ++n;
            }
            next[i] = acc / static_cast<double>(n);
        }
        smooth = next;
    }

    for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
        const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
        double phaseSteps = std::fmod(sample.steps, static_cast<double>(PecPeriodSteps));
        if (phaseSteps < 0.0)
            phaseSteps += PecPeriodSteps;
        const double binFloat = phaseSteps * static_cast<double>(PecBins) / static_cast<double>(PecPeriodSteps);
        const int bin0 = static_cast<int>(std::floor(binFloat)) % PecBins;
        const int bin1 = (bin0 + 1) % PecBins;
        const double frac = binFloat - std::floor(binFloat);
        const double cycleValue = smooth[bin0] * (1.0 - frac) + smooth[bin1] * frac;
        m_mtPhaseFilteredSeries->append(sample.chartSeconds, trendFor(sample.steps) + cycleValue);
    }
}

void MainWindow::updateMtPhasePeakMarkers(double peakBinFloat)
{
    if (!m_mtPhasePeakMarkerSeries)
        return;

    m_mtPhasePeakMarkerSeries->clear();
    if (m_mtPhaseScanSamples.isEmpty())
        return;

    int firstIndex = 0;
    int lastIndex = 0;
    if (!mtPhaseStableSampleRange(&firstIndex, &lastIndex))
        return;

    const double targetPhaseSteps = peakBinFloat / static_cast<double>(PecBins)
            * static_cast<double>(PecPeriodSteps);
    const double toleranceSteps = static_cast<double>(PecPeriodSteps) / static_cast<double>(PecBins) * 1.5;

    double lastMarkerSteps = -static_cast<double>(PecPeriodSteps);
    for (int sampleIndex = firstIndex; sampleIndex < lastIndex; ++sampleIndex) {
        const MtPhaseScanSample &sample = m_mtPhaseScanSamples.at(sampleIndex);
        double phaseSteps = std::fmod(sample.steps, static_cast<double>(PecPeriodSteps));
        if (phaseSteps < 0.0)
            phaseSteps += PecPeriodSteps;

        double delta = phaseSteps - targetPhaseSteps;
        if (delta > static_cast<double>(PecPeriodSteps) * 0.5)
            delta -= PecPeriodSteps;
        else if (delta < -static_cast<double>(PecPeriodSteps) * 0.5)
            delta += PecPeriodSteps;

        if (qAbs(delta) > toleranceSteps)
            continue;

        if (sample.steps - lastMarkerSteps < static_cast<double>(PecPeriodSteps) * 0.5)
            continue;

        double filteredY = sample.rawErrorArcsec;
        if (m_mtPhaseFilteredSeries && m_mtPhaseFilteredSeries->count() > 0) {
            const auto points = m_mtPhaseFilteredSeries->pointsVector();
            auto it = std::lower_bound(points.begin(), points.end(), sample.chartSeconds,
                                       [](const QPointF &point, double x) { return point.x() < x; });
            if (it == points.begin()) {
                filteredY = it->y();
            } else if (it == points.end()) {
                filteredY = points.last().y();
            } else {
                const QPointF p1 = *(it - 1);
                const QPointF p2 = *it;
                const double dx = p2.x() - p1.x();
                const double f = dx > 0.0 ? (sample.chartSeconds - p1.x()) / dx : 0.0;
                filteredY = p1.y() * (1.0 - f) + p2.y() * f;
            }
        }
        m_mtPhasePeakMarkerSeries->append(sample.chartSeconds, filteredY);
        lastMarkerSteps = sample.steps;
    }
}

uint32_t MainWindow::mtRawOffsetOnly25(uint32_t raw21) const
{
    uint32_t raw25 = (raw21 & (MtRawFullScale - 1u)) << 4;
    if (m_mtMonitorDirInverted)
        raw25 = invert25(raw25);
    return static_cast<uint32_t>((static_cast<qint64>(raw25) + m_mtMonitorOffset25)
                                 & (TamaFullScale - 1u));
}

void MainWindow::updateHysteresisTracking(const EncoderSample &sample)
{
    const uint32_t tama25 = sample.dec & (TamaFullScale - 1u);
    if (!m_hysHaveLastTama) {
        m_hysHaveLastTama = true;
        m_hysLastTama25 = tama25;
        return;
    }

    const int32_t d = wrapDiff25(tama25, m_hysLastTama25);
    m_hysLastTama25 = tama25;
    if (qAbs(d) < 8)
        return;

    const int dir = d > 0 ? 1 : -1;
    const double steps = qAbs(static_cast<double>(d)) / EncoderFullScale * PulsesPerOutputRev;
    if (m_hysAutoActive) {
        // DEC+ makes the Tamagawa count decrease, DEC- makes it increase.
        // During automated reversals, ignore the short unload/rebound samples
        // that still move opposite to the commanded segment.
        const int expectedDir = -m_hysAutoCurrentDir;
        if (dir != expectedDir) {
            m_hysLastDir = dir;
            return;
        }

        if (m_hysTransitionDir == 0) {
            m_hysTransitionDir = expectedDir;
            m_hysCurrentTransitionDir = expectedDir;
            m_hysStepsSinceReverse = 0.0;
            m_hysHaveStartError = false;
            m_hysCurrentPoints.clear();
            if (m_hysCurrentCorrSeries)
                m_hysCurrentCorrSeries->clear();
            if (m_hysCurrentRawSeries)
                m_hysCurrentRawSeries->clear();
            if (m_hysCurrentFitSeries)
                m_hysCurrentFitSeries->clear();
        }
        m_hysStepsSinceReverse += steps;
        m_hysLastDir = dir;
        return;
    }
    if (m_hysLastDir != 0 && dir != m_hysLastDir) {
        finishHysteresisTransition();
        m_hysTransitionDir = dir;
        m_hysCurrentTransitionDir = dir;
        m_hysStepsSinceReverse = 0.0;
        m_hysHaveStartError = false;
        m_hysCurrentPoints.clear();
        if (m_hysCurrentCorrSeries)
            m_hysCurrentCorrSeries->clear();
        if (m_hysCurrentRawSeries)
            m_hysCurrentRawSeries->clear();
        updateHysteresisStatus();
    }
    if (m_hysTransitionDir != 0)
        m_hysStepsSinceReverse += steps;
    m_hysLastDir = dir;
}

void MainWindow::finishHysteresisTransition()
{
    if (m_hysCurrentTransitionDir == 0 || m_hysCurrentPoints.isEmpty())
        return;

    QVector<HysBinStats> &bins = m_hysCurrentTransitionDir < 0
            ? m_hysPosToNegBins
            : m_hysNegToPosBins;
    QVector<HysMarkStats> &marks = m_hysCurrentTransitionDir < 0
            ? m_hysPosToNegMarks
            : m_hysNegToPosMarks;
    if (bins.isEmpty())
        bins.resize(HysMaxBins);
    if (marks.isEmpty())
        marks.resize(HysMarkCount);

    if (m_hysCurrentTransitionDir < 0)
        ++m_hysPosToNegTransitions;
    else
        ++m_hysNegToPosTransitions;

    QVector<bool> markFilled(HysMarkCount, false);
    for (const HysTransitionPoint &point : m_hysCurrentPoints) {
        const int bin = qBound(0,
                               static_cast<int>(point.steps / static_cast<double>(HysBinSteps)),
                               HysMaxBins - 1);
        HysBinStats &stats = bins[bin];
        stats.corrSum += point.corrDelta;
        stats.corrSumSq += point.corrDelta * point.corrDelta;
        stats.rawSum += point.rawDelta;
        stats.rawSumSq += point.rawDelta * point.rawDelta;
        ++stats.count;

        for (int i = 0; i < HysMarkCount; ++i) {
            if (markFilled[i] || point.steps < HysMarks[i])
                continue;
            HysMarkStats &mark = marks[i];
            mark.corrSum += point.corrDelta;
            mark.corrSumSq += point.corrDelta * point.corrDelta;
            mark.rawSum += point.rawDelta;
            mark.rawSumSq += point.rawDelta * point.rawDelta;
            ++mark.count;
            markFilled[i] = true;
        }
    }

    m_hysCurrentPoints.clear();
    m_hysCurrentTransitionDir = 0;
    updateHysteresisSeries();
}

void MainWindow::recordHysteresisAutoSegment(int dir)
{
    if (!m_hysAutoActive)
        return;

    updateHysteresisFit();

    HysAutoRecord record;
    record.segmentIndex = m_hysAutoSegmentIndex + 1;
    record.commandDir = dir >= 0 ? 1 : -1;
    record.measuredDir = m_hysCurrentTransitionDir != 0 ? m_hysCurrentTransitionDir : m_hysTransitionDir;
    const qint64 nowMs = m_elapsed.isValid() ? m_elapsed.elapsed() : 0;
    record.durationMs = qMax<qint64>(0, nowMs - m_hysAutoSegmentStartMs);
    record.sampleCount = m_hysCurrentPoints.size();
    record.fit = m_hysCurrentFit;
    if (!m_hysCurrentPoints.isEmpty()) {
        const HysTransitionPoint &first = m_hysCurrentPoints.first();
        const HysTransitionPoint &last = m_hysCurrentPoints.last();
        record.endSteps = last.steps;
        record.startCorrArcsec = first.corrDelta;
        record.endCorrArcsec = last.corrDelta;
        record.startRawArcsec = first.rawDelta;
        record.endRawArcsec = last.rawDelta;
    }

    m_hysAutoRecords.append(record);
}

void MainWindow::exportHysteresisAutoReport()
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    const QString path = QCoreApplication::applicationDirPath()
            + QStringLiteral("/hys_auto_%1.txt").arg(stamp);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        updateMountStatus(tr("Hys Auto export failed: %1").arg(file.errorString()));
        return;
    }

    QTextStream out(&file);
    out.setCodec("UTF-8");

    auto dirText = [](int dir) {
        if (dir > 0)
            return QStringLiteral("+");
        if (dir < 0)
            return QStringLiteral("-");
        return QStringLiteral("0");
    };
    auto meanValue = [](const QVector<double> &values) {
        if (values.isEmpty())
            return 0.0;
        return std::accumulate(values.cbegin(), values.cend(), 0.0)
                / static_cast<double>(values.size());
    };
    auto stdValue = [meanValue](const QVector<double> &values) {
        if (values.size() < 2)
            return 0.0;
        const double mean = meanValue(values);
        double sumSq = 0.0;
        for (double value : values) {
            const double d = value - mean;
            sumSq += d * d;
        }
        return std::sqrt(sumSq / static_cast<double>(values.size() - 1));
    };
    auto writeSummary = [&](const QString &title, int measuredDir) {
        QVector<double> amplitudes;
        QVector<double> taus;
        QVector<double> rmsValues;
        for (const HysAutoRecord &record : m_hysAutoRecords) {
            if (record.measuredDir != measuredDir || !record.fit.valid)
                continue;
            amplitudes.append(record.fit.amplitudeArcsec);
            taus.append(record.fit.tauSteps);
            rmsValues.append(record.fit.rmsArcsec);
        }

        out << title << "\n";
        out << "valid_segments=" << amplitudes.size()
            << ", A_mean_arcsec=" << QString::number(meanValue(amplitudes), 'f', 3)
            << ", A_std_arcsec=" << QString::number(stdValue(amplitudes), 'f', 3)
            << ", tau_mean_steps=" << QString::number(meanValue(taus), 'f', 1)
            << ", tau_std_steps=" << QString::number(stdValue(taus), 'f', 1)
            << ", rms_mean_arcsec=" << QString::number(meanValue(rmsValues), 'f', 3)
            << ", rms_std_arcsec=" << QString::number(stdValue(rmsValues), 'f', 3)
            << "\n\n";
    };

    out << "Hysteresis auto test report\n";
    out << "time=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "speed_khz=" << QString::number(m_hysAutoSpeedKHz, 'f', 6) << "\n";
    out << "cycles=" << HysAutoCycles << "\n";
    out << "segments=" << HysAutoSegments << "\n";
    out << "segment_ms=" << HysAutoSegmentMs << "\n";
    out << "pulses_per_output_rev=" << QString::number(PulsesPerOutputRev, 'f', 0) << "\n";
    out << "model=dCorr_arcsec = A * (1 - exp(-steps / tau_steps))\n\n";

    writeSummary(QStringLiteral("summary measured_dir=-"), -1);
    writeSummary(QStringLiteral("summary measured_dir=+"), 1);

    out << "segment,command_dir,measured_dir,duration_ms,end_steps,samples,"
           "start_corr_arcsec,end_corr_arcsec,start_raw_arcsec,end_raw_arcsec,"
           "fit_valid,A_arcsec,tau_steps,rms_arcsec\n";
    for (const HysAutoRecord &record : m_hysAutoRecords) {
        out << record.segmentIndex << ','
            << dirText(record.commandDir) << ','
            << dirText(record.measuredDir) << ','
            << record.durationMs << ','
            << QString::number(record.endSteps, 'f', 1) << ','
            << record.sampleCount << ','
            << QString::number(record.startCorrArcsec, 'f', 3) << ','
            << QString::number(record.endCorrArcsec, 'f', 3) << ','
            << QString::number(record.startRawArcsec, 'f', 3) << ','
            << QString::number(record.endRawArcsec, 'f', 3) << ','
            << (record.fit.valid ? 1 : 0) << ','
            << QString::number(record.fit.amplitudeArcsec, 'f', 3) << ','
            << QString::number(record.fit.tauSteps, 'f', 1) << ','
            << QString::number(record.fit.rmsArcsec, 'f', 3) << '\n';
    }

    file.close();
    if (m_hysteresisStatusLabel)
        m_hysteresisStatusLabel->setText(tr("Hys Auto report: %1").arg(path));
    updateMountStatus(tr("Hys Auto report saved: %1").arg(path));
}

void MainWindow::updateHysteresisSeries()
{
    updateHysteresisFit();

    auto fillSeries = [](QLineSeries *corrSeries,
                         QLineSeries *rawSeries,
                         const QVector<HysBinStats> &bins) {
        if (!corrSeries || !rawSeries)
            return;
        corrSeries->clear();
        rawSeries->clear();
        for (int i = 0; i < bins.size(); ++i) {
            const HysBinStats &stats = bins.at(i);
            if (stats.count <= 0)
                continue;
            const double x = (static_cast<double>(i) + 0.5) * HysBinSteps;
            corrSeries->append(x, stats.corrSum / static_cast<double>(stats.count));
            rawSeries->append(x, stats.rawSum / static_cast<double>(stats.count));
        }
    };

    fillSeries(m_hysPosToNegCorrSeries, m_hysPosToNegRawSeries, m_hysPosToNegBins);
    fillSeries(m_hysNegToPosCorrSeries, m_hysNegToPosRawSeries, m_hysNegToPosBins);
    if (m_hysCurrentCorrSeries)
        m_hysCurrentCorrSeries->clear();
    if (m_hysCurrentRawSeries)
        m_hysCurrentRawSeries->clear();
    for (const HysTransitionPoint &point : m_hysCurrentPoints) {
        if (m_hysCurrentCorrSeries)
            m_hysCurrentCorrSeries->append(point.steps, point.corrDelta);
        if (m_hysCurrentRawSeries)
            m_hysCurrentRawSeries->append(point.steps, point.rawDelta);
    }
    if (m_hysCurrentFitSeries)
        m_hysCurrentFitSeries->clear();
    if (m_hysCurrentFit.valid && m_hysCurrentFitSeries) {
        const double maxSteps = m_hysCurrentPoints.isEmpty() ? 0.0 : m_hysCurrentPoints.last().steps;
        const int points = qBound(8, static_cast<int>(maxSteps / 5000.0), 160);
        for (int i = 0; i <= points; ++i) {
            const double x = maxSteps * static_cast<double>(i) / static_cast<double>(points);
            const double y = m_hysCurrentFit.amplitudeArcsec * (1.0 - std::exp(-x / m_hysCurrentFit.tauSteps));
            m_hysCurrentFitSeries->append(x, y);
        }
    }

    double maxX = 20000.0;
    const QLineSeries *seriesList[] = {
        m_hysPosToNegCorrSeries,
        m_hysNegToPosCorrSeries,
        m_hysPosToNegRawSeries,
        m_hysNegToPosRawSeries,
        m_hysCurrentCorrSeries,
        m_hysCurrentRawSeries,
        m_hysCurrentFitSeries,
    };
    for (const QLineSeries *series : seriesList) {
        if (!series)
            continue;
        const auto points = series->pointsVector();
        for (const QPointF &point : points)
            maxX = qMax(maxX, point.x() * 1.05);
    }
    if (m_hysteresisAxisX)
        m_hysteresisAxisX->setRange(0.0, maxX);

    bool hasPoint = false;
    double minY = 0.0;
    double maxY = 0.0;
    for (const QLineSeries *series : seriesList) {
        if (!series)
            continue;
        const auto points = series->pointsVector();
        for (const QPointF &point : points) {
            if (!hasPoint) {
                minY = point.y();
                maxY = point.y();
                hasPoint = true;
            } else {
                minY = qMin(minY, point.y());
                maxY = qMax(maxY, point.y());
            }
        }
    }
    if (hasPoint && m_hysteresisAxisY) {
        const double padding = qMax(100.0, (maxY - minY) * 0.08);
        m_hysteresisAxisY->setRange(minY - padding, maxY + padding);
    }
}

void MainWindow::updateHysteresisFit()
{
    m_hysCurrentFit = HysFitResult{};
    if (m_hysCurrentPoints.size() < 20)
        return;

    const double maxSteps = m_hysCurrentPoints.last().steps;
    if (maxSteps < 20000.0)
        return;

    const double tauCandidates[] = {
        10000.0, 15000.0, 22000.0, 33000.0, 47000.0,
        68000.0, 100000.0, 150000.0, 220000.0, 330000.0,
        470000.0, 680000.0, 1000000.0, 1500000.0
    };

    HysFitResult best;
    double bestSse = 0.0;
    for (double tau : tauCandidates) {
        double sumXX = 0.0;
        double sumXY = 0.0;
        for (const HysTransitionPoint &point : m_hysCurrentPoints) {
            const double x = 1.0 - std::exp(-point.steps / tau);
            sumXX += x * x;
            sumXY += x * point.corrDelta;
        }
        if (sumXX < 1e-9)
            continue;

        const double amplitude = sumXY / sumXX;
        double sse = 0.0;
        for (const HysTransitionPoint &point : m_hysCurrentPoints) {
            const double x = 1.0 - std::exp(-point.steps / tau);
            const double residual = point.corrDelta - amplitude * x;
            sse += residual * residual;
        }
        if (!best.valid || sse < bestSse) {
            best.valid = true;
            best.amplitudeArcsec = amplitude;
            best.tauSteps = tau;
            best.rmsArcsec = std::sqrt(sse / static_cast<double>(m_hysCurrentPoints.size()));
            bestSse = sse;
        }
    }

    m_hysCurrentFit = best;
}

void MainWindow::updateHysteresisStatus()
{
    if (!m_hysteresisStatusLabel)
        return;

    auto describeBins = [](const QVector<HysBinStats> &bins) {
        int sampleCount = 0;
        int binCount = 0;
        double corrRepeatSumSq = 0.0;
        double rawRepeatSumSq = 0.0;
        for (const HysBinStats &stats : bins) {
            if (stats.count <= 0)
                continue;
            ++binCount;
            sampleCount += stats.count;
            const double n = static_cast<double>(stats.count);
            const double corrMean = stats.corrSum / n;
            const double rawMean = stats.rawSum / n;
            corrRepeatSumSq += qMax(0.0, stats.corrSumSq - 2.0 * corrMean * stats.corrSum + n * corrMean * corrMean);
            rawRepeatSumSq += qMax(0.0, stats.rawSumSq - 2.0 * rawMean * stats.rawSum + n * rawMean * rawMean);
        }
        const double corrRepeat = sampleCount > 0 ? std::sqrt(corrRepeatSumSq / static_cast<double>(sampleCount)) : 0.0;
        const double rawRepeat = sampleCount > 0 ? std::sqrt(rawRepeatSumSq / static_cast<double>(sampleCount)) : 0.0;
        return QStringLiteral("n=%1 bins=%2 repC=%3\" repR=%4\"")
                .arg(sampleCount)
                .arg(binCount)
                .arg(corrRepeat, 0, 'f', 1)
                .arg(rawRepeat, 0, 'f', 1);
    };

    auto describeMarks = [](const QVector<HysMarkStats> &marks) {
        QStringList parts;
        for (int i = 0; i < HysMarkCount; ++i) {
            const HysMarkStats stats = i < marks.size() ? marks.at(i) : HysMarkStats{};
            if (stats.count <= 0)
                continue;
            const double n = static_cast<double>(stats.count);
            const double corrMean = stats.corrSum / n;
            const double corrVar = qMax(0.0, stats.corrSumSq / n - corrMean * corrMean);
            const double rawMean = stats.rawSum / n;
            const double rawVar = qMax(0.0, stats.rawSumSq / n - rawMean * rawMean);
            parts.append(QStringLiteral("%1k C=%2±%3 R=%4±%5 n=%6")
                         .arg(HysMarks[i] / 1000)
                         .arg(corrMean, 0, 'f', 0)
                         .arg(std::sqrt(corrVar), 0, 'f', 0)
                         .arg(rawMean, 0, 'f', 0)
                         .arg(std::sqrt(rawVar), 0, 'f', 0)
                         .arg(stats.count));
        }
        return parts.isEmpty() ? QStringLiteral("marks=--") : parts.join(QStringLiteral("; "));
    };

    const QString active = m_hysTransitionDir == 0
            ? tr("idle")
            : (m_hysTransitionDir > 0 ? tr("-->+") : tr("+->-"));
    const QString fitText = m_hysCurrentFit.valid
            ? QStringLiteral("fit A=%1\" tau=%2k rms=%3\"")
              .arg(m_hysCurrentFit.amplitudeArcsec, 0, 'f', 0)
              .arg(m_hysCurrentFit.tauSteps / 1000.0, 0, 'f', 0)
              .arg(m_hysCurrentFit.rmsArcsec, 0, 'f', 1)
            : QStringLiteral("fit=--");
    m_hysteresisStatusLabel->setText(
                tr("Hys %1 step=%2 cur=%3 %4 | +->- t=%5 %6 %7 | -->+ t=%8 %9 %10")
                .arg(active)
                .arg(m_hysStepsSinceReverse, 0, 'f', 0)
                .arg(m_hysCurrentPoints.size())
                .arg(fitText)
                .arg(m_hysPosToNegTransitions)
                .arg(describeBins(m_hysPosToNegBins))
                .arg(describeMarks(m_hysPosToNegMarks))
                .arg(m_hysNegToPosTransitions)
                .arg(describeBins(m_hysNegToPosBins))
                .arg(describeMarks(m_hysNegToPosMarks)));
}

void MainWindow::resetHysteresisCurrentTransition()
{
    m_hysHaveLastTama = false;
    m_hysLastTama25 = 0;
    m_hysLastDir = 0;
    m_hysTransitionDir = 0;
    m_hysStepsSinceReverse = 0.0;
    m_hysHaveStartError = false;
    m_hysStartCorrErrorArcsec = 0.0;
    m_hysStartRawErrorArcsec = 0.0;
    m_hysCurrentTransitionDir = 0;
    m_hysCurrentPoints.clear();
    m_hysCurrentFit = HysFitResult{};
    if (m_hysCurrentCorrSeries)
        m_hysCurrentCorrSeries->clear();
    if (m_hysCurrentRawSeries)
        m_hysCurrentRawSeries->clear();
    if (m_hysCurrentFitSeries)
        m_hysCurrentFitSeries->clear();
}

void MainWindow::clearHysteresisData()
{
    m_hysPosToNegCorrSeries->clear();
    m_hysNegToPosCorrSeries->clear();
    m_hysPosToNegRawSeries->clear();
    m_hysNegToPosRawSeries->clear();
    if (m_hysCurrentCorrSeries)
        m_hysCurrentCorrSeries->clear();
    if (m_hysCurrentRawSeries)
        m_hysCurrentRawSeries->clear();
    if (m_hysCurrentFitSeries)
        m_hysCurrentFitSeries->clear();
    m_hysPosToNegTransitions = 0;
    m_hysNegToPosTransitions = 0;
    m_hysPosToNegBins.clear();
    m_hysNegToPosBins.clear();
    m_hysPosToNegMarks.clear();
    m_hysNegToPosMarks.clear();
    resetHysteresisCurrentTransition();
    if (m_hysteresisAxisX)
        m_hysteresisAxisX->setRange(0.0, 20000.0);
    if (m_hysteresisAxisY)
        m_hysteresisAxisY->setRange(-1000.0, 1000.0);
    if (m_hysteresisStatusLabel)
        m_hysteresisStatusLabel->setText(tr("Hys cleared"));
}

void MainWindow::markMtCalRawCoverage(int bin)
{
    if (bin < 0)
        bin = (bin % MtCalBins + MtCalBins) % MtCalBins;
    else if (bin >= MtCalBins)
        bin %= MtCalBins;

    if (!m_mtCalRawCoverage.value(bin, false)) {
        m_mtCalRawCoverage[bin] = true;
        ++m_mtCalRawCoverageCount;
    }
}

void MainWindow::processMtCalSample(const EncoderSample &sample)
{
    if (!m_mtCalTraining)
        return;

    const uint32_t tama25 = sample.dec & (TamaFullScale - 1u);
    if (m_mtCalHaveLastTama) {
        const double deltaCounts = qAbs(static_cast<double>(wrapDiff25(tama25, m_mtCalLastTama25)));
        m_mtCalActualSteps += deltaCounts / EncoderFullScale * PulsesPerOutputRev;
    }
    m_mtCalLastTama25 = tama25;
    m_mtCalHaveLastTama = true;

    if (m_mtCalActualSteps >= MtCalStopSteps) {
        m_mtCalStopArmed = true;
        if (m_mtCalRawCoverageCount >= MtCalBins) {
            updateMtCalStatus(tr("MT cal raw coverage full, building table"));
            stopMtCalibration();
            return;
        }
        if ((m_mtCalRaw21.size() % 50) == 0) {
            updateMtCalStatus(tr("MT cal one turn done, filling coverage %1/%2")
                              .arg(m_mtCalRawCoverageCount)
                              .arg(MtCalBins));
        }
    }

    if (m_mtCalRawPending && sample.elapsedMs - m_mtCalPendingElapsedMs > 1000) {
        m_mtCalRawPending = false;
        ++m_mtCalRawTimeouts;
        updateMtCalStatus(tr("MT raw timeout %1, samples %2")
                          .arg(m_mtCalRawTimeouts)
                          .arg(m_mtCalRaw21.size()));
    }

    if (m_mtCalRawPending || !m_mountController->isConnected())
        return;

    const qint64 minRequestIntervalMs = qMax<qint64>(20, m_intervalMs);
    if (m_mtCalLastRequestMs >= 0 && sample.elapsedMs - m_mtCalLastRequestMs < minRequestIntervalMs)
        return;

    if (m_mountController->sendCommand(QStringLiteral("MT:RAW"))) {
        m_mtCalRawPending = true;
        m_mtCalPendingTama25 = tama25;
        m_mtCalPendingElapsedMs = sample.elapsedMs;
        m_mtCalLastRequestMs = sample.elapsedMs;
    }
}

int32_t MainWindow::wrapDiff25(uint32_t ref25, uint32_t mt25)
{
    int32_t d = static_cast<int32_t>(ref25 & (TamaFullScale - 1u))
            - static_cast<int32_t>(mt25 & (TamaFullScale - 1u));
    const int32_t half = static_cast<int32_t>(TamaFullScale >> 1);
    const int32_t full = static_cast<int32_t>(TamaFullScale);
    if (d >= half)
        d -= full;
    if (d < -half)
        d += full;
    return d;
}

uint32_t MainWindow::invert25(uint32_t value25)
{
    value25 &= (TamaFullScale - 1u);
    if (value25 == 0)
        return 0;
    return (TamaFullScale - value25) & (TamaFullScale - 1u);
}

bool MainWindow::buildMtCalTable()
{
    const int sampleCount = qMin(m_mtCalRaw21.size(), m_mtCalTama25.size());
    if (sampleCount < 200) {
        updateMtCalStatus(tr("Too few MT samples: %1").arg(sampleCount));
        return false;
    }

    qint64 corr = 0;
    int deltaSamples = 0;
    for (int i = 1; i < sampleCount; ++i) {
        const int32_t dTama = wrapDiff25(m_mtCalTama25.at(i), m_mtCalTama25.at(i - 1));
        const uint32_t mt25 = (m_mtCalRaw21.at(i) & (MtRawFullScale - 1u)) << 4;
        const uint32_t prevMt25 = (m_mtCalRaw21.at(i - 1) & (MtRawFullScale - 1u)) << 4;
        const int32_t dMt = wrapDiff25(mt25, prevMt25);
        if (qAbs(dTama) < 16 || qAbs(dMt) < 16)
            continue;
        corr += static_cast<qint64>(dTama) * static_cast<qint64>(dMt);
        ++deltaSamples;
    }
    if (deltaSamples < 20) {
        updateMtCalStatus(tr("MT direction samples too few: %1").arg(deltaSamples));
        return false;
    }
    m_mtCalDirInverted = corr < 0 ? 1 : 0;

    QVector<int32_t> diffs;
    diffs.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        uint32_t mt25 = (m_mtCalRaw21.at(i) & (MtRawFullScale - 1u)) << 4;
        if (m_mtCalDirInverted)
            mt25 = invert25(mt25);
        diffs.append(wrapDiff25(m_mtCalTama25.at(i), mt25));
    }
    std::sort(diffs.begin(), diffs.end());
    m_mtCalOffset25 = diffs.at(diffs.size() / 2);

    QVector<double> sum(MtCalBins, 0.0);
    QVector<int> count(MtCalBins, 0);
    for (int i = 0; i < sampleCount; ++i) {
        uint32_t mt25 = (m_mtCalRaw21.at(i) & (MtRawFullScale - 1u)) << 4;
        if (m_mtCalDirInverted)
            mt25 = invert25(mt25);
        const uint32_t mt25Lin = static_cast<uint32_t>((static_cast<qint64>(mt25) + m_mtCalOffset25)
                                                       & (TamaFullScale - 1u));
        const int32_t err25 = wrapDiff25(m_mtCalTama25.at(i), mt25Lin);
        const int bin = static_cast<int>(mt25Lin >> 12);
        sum[bin] += static_cast<double>(err25 >> 4);
        ++count[bin];
    }

    QVector<double> lut(MtCalBins, 0.0);
    int filled = 0;
    for (int i = 0; i < MtCalBins; ++i) {
        if (count[i] > 0) {
            lut[i] = sum[i] / static_cast<double>(count[i]);
            ++filled;
        }
    }
    const int directFilled = filled;
    if (directFilled < (MtCalBins * 95) / 100) {
        updateMtCalStatus(tr("MT LUT coverage too low: %1/%2").arg(filled).arg(MtCalBins));
        return false;
    }
    if (directFilled < MtCalBins) {
        updateMtCalStatus(tr("MT LUT direct coverage %1/%2, interpolating gaps")
                          .arg(directFilled)
                          .arg(MtCalBins));
    }

    for (int i = 0; i < MtCalBins; ++i) {
        if (count[i] > 0)
            continue;
        int prev = -1;
        int next = -1;
        for (int k = 1; k < MtCalBins; ++k) {
            const int p = (i + MtCalBins - k) % MtCalBins;
            if (count[p] > 0) {
                prev = p;
                break;
            }
        }
        for (int k = 1; k < MtCalBins; ++k) {
            const int n = (i + k) % MtCalBins;
            if (count[n] > 0) {
                next = n;
                break;
            }
        }
        if (prev >= 0 && next >= 0) {
            int gap = next - prev;
            if (gap <= 0)
                gap += MtCalBins;
            int offset = i - prev;
            if (offset <= 0)
                offset += MtCalBins;
            const double t = static_cast<double>(offset) / static_cast<double>(gap);
            lut[i] = lut[prev] + (lut[next] - lut[prev]) * t;
        }
    }

    for (int pass = 0; pass < 3; ++pass) {
        QVector<double> smoothed = lut;
        for (int i = 0; i < MtCalBins; ++i) {
            const int p = (i + MtCalBins - 1) % MtCalBins;
            const int n = (i + 1) % MtCalBins;
            smoothed[i] = (lut[p] + 2.0 * lut[i] + lut[n]) * 0.25;
        }
        lut = smoothed;
    }

    m_mtCalLut = QVector<int>(MtCalBins, 0);
    int maxAbs = 0;
    for (int i = 0; i < MtCalBins; ++i) {
        const int v = qBound(-32768, static_cast<int>(qRound(lut[i])), 32767);
        m_mtCalLut[i] = v;
        maxAbs = qMax(maxAbs, qAbs(v));
    }

    updateMtCalStatus(tr("MT table ready: samples %1, table %2/%3, direct %4, interp %5, off %6, dir_inv %7, max %8")
                      .arg(sampleCount)
                      .arg(MtCalBins)
                      .arg(MtCalBins)
                      .arg(directFilled)
                      .arg(MtCalBins - directFilled)
                      .arg(m_mtCalOffset25)
                      .arg(m_mtCalDirInverted)
                      .arg(maxAbs));
    return true;
}

void MainWindow::resetPecTraining()
{
    m_pecTraining = false;
    m_pecTrainState = PecTrainState::Idle;
    m_pecTableReady = false;
    m_pecMtTemplateReady = false;
    m_pecErrorSum = QVector<double>(PecBins, 0.0);
    m_pecCount = QVector<int>(PecBins, 0);
    m_pecCycleErrorSum = QVector<double>(PecBins, 0.0);
    m_pecCycleCount = QVector<int>(PecBins, 0);
    m_pecTrimSps = QVector<double>(PecBins, 0.0);
    m_pecMtTemplateSum = QVector<double>(PecBins, 0.0);
    m_pecMtTemplateCount = QVector<int>(PecBins, 0);
    m_pecMtCycleSum = QVector<double>(PecBins, 0.0);
    m_pecMtCycleCount = QVector<int>(PecBins, 0);
    m_pecMtTemplate = QVector<double>(PecBins, 0.0);
    m_pecPrescanErrorSum = QVector<double>(PecBins, 0.0);
    m_pecPrescanCount = QVector<int>(PecBins, 0);
    m_pecStartSeconds = 0.0;
    m_pecPrescanStartSeconds = 0.0;
    m_pecAlignTargetSteps = 0.0;
    m_pecTrainRefSpeedHz = 0.0;
    m_pecCyclesDone = 0;
    m_pecLastPhaseStep = 0;
    m_pecCurrentBin = 0;
    m_pecPrescanLastPhaseStep = 0;
    m_pecPrescanPeakBin = 0;
    m_pecLastStatusUpdateMs = 0;
    if (m_pecStatusLabel)
        m_pecStatusLabel->setText(tr("PEC idle"));
}

bool MainWindow::analyzePecPrescanPeak(int *peakBin, double *peakBinFloat, double *peakToPeakArcsec) const
{
    if (m_pecPrescanErrorSum.size() != PecBins || m_pecPrescanCount.size() != PecBins)
        return false;

    QVector<double> folded(PecBins, 0.0);
    int filled = 0;
    for (int i = 0; i < PecBins; ++i) {
        if (m_pecPrescanCount[i] > 0) {
            folded[i] = m_pecPrescanErrorSum[i] / static_cast<double>(m_pecPrescanCount[i]);
            ++filled;
        }
    }
    if (filled < PecBins * 3 / 4)
        return false;

    for (int i = 0; i < PecBins; ++i) {
        if (m_pecPrescanCount[i] > 0)
            continue;
        int prev = -1;
        int next = -1;
        for (int k = 1; k < PecBins; ++k) {
            const int p = (i + PecBins - k) % PecBins;
            if (m_pecPrescanCount[p] > 0) {
                prev = p;
                break;
            }
        }
        for (int k = 1; k < PecBins; ++k) {
            const int n = (i + k) % PecBins;
            if (m_pecPrescanCount[n] > 0) {
                next = n;
                break;
            }
        }
        if (prev >= 0 && next >= 0)
            folded[i] = (folded[prev] + folded[next]) * 0.5;
    }

    QVector<double> smooth = folded;
    for (int pass = 0; pass < 3; ++pass) {
        QVector<double> next(PecBins, 0.0);
        for (int i = 0; i < PecBins; ++i) {
            double acc = 0.0;
            int n = 0;
            for (int k = -12; k <= 12; ++k) {
                int idx = (i + k) % PecBins;
                if (idx < 0)
                    idx += PecBins;
                acc += smooth[idx];
                ++n;
            }
            next[i] = acc / static_cast<double>(n);
        }
        smooth = next;
    }

    int maxBin = 0;
    int minBin = 0;
    for (int i = 1; i < PecBins; ++i) {
        if (smooth[i] > smooth[maxBin])
            maxBin = i;
        if (smooth[i] < smooth[minBin])
            minBin = i;
    }

    const double peakToPeak = smooth[maxBin] - smooth[minBin];
    if (peakToPeak <= 1.0)
        return false;

    const double topThreshold = smooth[maxBin] - peakToPeak * 0.25;
    double weightedBinSum = 0.0;
    double weightSum = 0.0;
    for (int offset = -PecBins / 2; offset <= PecBins / 2; ++offset) {
        int idx = (maxBin + offset) % PecBins;
        if (idx < 0)
            idx += PecBins;
        if (smooth[idx] < topThreshold)
            continue;
        const double weight = qMax(0.0, smooth[idx] - topThreshold);
        const double shapedWeight = weight * weight;
        weightedBinSum += static_cast<double>(maxBin + offset) * shapedWeight;
        weightSum += shapedWeight;
    }

    double refined = weightSum > 0.0
            ? weightedBinSum / weightSum
            : static_cast<double>(maxBin);
    refined = std::fmod(refined, static_cast<double>(PecBins));
    if (refined < 0.0)
        refined += PecBins;
    const int rounded = static_cast<int>(std::lround(refined)) % PecBins;

    if (peakBin)
        *peakBin = rounded;
    if (peakBinFloat)
        *peakBinFloat = refined;
    if (peakToPeakArcsec)
        *peakToPeakArcsec = peakToPeak;
    return true;
}

void MainWindow::beginFormalPecTraining(double seconds)
{
    m_pecErrorSum = QVector<double>(PecBins, 0.0);
    m_pecCount = QVector<int>(PecBins, 0);
    clearPecCycle();
    m_pecTrimSps = QVector<double>(PecBins, 0.0);
    m_pecMtTemplateSum = QVector<double>(PecBins, 0.0);
    m_pecMtTemplateCount = QVector<int>(PecBins, 0);
    m_pecMtCycleSum = QVector<double>(PecBins, 0.0);
    m_pecMtCycleCount = QVector<int>(PecBins, 0);
    m_pecMtTemplate = QVector<double>(PecBins, 0.0);
    m_pecMtTemplateReady = false;
    m_pecStartSeconds = seconds;
    m_pecCyclesDone = 0;
    m_pecLastPhaseStep = 0;
    m_pecCurrentBin = 0;
    m_pecTableReady = false;
    m_pecTrainState = PecTrainState::Training;
    m_pecLastStatusUpdateMs = 0;
    updateGuideStatus(tr("PEC formal training started at Tamagawa peak bin %1").arg(m_pecPrescanPeakBin));
    updatePecStatus();
}

void MainWindow::clearPecCycle()
{
    if (m_pecCycleErrorSum.size() != PecBins)
        m_pecCycleErrorSum = QVector<double>(PecBins, 0.0);
    else
        std::fill(m_pecCycleErrorSum.begin(), m_pecCycleErrorSum.end(), 0.0);

    if (m_pecCycleCount.size() != PecBins)
        m_pecCycleCount = QVector<int>(PecBins, 0);
    else
        std::fill(m_pecCycleCount.begin(), m_pecCycleCount.end(), 0);

    if (m_pecMtCycleSum.size() != PecBins)
        m_pecMtCycleSum = QVector<double>(PecBins, 0.0);
    else
        std::fill(m_pecMtCycleSum.begin(), m_pecMtCycleSum.end(), 0.0);

    if (m_pecMtCycleCount.size() != PecBins)
        m_pecMtCycleCount = QVector<int>(PecBins, 0);
    else
        std::fill(m_pecMtCycleCount.begin(), m_pecMtCycleCount.end(), 0);
}

bool MainWindow::finalizePecCycle(bool allowPartial)
{
    if (m_pecCycleErrorSum.size() != PecBins || m_pecCycleCount.size() != PecBins)
        return false;
    if (m_pecErrorSum.size() != PecBins || m_pecCount.size() != PecBins)
        return false;

    QVector<double> cycleAvg(PecBins, 0.0);
    QVector<int> filledBins;
    filledBins.reserve(PecBins);
    for (int i = 0; i < PecBins; ++i) {
        if (m_pecCycleCount[i] > 0) {
            cycleAvg[i] = m_pecCycleErrorSum[i] / static_cast<double>(m_pecCycleCount[i]);
            filledBins.append(i);
        }
    }

    const int minBins = allowPartial ? PecBins / 4 : PecBins / 2;
    if (filledBins.size() < minBins) {
        clearPecCycle();
        return false;
    }

    const int firstBin = filledBins.first();
    const int lastBin = filledBins.last();
    const double firstValue = cycleAvg[firstBin];
    const double lastValue = cycleAvg[lastBin];
    const double span = qMax(1, lastBin - firstBin);

    double sumDetrended = 0.0;
    for (int bin : filledBins) {
        const double t = static_cast<double>(bin - firstBin) / span;
        const double trend = firstValue + (lastValue - firstValue) * t;
        cycleAvg[bin] -= trend;
        sumDetrended += cycleAvg[bin];
    }

    const double mean = sumDetrended / static_cast<double>(filledBins.size());
    for (int bin : filledBins) {
        m_pecErrorSum[bin] += cycleAvg[bin] - mean;
        ++m_pecCount[bin];
    }

    if (m_pecMtCycleSum.size() == PecBins && m_pecMtCycleCount.size() == PecBins
            && m_pecMtTemplateSum.size() == PecBins && m_pecMtTemplateCount.size() == PecBins) {
        QVector<int> mtFilledBins;
        mtFilledBins.reserve(PecBins);
        QVector<double> mtCycleAvg(PecBins, 0.0);
        for (int i = 0; i < PecBins; ++i) {
            if (m_pecMtCycleCount[i] > 0) {
                mtCycleAvg[i] = m_pecMtCycleSum[i] / static_cast<double>(m_pecMtCycleCount[i]);
                mtFilledBins.append(i);
            }
        }
        if (mtFilledBins.size() >= minBins) {
            const int mtFirstBin = mtFilledBins.first();
            const int mtLastBin = mtFilledBins.last();
            const double mtFirstValue = mtCycleAvg[mtFirstBin];
            const double mtLastValue = mtCycleAvg[mtLastBin];
            const double mtSpan = qMax(1, mtLastBin - mtFirstBin);
            double mtSumDetrended = 0.0;
            for (int bin : mtFilledBins) {
                const double t = static_cast<double>(bin - mtFirstBin) / mtSpan;
                const double trend = mtFirstValue + (mtLastValue - mtFirstValue) * t;
                mtCycleAvg[bin] -= trend;
                mtSumDetrended += mtCycleAvg[bin];
            }
            const double mtMean = mtSumDetrended / static_cast<double>(mtFilledBins.size());
            for (int bin : mtFilledBins) {
                m_pecMtTemplateSum[bin] += mtCycleAvg[bin] - mtMean;
                ++m_pecMtTemplateCount[bin];
            }
        }
    }

    ++m_pecCyclesDone;
    clearPecCycle();
    return true;
}

void MainWindow::updatePecStatus()
{
    if (!m_pecStatusLabel)
        return;

    int filled = 0;
    int samples = 0;
    for (int i = 0; i < m_pecCount.size(); ++i) {
        if (m_pecCount[i] > 0)
            ++filled;
        samples += m_pecCount[i];
    }
    int mtTemplateFilled = 0;
    for (int count : m_pecMtTemplateCount) {
        if (count > 0)
            ++mtTemplateFilled;
    }

    QString state = tr("idle");
    if (m_pecTraining) {
        if (m_pecTrainState == PecTrainState::Prescan)
            state = tr("prescan");
        else if (m_pecTrainState == PecTrainState::AlignWait)
            state = tr("align");
        else if (m_pecTrainState == PecTrainState::Training)
            state = tr("training");
    } else if (m_pecTableReady) {
        state = tr("ready");
    }
    m_pecStatusLabel->setText(tr("PEC %1  turn %2/%3  bin %4  cov %5/%6  n=%7  mt_tpl=%8/%9")
                              .arg(state)
                              .arg(m_pecCyclesDone)
                              .arg(m_pecCyclesSpinBox ? m_pecCyclesSpinBox->value() : 0)
                              .arg(m_pecCurrentBin)
                              .arg(filled)
                              .arg(PecBins)
                              .arg(samples)
                              .arg(mtTemplateFilled)
                              .arg(PecBins));
}

bool MainWindow::buildPecTrimTable()
{
    if (m_pecErrorSum.size() != PecBins || m_pecCount.size() != PecBins)
        return false;

    const double refHz = qAbs(m_pecTrainRefSpeedHz) > 0.0 ? qAbs(m_pecTrainRefSpeedHz) : qAbs(m_referenceDecSpeedHz);
    if (refHz < 1.0)
        return false;

    QVector<double> avg(PecBins, 0.0);
    int filled = 0;
    for (int i = 0; i < PecBins; ++i) {
        if (m_pecCount[i] > 0) {
            avg[i] = m_pecErrorSum[i] / static_cast<double>(m_pecCount[i]);
            ++filled;
        }
    }

    if (filled < PecBins / 3) {
        updateGuideStatus(tr("PEC samples are too sparse: %1/%2 bins").arg(filled).arg(PecBins));
        return false;
    }

    for (int i = 0; i < PecBins; ++i) {
        if (m_pecCount[i] > 0)
            continue;

        int prev = -1;
        int next = -1;
        for (int k = 1; k < PecBins; ++k) {
            const int p = (i + PecBins - k) % PecBins;
            if (m_pecCount[p] > 0) {
                prev = p;
                break;
            }
        }
        for (int k = 1; k < PecBins; ++k) {
            const int n = (i + k) % PecBins;
            if (m_pecCount[n] > 0) {
                next = n;
                break;
            }
        }
        if (prev >= 0 && next >= 0)
            avg[i] = (avg[prev] + avg[next]) * 0.5;
    }

    QVector<double> smooth = avg;
    for (int pass = 0; pass < 4; ++pass) {
        QVector<double> next = smooth;
        for (int i = 0; i < PecBins; ++i) {
            const int p = (i + PecBins - 1) % PecBins;
            const int n = (i + 1) % PecBins;
            next[i] = (smooth[p] + 2.0 * smooth[i] + smooth[n]) * 0.25;
        }
        smooth = next;
    }

    const double arcsecPerStep = ArcsecPerRev / PulsesPerOutputRev;
    const double phaseStepPerBin = static_cast<double>(PecPeriodSteps) / static_cast<double>(PecBins);
    m_pecTrimSps = QVector<double>(PecBins, 0.0);

    double sumTrim = 0.0;
    double maxAbsTrim = 0.0;
    for (int i = 0; i < PecBins; ++i) {
        const int p = (i + PecBins - 1) % PecBins;
        const int n = (i + 1) % PecBins;
        const double dArcsecPerStep = (smooth[n] - smooth[p]) / (2.0 * phaseStepPerBin);
        double trimSps = dArcsecPerStep * refHz / arcsecPerStep;
        const double limit = refHz * 0.35;
        trimSps = qBound(-limit, trimSps, limit);
        m_pecTrimSps[i] = trimSps;
        sumTrim += trimSps;
    }

    const double meanTrim = sumTrim / static_cast<double>(PecBins);
    for (int i = 0; i < PecBins; ++i) {
        m_pecTrimSps[i] -= meanTrim;
        maxAbsTrim = qMax(maxAbsTrim, qAbs(m_pecTrimSps[i]));
    }

    if (m_pecMtTemplateSum.size() == PecBins && m_pecMtTemplateCount.size() == PecBins) {
        QVector<double> mtTemplate(PecBins, 0.0);
        int mtFilled = 0;
        for (int i = 0; i < PecBins; ++i) {
            if (m_pecMtTemplateCount[i] > 0) {
                mtTemplate[i] = m_pecMtTemplateSum[i] / static_cast<double>(m_pecMtTemplateCount[i]);
                ++mtFilled;
            }
        }
        if (mtFilled >= PecBins / 3) {
            for (int i = 0; i < PecBins; ++i) {
                if (m_pecMtTemplateCount[i] > 0)
                    continue;
                int prev = -1;
                int next = -1;
                for (int k = 1; k < PecBins; ++k) {
                    const int p = (i + PecBins - k) % PecBins;
                    if (m_pecMtTemplateCount[p] > 0) {
                        prev = p;
                        break;
                    }
                }
                for (int k = 1; k < PecBins; ++k) {
                    const int n = (i + k) % PecBins;
                    if (m_pecMtTemplateCount[n] > 0) {
                        next = n;
                        break;
                    }
                }
                if (prev >= 0 && next >= 0)
                    mtTemplate[i] = (mtTemplate[prev] + mtTemplate[next]) * 0.5;
            }
            m_pecMtTemplate = mtTemplate;
            m_pecMtTemplateReady = true;
        }
    }

    updateGuideStatus(tr("PEC table built: %1/%2 bins, max trim %3 Hz")
                      .arg(filled)
                      .arg(PecBins)
                      .arg(maxAbsTrim, 0, 'f', 2));
    return true;
}

bool MainWindow::waitForMountResponse(const QString &contains, int timeoutMs)
{
    QDeadlineTimer deadline(timeoutMs);
    int checked = 0;
    while (!deadline.hasExpired()) {
        for (; checked < m_mountResponses.size(); ++checked) {
            if (m_mountResponses[checked].contains(contains))
                return true;
            if (m_mountResponses[checked].contains(QStringLiteral("FAIL")))
                return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        QThread::msleep(5);
    }
    return false;
}

void MainWindow::setChartXRange(double minSeconds, double maxSeconds)
{
    m_commandSpeedAxisX->setRange(minSeconds, maxSeconds);
    m_errorAxisX->setRange(minSeconds, maxSeconds);
    m_mtCompareAxisX->setRange(minSeconds, maxSeconds);
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
