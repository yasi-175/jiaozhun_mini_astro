#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "encoder_worker.h"
#include "mount_controller.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVector>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QXYSeries>

QT_CHARTS_USE_NAMESPACE

class QVBoxLayout;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(const QString &libraryPath,
                        const QString &deviceName,
                        int intervalMs,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

private slots:
    void startReading();
    void stopReading();
    void clearChartData();
    void updateReadInterval(int intervalMs);
    void updateVisibleSeconds(int seconds);
    void updateChartVisibility();
    void updateErrorSeriesVisibility();
    void refreshMountPorts();
    void handleMountResponse(const QString &line);
    void connectMount();
    void disconnectMount();
    void slewDecPositive();
    void slewDecNegative();
    void stopDec();
    void startGuideSimulation();
    void stopGuideSimulation();
    void runGuideExposure();
    void finishGuidePulse();
    void startPecTraining();
    void stopPecTraining();
    void uploadPecTable();
    void enablePecPlayback();
    void disablePecPlayback();
    void queryPecStatus();
    void startGotoPhaseTest();
    void runGotoPhaseTestStep();
    void startMtPhaseScan();
    void stopMtPhaseScan();
    void startMtCalibration();
    void stopMtCalibration();
    void uploadMtCalibration();
    void startHysteresisAutoTest();
    void stopHysteresisAutoTest();
    void runHysteresisAutoStep();
    void startBacklashTest();
    void stopBacklashTest();
    void runBacklashStep();
    void clearHysteresisData();

private:
    enum class BacklashState {
        Idle,
        Settling,
        Measuring
    };
    enum class PecTrainState {
        Idle,
        Prescan,
        AlignWait,
        Training
    };

    void setupUi();
    void setupMountUi(QVBoxLayout *root);
    void setupGuideUi(QVBoxLayout *root);
    void updateStatus(const QString &message);
    void updateMountStatus(const QString &message);
    void updateGuideStatus(const QString &message);
    void handleWorkerStopped();
    void appendSample(const EncoderSample &sample);
    void updateYAxisForVisibleRange(QLineSeries *series, QValueAxis *axis, double minVisibleSeconds, double minPadding);
    void updateErrorYAxisForVisibleRange(double minVisibleSeconds);
    void resetChart();
    double selectedMountSpeedKHz() const;
    void setDecSpeedState(double commandSpeedKHz, double referenceSpeedKHz);
    bool sendGuideSpeed(double commandSpeedKHz, double referenceSpeedKHz);
    double guideCorrectionArcsecPerSecond() const;
    void appendGuideErrorSample(qint64 elapsedMs, double errorArcsec);
    double currentGuideRmsArcsec() const;
    void resetGuideRms();
    void resetPecTraining();
    bool analyzePecPrescanPeak(int *peakBin, double *peakBinFloat, double *peakToPeakArcsec) const;
    void beginFormalPecTraining(double seconds);
    void updatePecStatus();
    void updateMtCalStatus(const QString &message);
    void handleMtRawResponse(const QString &line);
    void handleMtMonitorRawResponse(const QString &line);
    void handleCalStatusResponse(const QString &line);
    void requestMtMonitorSample(const EncoderSample &sample);
    void appendMtMonitorSample(uint32_t tama25, uint32_t raw21, qint64 elapsedMs);
    void finishMtPhaseScan(bool aborted);
    bool mtPhaseStableSampleRange(int *firstIndex, int *lastIndex) const;
    bool matchMtPhaseTemplate(const QVector<double> &scanCurve, double *peakBinFloat,
                              double *correlation, int *shiftBins) const;
    bool analyzeMtPhaseScan(int *peakBin, double *peakBinFloat, double *peakToPeakArcsec,
                            double *residualRmsArcsec, int *coverageBins,
                            double *templateCorrelation, bool *usedTemplate) const;
    void updateMtPhaseFilteredSeries();
    void updateMtPhasePeakMarkers(double peakBinFloat);
    bool queryPecStatusSnapshot(int *idx, bool *enabled);
    static int circularBinDiff(int a, int b, int bins);
    uint32_t mtRawOffsetOnly25(uint32_t raw21) const;
    void updateMtCompareYAxis(double minVisibleSeconds);
    void updateMtCompareStatsLabel();
    void updateHysteresisTracking(const EncoderSample &sample);
    void updateHysteresisSeries();
    void updateHysteresisStatus();
    void finishHysteresisTransition();
    void recordHysteresisAutoSegment(int dir);
    bool beginHysteresisAutoSegment(int dir);
    void updateHysteresisFit();
    void resetHysteresisCurrentTransition();
    void exportHysteresisAutoReport();
    void markMtCalRawCoverage(int bin);
    void processMtCalSample(const EncoderSample &sample);
    bool buildMtCalTable();
    static int32_t wrapDiff25(uint32_t ref25, uint32_t mt25);
    static uint32_t invert25(uint32_t value25);
    void clearPecCycle();
    bool finalizePecCycle(bool allowPartial);
    bool buildPecTrimTable();
    bool waitForMountResponse(const QString &contains, int timeoutMs);
    void beginBacklashSettling(int dir);
    void beginBacklashMeasurement(int dir);
    void processBacklashSample(const EncoderSample &sample, double actualSpeedHz);
    void recordBacklashResult(double steps);
    void updateBacklashStatus(const QString &message);
    void setChartXRange(double minSeconds, double maxSeconds);
    void pruneSeries(QLineSeries *series, double minVisibleSeconds, int maxSamples);

    QString m_libraryPath;
    QString m_deviceName;
    int m_intervalMs = 200;

    QElapsedTimer m_elapsed;
    QThread *m_workerThread = nullptr;
    EncoderWorker *m_worker = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_decLabel = nullptr;
    QLabel *m_decDegreeLabel = nullptr;
    QLabel *m_commandSpeedLabel = nullptr;
    QLabel *m_actualSpeedLabel = nullptr;
    QLabel *m_positionErrorLabel = nullptr;
    QLabel *m_actualIntervalLabel = nullptr;
    QLabel *m_readDurationLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QSpinBox *m_intervalSpinBox = nullptr;
    QSpinBox *m_visibleSecondsSpinBox = nullptr;
    QCheckBox *m_bulkReadCheckBox = nullptr;
    QCheckBox *m_triggerCheckBox = nullptr;
    QCheckBox *m_showCommandSpeedChartCheckBox = nullptr;
    QCheckBox *m_showPositionErrorChartCheckBox = nullptr;
    QCheckBox *m_showMtCompareChartCheckBox = nullptr;
    QCheckBox *m_showHysteresisChartCheckBox = nullptr;
    QCheckBox *m_showTamagawaErrorCheckBox = nullptr;
    QCheckBox *m_showMtRawErrorCheckBox = nullptr;
    QCheckBox *m_showMtFilteredErrorCheckBox = nullptr;
    QCheckBox *m_showMtPeakMarkerCheckBox = nullptr;

    MountController *m_mountController = nullptr;
    QComboBox *m_mountPortComboBox = nullptr;
    QSpinBox *m_mountBaudSpinBox = nullptr;
    QDoubleSpinBox *m_mountSpeedSpinBox = nullptr;
    QPushButton *m_refreshPortsButton = nullptr;
    QPushButton *m_mountConnectButton = nullptr;
    QPushButton *m_mountDisconnectButton = nullptr;
    QPushButton *m_decPositiveButton = nullptr;
    QPushButton *m_decNegativeButton = nullptr;
    QPushButton *m_decStopButton = nullptr;
    QLabel *m_mountStatusLabel = nullptr;
    QStringList m_mountResponses;
    QDoubleSpinBox *m_guideBaseSpeedSpinBox = nullptr;
    QDoubleSpinBox *m_guideDeltaSpeedSpinBox = nullptr;
    QSpinBox *m_guideExposureMsSpinBox = nullptr;
    QSpinBox *m_guideAggressivenessSpinBox = nullptr;
    QSpinBox *m_guideMaxPulseMsSpinBox = nullptr;
    QPushButton *m_guideStartButton = nullptr;
    QPushButton *m_guideStopButton = nullptr;
    QLabel *m_guideRmsLabel = nullptr;
    QLabel *m_guideStatusLabel = nullptr;
    QPushButton *m_pecStartButton = nullptr;
    QPushButton *m_pecStopButton = nullptr;
    QPushButton *m_pecUploadButton = nullptr;
    QPushButton *m_pecEnableButton = nullptr;
    QPushButton *m_pecDisableButton = nullptr;
    QPushButton *m_pecStatusButton = nullptr;
    QPushButton *m_gotoPhaseTestButton = nullptr;
    QSpinBox *m_pecCyclesSpinBox = nullptr;
    QSpinBox *m_mtPhasePeakBinSpinBox = nullptr;
    QPushButton *m_mtPhaseScanButton = nullptr;
    QPushButton *m_mtPhaseStopButton = nullptr;
    QLabel *m_mtPhaseStatusLabel = nullptr;
    QLabel *m_pecStatusLabel = nullptr;
    QDoubleSpinBox *m_mtCalSpeedSpinBox = nullptr;
    QPushButton *m_mtCalStartButton = nullptr;
    QPushButton *m_mtCalStopButton = nullptr;
    QPushButton *m_mtCalUploadButton = nullptr;
    QLabel *m_mtCalStatusLabel = nullptr;
    QLabel *m_mtCompareStatsLabel = nullptr;
    QPushButton *m_hysteresisClearButton = nullptr;
    QPushButton *m_hysteresisAutoButton = nullptr;
    QPushButton *m_hysteresisAutoStopButton = nullptr;
    QLabel *m_hysteresisStatusLabel = nullptr;
    QDoubleSpinBox *m_backlashSpeedSpinBox = nullptr;
    QSpinBox *m_backlashCyclesSpinBox = nullptr;
    QSpinBox *m_backlashSettleMsSpinBox = nullptr;
    QPushButton *m_backlashStartButton = nullptr;
    QPushButton *m_backlashStopButton = nullptr;
    QLabel *m_backlashStatusLabel = nullptr;
    QTimer *m_guideExposureTimer = nullptr;
    QTimer *m_guidePulseTimer = nullptr;
    QTimer *m_backlashTimer = nullptr;
    QTimer *m_hysteresisAutoTimer = nullptr;
    QTimer *m_gotoPhaseTestTimer = nullptr;
    bool m_guideActive = false;
    bool m_guidePulseActive = false;
    bool m_backlashActive = false;

    QChart *m_commandSpeedChart = nullptr;
    QChart *m_errorChart = nullptr;
    QChart *m_mtCompareChart = nullptr;
    QChart *m_hysteresisChart = nullptr;
    QChartView *m_commandSpeedChartView = nullptr;
    QChartView *m_errorChartView = nullptr;
    QChartView *m_mtCompareChartView = nullptr;
    QChartView *m_hysteresisChartView = nullptr;
    QLineSeries *m_commandSpeedSeries = nullptr;
    QLineSeries *m_positionErrorSeries = nullptr;
    QLineSeries *m_mtRawOffsetPositionErrorSeries = nullptr;
    QLineSeries *m_mtPhaseFilteredSeries = nullptr;
    QScatterSeries *m_mtPhasePeakMarkerSeries = nullptr;
    QLineSeries *m_tamaDegreeSeries = nullptr;
    QLineSeries *m_mtRawOffsetDegreeSeries = nullptr;
    QLineSeries *m_hysPosToNegCorrSeries = nullptr;
    QLineSeries *m_hysNegToPosCorrSeries = nullptr;
    QLineSeries *m_hysPosToNegRawSeries = nullptr;
    QLineSeries *m_hysNegToPosRawSeries = nullptr;
    QLineSeries *m_hysCurrentCorrSeries = nullptr;
    QLineSeries *m_hysCurrentRawSeries = nullptr;
    QLineSeries *m_hysCurrentFitSeries = nullptr;
    QValueAxis *m_commandSpeedAxisX = nullptr;
    QValueAxis *m_commandSpeedAxisY = nullptr;
    QValueAxis *m_errorAxisX = nullptr;
    QValueAxis *m_errorAxisY = nullptr;
    QValueAxis *m_mtCompareAxisX = nullptr;
    QValueAxis *m_mtCompareAxisY = nullptr;
    QValueAxis *m_hysteresisAxisX = nullptr;
    QValueAxis *m_hysteresisAxisY = nullptr;

    double m_visibleSeconds = 60.0;
    bool m_hasPreviousDerivedSample = false;
    uint32_t m_previousDec = 0;
    qint64 m_previousElapsedMs = 0;
    double m_commandedDecSpeedHz = 0.0;
    double m_referenceDecSpeedHz = 0.0;
    double m_cumulativePositionErrorCounts = 0.0;
    double m_signedPositionErrorArcsec = 0.0;
    bool m_hasPreviousMtErrorSample = false;
    uint32_t m_previousMtRawOffset25 = 0;
    qint64 m_previousMtErrorElapsedMs = 0;
    double m_mtRawOffsetCumulativePositionErrorCounts = 0.0;
    double m_mtRawOffsetPositionErrorArcsec = 0.0;
    qint64 m_lastChartAxisUpdateMs = -1;
    QVector<QPointF> m_guideErrorSamples;
    static constexpr int PecBins = 512;
    static constexpr int PecPeriodSteps = 25600;
    bool m_pecTraining = false;
    PecTrainState m_pecTrainState = PecTrainState::Idle;
    bool m_pecTableReady = false;
    bool m_pecPlaybackKnown = false;
    bool m_pecPlaybackEnabled = false;
    QVector<double> m_pecErrorSum;
    QVector<int> m_pecCount;
    QVector<double> m_pecCycleErrorSum;
    QVector<int> m_pecCycleCount;
    QVector<double> m_pecTrimSps;
    QVector<double> m_pecMtTemplateSum;
    QVector<int> m_pecMtTemplateCount;
    QVector<double> m_pecMtCycleSum;
    QVector<int> m_pecMtCycleCount;
    QVector<double> m_pecMtTemplate;
    bool m_pecMtTemplateReady = false;
    QVector<double> m_pecPrescanErrorSum;
    QVector<int> m_pecPrescanCount;
    double m_pecPrescanStartSeconds = 0.0;
    double m_pecAlignTargetSteps = 0.0;
    int m_pecPrescanLastPhaseStep = 0;
    int m_pecPrescanPeakBin = 0;
    static constexpr int MtCalBins = 8192;
    static constexpr qint64 MtMonitorMinIntervalMs = 500;
    static constexpr qint64 MtStatsUpdateIntervalMs = 1000;
    static constexpr qint64 ChartAxisUpdateIntervalMs = 500;
    static constexpr qint64 MtPhaseScanDurationMs = 5 * 60 * 1000;
    static constexpr double MtPhaseScanSpeedKHz = 0.300;
    static constexpr int MtPhaseSmoothHalfWindowBins = 12;
    static constexpr double MtPhaseTrimFraction = 0.08;
    static constexpr double MtPhaseMinUsedCycles = 2.5;
    static constexpr int HysBinSteps = 2000;
    static constexpr int HysMaxBins = 512;
    static constexpr int HysAutoCycles = 8;
    static constexpr int HysAutoSegments = HysAutoCycles * 2;
    static constexpr int HysAutoSegmentMs = 5 * 60 * 1000;
    static constexpr int HysMarkCount = 7;
    static constexpr int HysMarks[HysMarkCount] = {50000, 100000, 200000, 300000, 500000, 700000, 1000000};
    struct HysBinStats {
        double corrSum = 0.0;
        double corrSumSq = 0.0;
        double rawSum = 0.0;
        double rawSumSq = 0.0;
        int count = 0;
    };
    struct HysTransitionPoint {
        double steps = 0.0;
        double corrDelta = 0.0;
        double rawDelta = 0.0;
    };
    struct HysMarkStats {
        double corrSum = 0.0;
        double corrSumSq = 0.0;
        double rawSum = 0.0;
        double rawSumSq = 0.0;
        int count = 0;
    };
    struct HysFitResult {
        bool valid = false;
        double amplitudeArcsec = 0.0;
        double tauSteps = 0.0;
        double rmsArcsec = 0.0;
    };
    struct HysAutoRecord {
        int segmentIndex = 0;
        int commandDir = 0;
        int measuredDir = 0;
        qint64 durationMs = 0;
        double endSteps = 0.0;
        int sampleCount = 0;
        double startCorrArcsec = 0.0;
        double endCorrArcsec = 0.0;
        double startRawArcsec = 0.0;
        double endRawArcsec = 0.0;
        HysFitResult fit;
    };
    bool m_mtCalTraining = false;
    bool m_mtCalTableReady = false;
    int m_mtCalDirInverted = 0;
    int32_t m_mtCalOffset25 = 0;
    bool m_mtMonitorHasCalStatus = false;
    int m_mtMonitorDirInverted = 0;
    int32_t m_mtMonitorOffset25 = 0;
    bool m_mtMonitorRawPending = false;
    uint32_t m_mtMonitorPendingTama25 = 0;
    qint64 m_mtMonitorPendingElapsedMs = 0;
    qint64 m_mtMonitorLastRequestMs = -1;
    qint64 m_mtCompareLastStatsUpdateMs = -1;
    bool m_mtUploadActive = false;
    QVector<QPointF> m_mtRawOffsetErrorArcsecSamples;
    struct MtPhaseScanSample {
        double chartSeconds = 0.0;
        double steps = 0.0;
        double rawErrorArcsec = 0.0;
    };
    bool m_mtPhaseScanActive = false;
    qint64 m_mtPhaseScanStartMs = 0;
    qint64 m_mtPhaseScanLastStatusMs = 0;
    double m_mtPhaseScanSpeedHz = 0.0;
    QVector<MtPhaseScanSample> m_mtPhaseScanSamples;
    bool m_mtPhaseRefValid = false;
    int m_mtPhaseRefIdx = 0;
    bool m_mtPhaseRestorePecEnabled = false;
    enum class GotoPhaseTestState {
        Idle,
        FirstMove,
        WaitFirstDone,
        SecondMove,
        WaitSecondDone,
        StartMtScan
    };
    GotoPhaseTestState m_gotoPhaseTestState = GotoPhaseTestState::Idle;
    QStringList m_gotoPhaseTestLog;
    int m_gotoPhaseTestPolls = 0;
    bool m_hysHaveLastTama = false;
    uint32_t m_hysLastTama25 = 0;
    int m_hysLastDir = 0;
    int m_hysTransitionDir = 0;
    double m_hysStepsSinceReverse = 0.0;
    bool m_hysHaveStartError = false;
    double m_hysStartCorrErrorArcsec = 0.0;
    double m_hysStartRawErrorArcsec = 0.0;
    int m_hysPosToNegTransitions = 0;
    int m_hysNegToPosTransitions = 0;
    int m_hysCurrentTransitionDir = 0;
    QVector<HysTransitionPoint> m_hysCurrentPoints;
    HysFitResult m_hysCurrentFit;
    QVector<HysBinStats> m_hysPosToNegBins;
    QVector<HysBinStats> m_hysNegToPosBins;
    QVector<HysMarkStats> m_hysPosToNegMarks;
    QVector<HysMarkStats> m_hysNegToPosMarks;
    bool m_hysAutoActive = false;
    int m_hysAutoSegmentIndex = 0;
    int m_hysAutoCurrentDir = 0;
    qint64 m_hysAutoSegmentStartMs = 0;
    double m_hysAutoSpeedKHz = 1.0;
    QVector<HysAutoRecord> m_hysAutoRecords;
    bool m_mtCalRawPending = false;
    uint32_t m_mtCalPendingTama25 = 0;
    qint64 m_mtCalPendingElapsedMs = 0;
    qint64 m_mtCalLastRequestMs = -1;
    qint64 m_mtCalStartMs = 0;
    double m_mtCalSpeedHz = 0.0;
    bool m_mtCalHaveLastTama = false;
    uint32_t m_mtCalLastTama25 = 0;
    double m_mtCalActualSteps = 0.0;
    int m_mtCalRawTimeouts = 0;
    QVector<bool> m_mtCalRawCoverage;
    int m_mtCalRawCoverageCount = 0;
    bool m_mtCalHaveLastRawBin = false;
    int m_mtCalLastRawBin = 0;
    bool m_mtCalStopArmed = false;
    QVector<uint32_t> m_mtCalRaw21;
    QVector<uint32_t> m_mtCalTama25;
    QVector<int> m_mtCalLut;
    double m_pecStartSeconds = 0.0;
    double m_pecTrainRefSpeedHz = 0.0;
    int m_pecCyclesDone = 0;
    int m_pecLastPhaseStep = 0;
    int m_pecCurrentBin = 0;
    qint64 m_pecLastStatusUpdateMs = 0;
    BacklashState m_backlashState = BacklashState::Idle;
    int m_backlashCurrentDir = 1;
    int m_backlashCyclesDone = 0;
    qint64 m_backlashStateStartMs = 0;
    qint64 m_backlashReverseMs = 0;
    uint32_t m_backlashReverseDec = 0;
    double m_backlashMoveThresholdArcsec = 1.0;
    int m_backlashConsecutive = 0;
    QVector<double> m_backlashPosToNegSteps;
    QVector<double> m_backlashNegToPosSteps;
};

#endif // MAINWINDOW_H
