#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "encoder_worker.h"

#include <QCheckBox>
#include <QElapsedTimer>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QSpinBox>
#include <QThread>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

QT_CHARTS_USE_NAMESPACE

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

private:
    void setupUi();
    void updateStatus(const QString &message);
    void handleWorkerStopped();
    void appendSample(const EncoderSample &sample);
    void updateYAxisForVisibleRange(double minVisibleSeconds);
    void resetChart();

    QString m_libraryPath;
    QString m_deviceName;
    int m_intervalMs = 200;

    QElapsedTimer m_elapsed;
    QThread *m_workerThread = nullptr;
    EncoderWorker *m_worker = nullptr;

    QLabel *m_statusLabel = nullptr;
    QLabel *m_decLabel = nullptr;
    QLabel *m_decDegreeLabel = nullptr;
    QLabel *m_actualIntervalLabel = nullptr;
    QLabel *m_readDurationLabel = nullptr;
    QPushButton *m_startButton = nullptr;
    QPushButton *m_stopButton = nullptr;
    QSpinBox *m_intervalSpinBox = nullptr;
    QSpinBox *m_visibleSecondsSpinBox = nullptr;
    QCheckBox *m_bulkReadCheckBox = nullptr;
    QCheckBox *m_triggerCheckBox = nullptr;

    QChart *m_chart = nullptr;
    QChartView *m_chartView = nullptr;
    QLineSeries *m_decSeries = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    double m_minY = 0.0;
    double m_maxY = 1.0;
    bool m_hasSample = false;
    double m_visibleSeconds = 60.0;
};

#endif // MAINWINDOW_H
