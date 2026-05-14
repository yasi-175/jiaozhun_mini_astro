/****************************************************************************
** Meta object code from reading C++ file 'mount_controller.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "src/mount_controller.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'mount_controller.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_MountController_t {
    QByteArrayData data[19];
    char stringdata0[236];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_MountController_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_MountController_t qt_meta_stringdata_MountController = {
    {
QT_MOC_LITERAL(0, 0, 15), // "MountController"
QT_MOC_LITERAL(1, 16, 17), // "connectionChanged"
QT_MOC_LITERAL(2, 34, 0), // ""
QT_MOC_LITERAL(3, 35, 9), // "connected"
QT_MOC_LITERAL(4, 45, 13), // "statusChanged"
QT_MOC_LITERAL(5, 59, 7), // "message"
QT_MOC_LITERAL(6, 67, 16), // "responseReceived"
QT_MOC_LITERAL(7, 84, 4), // "line"
QT_MOC_LITERAL(8, 89, 13), // "connectToPort"
QT_MOC_LITERAL(9, 103, 8), // "portName"
QT_MOC_LITERAL(10, 112, 8), // "baudRate"
QT_MOC_LITERAL(11, 121, 18), // "disconnectFromPort"
QT_MOC_LITERAL(12, 140, 7), // "slewDec"
QT_MOC_LITERAL(13, 148, 8), // "speedKHz"
QT_MOC_LITERAL(14, 157, 7), // "stopDec"
QT_MOC_LITERAL(15, 165, 17), // "readAvailableData"
QT_MOC_LITERAL(16, 183, 17), // "handleSerialError"
QT_MOC_LITERAL(17, 201, 28), // "QSerialPort::SerialPortError"
QT_MOC_LITERAL(18, 230, 5) // "error"

    },
    "MountController\0connectionChanged\0\0"
    "connected\0statusChanged\0message\0"
    "responseReceived\0line\0connectToPort\0"
    "portName\0baudRate\0disconnectFromPort\0"
    "slewDec\0speedKHz\0stopDec\0readAvailableData\0"
    "handleSerialError\0QSerialPort::SerialPortError\0"
    "error"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_MountController[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       9,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       3,       // signalCount

 // signals: name, argc, parameters, tag, flags
       1,    1,   59,    2, 0x06 /* Public */,
       4,    1,   62,    2, 0x06 /* Public */,
       6,    1,   65,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       8,    2,   68,    2, 0x0a /* Public */,
      11,    0,   73,    2, 0x0a /* Public */,
      12,    1,   74,    2, 0x0a /* Public */,
      14,    0,   77,    2, 0x0a /* Public */,
      15,    0,   78,    2, 0x08 /* Private */,
      16,    1,   79,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, QMetaType::Bool,    3,
    QMetaType::Void, QMetaType::QString,    5,
    QMetaType::Void, QMetaType::QString,    7,

 // slots: parameters
    QMetaType::Bool, QMetaType::QString, QMetaType::Int,    9,   10,
    QMetaType::Void,
    QMetaType::Bool, QMetaType::Double,   13,
    QMetaType::Bool,
    QMetaType::Void,
    QMetaType::Void, 0x80000000 | 17,   18,

       0        // eod
};

void MountController::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<MountController *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->connectionChanged((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 1: _t->statusChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->responseReceived((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 3: { bool _r = _t->connectToPort((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< int(*)>(_a[2])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 4: _t->disconnectFromPort(); break;
        case 5: { bool _r = _t->slewDec((*reinterpret_cast< double(*)>(_a[1])));
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 6: { bool _r = _t->stopDec();
            if (_a[0]) *reinterpret_cast< bool*>(_a[0]) = std::move(_r); }  break;
        case 7: _t->readAvailableData(); break;
        case 8: _t->handleSerialError((*reinterpret_cast< QSerialPort::SerialPortError(*)>(_a[1]))); break;
        default: ;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (MountController::*)(bool );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MountController::connectionChanged)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (MountController::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MountController::statusChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (MountController::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&MountController::responseReceived)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject MountController::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_MountController.data,
    qt_meta_data_MountController,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *MountController::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *MountController::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_MountController.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int MountController::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void MountController::connectionChanged(bool _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void MountController::statusChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void MountController::responseReceived(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 2, _a);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
