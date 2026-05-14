/****************************************************************************
** Meta object code from reading C++ file 'encoder_worker.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.15.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include <memory>
#include "src/encoder_worker.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'encoder_worker.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.15.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_EncoderWorker_t {
    QByteArrayData data[20];
    char stringdata0[225];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_EncoderWorker_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_EncoderWorker_t qt_meta_stringdata_EncoderWorker = {
    {
QT_MOC_LITERAL(0, 0, 13), // "EncoderWorker"
QT_MOC_LITERAL(1, 14, 11), // "sampleReady"
QT_MOC_LITERAL(2, 26, 0), // ""
QT_MOC_LITERAL(3, 27, 13), // "EncoderSample"
QT_MOC_LITERAL(4, 41, 6), // "sample"
QT_MOC_LITERAL(5, 48, 13), // "statusChanged"
QT_MOC_LITERAL(6, 62, 7), // "message"
QT_MOC_LITERAL(7, 70, 7), // "stopped"
QT_MOC_LITERAL(8, 78, 5), // "start"
QT_MOC_LITERAL(9, 84, 11), // "libraryPath"
QT_MOC_LITERAL(10, 96, 10), // "deviceName"
QT_MOC_LITERAL(11, 107, 10), // "intervalMs"
QT_MOC_LITERAL(12, 118, 15), // "bulkReadEnabled"
QT_MOC_LITERAL(13, 134, 14), // "triggerEnabled"
QT_MOC_LITERAL(14, 149, 4), // "stop"
QT_MOC_LITERAL(15, 154, 13), // "setIntervalMs"
QT_MOC_LITERAL(16, 168, 18), // "setBulkReadEnabled"
QT_MOC_LITERAL(17, 187, 7), // "enabled"
QT_MOC_LITERAL(18, 195, 17), // "setTriggerEnabled"
QT_MOC_LITERAL(19, 213, 11) // "pollEncoder"

    },
    "EncoderWorker\0sampleReady\0\0EncoderSample\0"
    "sample\0statusChanged\0message\0stopped\0"
    "start\0libraryPath\0deviceName\0intervalMs\0"
    "bulkReadEnabled\0triggerEnabled\0stop\0"
    "setIntervalMs\0setBulkReadEnabled\0"
    "enabled\0setTriggerEnabled\0pollEncoder"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_EncoderWorker[] = {

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
       5,    1,   62,    2, 0x06 /* Public */,
       7,    0,   65,    2, 0x06 /* Public */,

 // slots: name, argc, parameters, tag, flags
       8,    5,   66,    2, 0x0a /* Public */,
      14,    0,   77,    2, 0x0a /* Public */,
      15,    1,   78,    2, 0x0a /* Public */,
      16,    1,   81,    2, 0x0a /* Public */,
      18,    1,   84,    2, 0x0a /* Public */,
      19,    0,   87,    2, 0x08 /* Private */,

 // signals: parameters
    QMetaType::Void, 0x80000000 | 3,    4,
    QMetaType::Void, QMetaType::QString,    6,
    QMetaType::Void,

 // slots: parameters
    QMetaType::Void, QMetaType::QString, QMetaType::QString, QMetaType::Int, QMetaType::Bool, QMetaType::Bool,    9,   10,   11,   12,   13,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,   11,
    QMetaType::Void, QMetaType::Bool,   17,
    QMetaType::Void, QMetaType::Bool,   17,
    QMetaType::Void,

       0        // eod
};

void EncoderWorker::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<EncoderWorker *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->sampleReady((*reinterpret_cast< const EncoderSample(*)>(_a[1]))); break;
        case 1: _t->statusChanged((*reinterpret_cast< const QString(*)>(_a[1]))); break;
        case 2: _t->stopped(); break;
        case 3: _t->start((*reinterpret_cast< const QString(*)>(_a[1])),(*reinterpret_cast< const QString(*)>(_a[2])),(*reinterpret_cast< int(*)>(_a[3])),(*reinterpret_cast< bool(*)>(_a[4])),(*reinterpret_cast< bool(*)>(_a[5]))); break;
        case 4: _t->stop(); break;
        case 5: _t->setIntervalMs((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->setBulkReadEnabled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 7: _t->setTriggerEnabled((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 8: _t->pollEncoder(); break;
        default: ;
        }
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        switch (_id) {
        default: *reinterpret_cast<int*>(_a[0]) = -1; break;
        case 0:
            switch (*reinterpret_cast<int*>(_a[1])) {
            default: *reinterpret_cast<int*>(_a[0]) = -1; break;
            case 0:
                *reinterpret_cast<int*>(_a[0]) = qRegisterMetaType< EncoderSample >(); break;
            }
            break;
        }
    } else if (_c == QMetaObject::IndexOfMethod) {
        int *result = reinterpret_cast<int *>(_a[0]);
        {
            using _t = void (EncoderWorker::*)(const EncoderSample & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&EncoderWorker::sampleReady)) {
                *result = 0;
                return;
            }
        }
        {
            using _t = void (EncoderWorker::*)(const QString & );
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&EncoderWorker::statusChanged)) {
                *result = 1;
                return;
            }
        }
        {
            using _t = void (EncoderWorker::*)();
            if (*reinterpret_cast<_t *>(_a[1]) == static_cast<_t>(&EncoderWorker::stopped)) {
                *result = 2;
                return;
            }
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject EncoderWorker::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_meta_stringdata_EncoderWorker.data,
    qt_meta_data_EncoderWorker,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *EncoderWorker::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *EncoderWorker::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_EncoderWorker.stringdata0))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int EncoderWorker::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
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
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void EncoderWorker::sampleReady(const EncoderSample & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 0, _a);
}

// SIGNAL 1
void EncoderWorker::statusChanged(const QString & _t1)
{
    void *_a[] = { nullptr, const_cast<void*>(reinterpret_cast<const void*>(std::addressof(_t1))) };
    QMetaObject::activate(this, &staticMetaObject, 1, _a);
}

// SIGNAL 2
void EncoderWorker::stopped()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
