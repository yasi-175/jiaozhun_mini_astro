/****************************************************************************
** Meta object code from reading C++ file 'mainwindow.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "src/mainwindow.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mainwindow.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MainWindow_t {
    QByteArrayData data[43];
    char stringdata0[718];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MainWindow_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MainWindow_t qt_meta_stringdata_MainWindow = {
    {
QT_MOC_LITERAL(0, 0, 10), // "MainWindow"
QT_MOC_LITERAL(1, 11, 12), // "startReading"
QT_MOC_LITERAL(2, 24, 0), // ""
QT_MOC_LITERAL(3, 25, 11), // "stopReading"
QT_MOC_LITERAL(4, 37, 14), // "clearChartData"
QT_MOC_LITERAL(5, 52, 18), // "updateReadInterval"
QT_MOC_LITERAL(6, 71, 10), // "intervalMs"
QT_MOC_LITERAL(7, 82, 20), // "updateVisibleSeconds"
QT_MOC_LITERAL(8, 103, 7), // "seconds"
QT_MOC_LITERAL(9, 111, 21), // "updateChartVisibility"
QT_MOC_LITERAL(10, 133, 27), // "updateErrorSeriesVisibility"
QT_MOC_LITERAL(11, 161, 17), // "refreshMountPorts"
QT_MOC_LITERAL(12, 179, 19), // "handleMountResponse"
QT_MOC_LITERAL(13, 199, 4), // "line"
QT_MOC_LITERAL(14, 204, 12), // "connectMount"
QT_MOC_LITERAL(15, 217, 15), // "disconnectMount"
QT_MOC_LITERAL(16, 233, 15), // "slewDecPositive"
QT_MOC_LITERAL(17, 249, 15), // "slewDecNegative"
QT_MOC_LITERAL(18, 265, 7), // "stopDec"
QT_MOC_LITERAL(19, 273, 20), // "startGuideSimulation"
QT_MOC_LITERAL(20, 294, 19), // "stopGuideSimulation"
QT_MOC_LITERAL(21, 314, 16), // "runGuideExposure"
QT_MOC_LITERAL(22, 331, 16), // "finishGuidePulse"
QT_MOC_LITERAL(23, 348, 16), // "startPecTraining"
QT_MOC_LITERAL(24, 365, 15), // "stopPecTraining"
QT_MOC_LITERAL(25, 381, 14), // "uploadPecTable"
QT_MOC_LITERAL(26, 396, 17), // "enablePecPlayback"
QT_MOC_LITERAL(27, 414, 18), // "disablePecPlayback"
QT_MOC_LITERAL(28, 433, 14), // "queryPecStatus"
QT_MOC_LITERAL(29, 448, 18), // "startGotoPhaseTest"
QT_MOC_LITERAL(30, 467, 20), // "runGotoPhaseTestStep"
QT_MOC_LITERAL(31, 488, 16), // "startMtPhaseScan"
QT_MOC_LITERAL(32, 505, 15), // "stopMtPhaseScan"
QT_MOC_LITERAL(33, 521, 18), // "startMtCalibration"
QT_MOC_LITERAL(34, 540, 17), // "stopMtCalibration"
QT_MOC_LITERAL(35, 558, 19), // "uploadMtCalibration"
QT_MOC_LITERAL(36, 578, 23), // "startHysteresisAutoTest"
QT_MOC_LITERAL(37, 602, 22), // "stopHysteresisAutoTest"
QT_MOC_LITERAL(38, 625, 21), // "runHysteresisAutoStep"
QT_MOC_LITERAL(39, 647, 17), // "startBacklashTest"
QT_MOC_LITERAL(40, 665, 16), // "stopBacklashTest"
QT_MOC_LITERAL(41, 682, 15), // "runBacklashStep"
QT_MOC_LITERAL(42, 698, 19) // "clearHysteresisData"

    },
    "MainWindow\0startReading\0\0stopReading\0"
    "clearChartData\0updateReadInterval\0"
    "intervalMs\0updateVisibleSeconds\0seconds\0"
    "updateChartVisibility\0updateErrorSeriesVisibility\0"
    "refreshMountPorts\0handleMountResponse\0"
    "line\0connectMount\0disconnectMount\0"
    "slewDecPositive\0slewDecNegative\0stopDec\0"
    "startGuideSimulation\0stopGuideSimulation\0"
    "runGuideExposure\0finishGuidePulse\0"
    "startPecTraining\0stopPecTraining\0"
    "uploadPecTable\0enablePecPlayback\0"
    "disablePecPlayback\0queryPecStatus\0"
    "startGotoPhaseTest\0runGotoPhaseTestStep\0"
    "startMtPhaseScan\0stopMtPhaseScan\0"
    "startMtCalibration\0stopMtCalibration\0"
    "uploadMtCalibration\0startHysteresisAutoTest\0"
    "stopHysteresisAutoTest\0runHysteresisAutoStep\0"
    "startBacklashTest\0stopBacklashTest\0"
    "runBacklashStep\0clearHysteresisData"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      38,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,  204,    2, 0x08 /* Private */,
       3,    0,  205,    2, 0x08 /* Private */,
       4,    0,  206,    2, 0x08 /* Private */,
       5,    1,  207,    2, 0x08 /* Private */,
       7,    1,  210,    2, 0x08 /* Private */,
       9,    0,  213,    2, 0x08 /* Private */,
      10,    0,  214,    2, 0x08 /* Private */,
      11,    0,  215,    2, 0x08 /* Private */,
      12,    1,  216,    2, 0x08 /* Private */,
      14,    0,  219,    2, 0x08 /* Private */,
      15,    0,  220,    2, 0x08 /* Private */,
      16,    0,  221,    2, 0x08 /* Private */,
      17,    0,  222,    2, 0x08 /* Private */,
      18,    0,  223,    2, 0x08 /* Private */,
      19,    0,  224,    2, 0x08 /* Private */,
      20,    0,  225,    2, 0x08 /* Private */,
      21,    0,  226,    2, 0x08 /* Private */,
      22,    0,  227,    2, 0x08 /* Private */,
      23,    0,  228,    2, 0x08 /* Private */,
      24,    0,  229,    2, 0x08 /* Private */,
      25,    0,  230,    2, 0x08 /* Private */,
      26,    0,  231,    2, 0x08 /* Private */,
      27,    0,  232,    2, 0x08 /* Private */,
      28,    0,  233,    2, 0x08 /* Private */,
      29,    0,  234,    2, 0x08 /* Private */,
      30,    0,  235,    2, 0x08 /* Private */,
      31,    0,  236,    2, 0x08 /* Private */,
      32,    0,  237,    2, 0x08 /* Private */,
      33,    0,  238,    2, 0x08 /* Private */,
      34,    0,  239,    2, 0x08 /* Private */,
      35,    0,  240,    2, 0x08 /* Private */,
      36,    0,  241,    2, 0x08 /* Private */,
      37,    0,  242,    2, 0x08 /* Private */,
      38,    0,  243,    2, 0x08 /* Private */,
      39,    0,  244,    2, 0x08 /* Private */,
      40,    0,  245,    2, 0x08 /* Private */,
      41,    0,  246,    2, 0x08 /* Private */,
      42,    0,  247,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    6,
    QMetaType::Void, QMetaType::Int,    8,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::QString,   13,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void MainWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MainWindow *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->startReading(); break;
        case 1: _t->stopReading(); break;
        case 2: _t->clearChartData(); break;
        case 3: _t->updateReadInterval((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->updateVisibleSeconds((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 5: _t->updateChartVisibility(); break;
        case 6: _t->updateErrorSeriesVisibility(); break;
        case 7: _t->refreshMountPorts(); break;
        case 8: _t->handleMountResponse((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 9: _t->connectMount(); break;
        case 10: _t->disconnectMount(); break;
        case 11: _t->slewDecPositive(); break;
        case 12: _t->slewDecNegative(); break;
        case 13: _t->stopDec(); break;
        case 14: _t->startGuideSimulation(); break;
        case 15: _t->stopGuideSimulation(); break;
        case 16: _t->runGuideExposure(); break;
        case 17: _t->finishGuidePulse(); break;
        case 18: _t->startPecTraining(); break;
        case 19: _t->stopPecTraining(); break;
        case 20: _t->uploadPecTable(); break;
        case 21: _t->enablePecPlayback(); break;
        case 22: _t->disablePecPlayback(); break;
        case 23: _t->queryPecStatus(); break;
        case 24: _t->startGotoPhaseTest(); break;
        case 25: _t->runGotoPhaseTestStep(); break;
        case 26: _t->startMtPhaseScan(); break;
        case 27: _t->stopMtPhaseScan(); break;
        case 28: _t->startMtCalibration(); break;
        case 29: _t->stopMtCalibration(); break;
        case 30: _t->uploadMtCalibration(); break;
        case 31: _t->startHysteresisAutoTest(); break;
        case 32: _t->stopHysteresisAutoTest(); break;
        case 33: _t->runHysteresisAutoStep(); break;
        case 34: _t->startBacklashTest(); break;
        case 35: _t->stopBacklashTest(); break;
        case 36: _t->runBacklashStep(); break;
        case 37: _t->clearHysteresisData(); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MainWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QMainWindow::staticMetaObject>(),
    qt_meta_stringdata_MainWindow.data,
    qt_meta_data_MainWindow,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MainWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MainWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MainWindow.stringdata0))
        return static_cast<void*>(this);
    return QMainWindow::qt_metacast(_clname);
}

int MainWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QMainWindow::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 38)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 38;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 38)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 38;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
