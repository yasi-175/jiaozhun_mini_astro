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
    QByteArrayData data[18];
    char stringdata0[258];
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
QT_MOC_LITERAL(4, 37, 18), // "updateReadInterval"
QT_MOC_LITERAL(5, 56, 10), // "intervalMs"
QT_MOC_LITERAL(6, 67, 20), // "updateVisibleSeconds"
QT_MOC_LITERAL(7, 88, 7), // "seconds"
QT_MOC_LITERAL(8, 96, 17), // "refreshMountPorts"
QT_MOC_LITERAL(9, 114, 12), // "connectMount"
QT_MOC_LITERAL(10, 127, 15), // "disconnectMount"
QT_MOC_LITERAL(11, 143, 15), // "slewDecPositive"
QT_MOC_LITERAL(12, 159, 15), // "slewDecNegative"
QT_MOC_LITERAL(13, 175, 7), // "stopDec"
QT_MOC_LITERAL(14, 183, 20), // "startGuideSimulation"
QT_MOC_LITERAL(15, 204, 19), // "stopGuideSimulation"
QT_MOC_LITERAL(16, 224, 16), // "runGuideExposure"
QT_MOC_LITERAL(17, 241, 16) // "finishGuidePulse"

    },
    "MainWindow\0startReading\0\0stopReading\0"
    "updateReadInterval\0intervalMs\0"
    "updateVisibleSeconds\0seconds\0"
    "refreshMountPorts\0connectMount\0"
    "disconnectMount\0slewDecPositive\0"
    "slewDecNegative\0stopDec\0startGuideSimulation\0"
    "stopGuideSimulation\0runGuideExposure\0"
    "finishGuidePulse"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MainWindow[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
      14,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   84,    2, 0x08 /* Private */,
       3,    0,   85,    2, 0x08 /* Private */,
       4,    1,   86,    2, 0x08 /* Private */,
       6,    1,   89,    2, 0x08 /* Private */,
       8,    0,   92,    2, 0x08 /* Private */,
       9,    0,   93,    2, 0x08 /* Private */,
      10,    0,   94,    2, 0x08 /* Private */,
      11,    0,   95,    2, 0x08 /* Private */,
      12,    0,   96,    2, 0x08 /* Private */,
      13,    0,   97,    2, 0x08 /* Private */,
      14,    0,   98,    2, 0x08 /* Private */,
      15,    0,   99,    2, 0x08 /* Private */,
      16,    0,  100,    2, 0x08 /* Private */,
      17,    0,  101,    2, 0x08 /* Private */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    5,
    QMetaType::Void, QMetaType::Int,    7,
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
        case 2: _t->updateReadInterval((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 3: _t->updateVisibleSeconds((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->refreshMountPorts(); break;
        case 5: _t->connectMount(); break;
        case 6: _t->disconnectMount(); break;
        case 7: _t->slewDecPositive(); break;
        case 8: _t->slewDecNegative(); break;
        case 9: _t->stopDec(); break;
        case 10: _t->startGuideSimulation(); break;
        case 11: _t->stopGuideSimulation(); break;
        case 12: _t->runGuideExposure(); break;
        case 13: _t->finishGuidePulse(); break;
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
        if (_id < 14)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 14;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 14)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 14;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
