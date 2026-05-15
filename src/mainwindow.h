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
#include <QtCharts/QValueAxis>

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
    void updateReadInterval(int intervalMs);
    void updateVisibleSeconds(int seconds);
    void refreshMountPorts();
    void connectMount();
    void disconnectMount();
    void slewDecPositive();
    void slewDecNegative();
    void stopDec();
    void startGuideSimulation();
    void stopGuideSimulation();
    void runGuideExposure();
    void finishGuidePulse();

private:
    void setupUi();
    void setupMountUi(QVBoxLayout *root);
    void setupGuideUi(QVBoxLayout *root);
    void updateStatus(const QString &message);
    void updateMountStatus(const QString &message);
    void updateGuideStatus(const QString &message);
    void handleWorkerStopped();
    void appendSample(const EncoderSample &sample);
    void updateYAxisForVisibleRange(QLineSeries *series, QValueAxis *axis, double minVisibleSeconds, double minPadding);
    void resetChart();
    double selectedMountSpeedKHz() const;
    void setDecSpeedState(double commandSpeedKHz, double referenceSpeedKHz);
    bool sendGuideSpeed(double commandSpeedKHz, double referenceSpeedKHz);
    double guideCorrectionArcsecPerSecond() const;
    void appendGuideErrorSample(qint64 elapsedMs, double errorArcsec);
    double currentGuideRmsArcsec() const;
    void resetGuideRms();
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
    QSpinBox *m_intervalSpinBox = nullptr;
    QSpinBox *m_visibleSecondsSpinBox = nullptr;
    QCheckBox *m_bulkReadCheckBox = nullptr;
    QCheckBox *m_triggerCheckBox = nullptr;

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
    QDoubleSpinBox *m_guideBaseSpeedSpinBox = nullptr;
    QDoubleSpinBox *m_guideDeltaSpeedSpinBox = nullptr;
    QSpinBox *m_guideExposureMsSpinBox = nullptr;
    QSpinBox *m_guideAggressivenessSpinBox = nullptr;
    QSpinBox *m_guideMaxPulseMsSpinBox = nullptr;
    QPushButton *m_guideStartButton = nullptr;
    QPushButton *m_guideStopButton = nullptr;
    QLabel *m_guideRmsLabel = nullptr;
    QLabel *m_guideStatusLabel = nullptr;
    QTimer *m_guideExposureTimer = nullptr;
    QTimer *m_guidePulseTimer = nullptr;
    bool m_guideActive = false;
    bool m_guidePulseActive = false;

    QChart *m_encoderChart = nullptr;
    QChart *m_commandSpeedChart = nullptr;
    QChart *m_speedChart = nullptr;
    QChart *m_errorChart = nullptr;
    QChartView *m_encoderChartView = nullptr;
    QChartView *m_commandSpeedChartView = nullptr;
    QChartView *m_speedChartView = nullptr;
    QChartView *m_errorChartView = nullptr;
    QLineSeries *m_decSeries = nullptr;
    QLineSeries *m_commandSpeedSeries = nullptr;
    QLineSeries *m_actualSpeedSeries = nullptr;
    QLineSeries *m_positionErrorSeries = nullptr;
    QValueAxis *m_encoderAxisX = nullptr;
    QValueAxis *m_encoderAxisY = nullptr;
    QValueAxis *m_commandSpeedAxisX = nullptr;
    QValueAxis *m_commandSpeedAxisY = nullptr;
    QValueAxis *m_speedAxisX = nullptr;
    QValueAxis *m_speedAxisY = nullptr;
    QValueAxis *m_errorAxisX = nullptr;
    QValueAxis *m_errorAxisY = nullptr;

    double m_visibleSeconds = 60.0;
    bool m_hasPreviousDerivedSample = false;
    uint32_t m_previousDec = 0;
    qint64 m_previousElapsedMs = 0;
    double m_commandedDecSpeedHz = 0.0;
    double m_referenceDecSpeedHz = 0.0;
    double m_cumulativePositionErrorCounts = 0.0;
    double m_signedPositionErrorArcsec = 0.0;
    QVector<QPointF> m_guideErrorSamples;
};

#endif // MAINWINDOW_H
