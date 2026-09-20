#ifndef SNAPSHOT_JSON_WRITER_H
#define SNAPSHOT_JSON_WRITER_H

#include <QIODevice>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMetaType>
#include <QString>
#include <QVariant>
#include <QVector>

#include <cmath>

// Small, forward-only JSON writer used by snapshots. It keeps at most a
// bounded output buffer and never builds a document-sized QJson tree.
class SnapshotJsonWriter
{
public:
    explicit SnapshotJsonWriter(QIODevice &device);

    bool beginObject();
    bool endObject();
    bool beginArray();
    bool endArray();
    bool key(const QString &name);
    bool value(const QVariant &value);
    bool field(const QString &name, const QVariant &value);
    bool finish();
    QString error() const;

private:
    struct Frame {
        enum Kind { Object, Array } kind;
        bool first = true;
        bool expectingKey = false;
    };

    bool beforeValue();
    bool writeRaw(const QByteArray &bytes);
    bool writeToken(const QByteArray &bytes);
    bool writeVariant(const QVariant &value);
    bool writeString(const QString &value);
    bool writeJsonValue(const QVariant &value);
    bool flushBuffer();
    bool fail(const QString &message);

    QIODevice *m_device = nullptr;
    QVector<Frame> m_stack;
    QByteArray m_buffer;
    bool m_rootWritten = false;
    bool m_failed = false;
    bool m_finished = false;
    QString m_error;
};

namespace snapshot_json_writer_detail {
static constexpr qlonglong JsonExactIntegerLimit = 9007199254740991LL;
static constexpr int MaxBufferBytes = 64 * 1024;
static constexpr int WriteChunkBytes = 16 * 1024;
}

inline SnapshotJsonWriter::SnapshotJsonWriter(QIODevice &device) : m_device(&device)
{
    m_buffer.reserve(snapshot_json_writer_detail::MaxBufferBytes);
}

inline bool SnapshotJsonWriter::fail(const QString &message)
{
    if (!m_failed) m_error = message;
    m_failed = true;
    return false;
}

inline bool SnapshotJsonWriter::flushBuffer()
{
    if (m_failed || !m_device || m_buffer.isEmpty()) return !m_failed;
    if (!m_device->isOpen() || !m_device->isWritable())
        return fail(QStringLiteral("snapshot JSON device is not writable"));
    while (!m_buffer.isEmpty()) {
        const qint64 written = m_device->write(m_buffer.constData(), m_buffer.size());
        if (written <= 0)
            return fail(m_device->errorString().isEmpty()
                ? QStringLiteral("snapshot JSON write failed") : m_device->errorString());
        m_buffer.remove(0, static_cast<int>(written));
    }
    return true;
}

inline bool SnapshotJsonWriter::writeRaw(const QByteArray &bytes)
{
    if (m_failed) return false;
    int offset = 0;
    while (offset < bytes.size()) {
        if (m_buffer.size() >= snapshot_json_writer_detail::MaxBufferBytes && !flushBuffer()) return false;
        const int available = snapshot_json_writer_detail::MaxBufferBytes - m_buffer.size();
        const int count = qMin(qMin(snapshot_json_writer_detail::WriteChunkBytes, available), bytes.size() - offset);
        if (count <= 0) return fail(QStringLiteral("snapshot JSON buffer is full"));
        m_buffer.append(bytes.constData() + offset, count);
        offset += count;
    }
    return true;
}

inline bool SnapshotJsonWriter::writeToken(const QByteArray &bytes) { return writeRaw(bytes); }

inline bool SnapshotJsonWriter::beforeValue()
{
    if (m_failed) return false;
    if (m_finished) return fail(QStringLiteral("snapshot JSON writer already finished"));
    if (m_stack.isEmpty()) {
        if (m_rootWritten) return fail(QStringLiteral("snapshot JSON has multiple root values"));
        m_rootWritten = true;
        return true;
    }
    Frame &frame = m_stack.last();
    if (frame.kind == Frame::Object) {
        if (frame.expectingKey) return fail(QStringLiteral("snapshot JSON object value requires a key"));
        frame.expectingKey = true;
        return true;
    }
    if (!frame.first && !writeRaw(QByteArrayLiteral(","))) return false;
    frame.first = false;
    return true;
}

inline bool SnapshotJsonWriter::beginObject()
{
    if (!beforeValue() || !writeRaw(QByteArrayLiteral("{"))) return false;
    Frame frame; frame.kind = Frame::Object; frame.expectingKey = true; m_stack.append(frame); return true;
}

inline bool SnapshotJsonWriter::endObject()
{
    if (m_failed || m_stack.isEmpty() || m_stack.last().kind != Frame::Object)
        return fail(QStringLiteral("snapshot JSON object nesting mismatch"));
    if (!m_stack.last().expectingKey) return fail(QStringLiteral("snapshot JSON object is missing a value"));
    m_stack.removeLast(); return writeRaw(QByteArrayLiteral("}"));
}

inline bool SnapshotJsonWriter::beginArray()
{
    if (!beforeValue() || !writeRaw(QByteArrayLiteral("["))) return false;
    Frame frame; frame.kind = Frame::Array; m_stack.append(frame); return true;
}

inline bool SnapshotJsonWriter::endArray()
{
    if (m_failed || m_stack.isEmpty() || m_stack.last().kind != Frame::Array)
        return fail(QStringLiteral("snapshot JSON array nesting mismatch"));
    m_stack.removeLast(); return writeRaw(QByteArrayLiteral("]"));
}

inline bool SnapshotJsonWriter::key(const QString &name)
{
    if (m_failed || m_stack.isEmpty() || m_stack.last().kind != Frame::Object)
        return fail(QStringLiteral("snapshot JSON key outside object"));
    Frame &frame = m_stack.last();
    if (!frame.expectingKey) return fail(QStringLiteral("snapshot JSON object key follows an incomplete value"));
    if (!frame.first && !writeRaw(QByteArrayLiteral(","))) return false;
    frame.first = false; frame.expectingKey = false;
    return writeString(name) && writeRaw(QByteArrayLiteral(":"));
}

inline bool SnapshotJsonWriter::field(const QString &name, const QVariant &value)
{ return key(name) && this->value(value); }

inline bool SnapshotJsonWriter::writeString(const QString &value)
{
    if (!writeRaw(QByteArrayLiteral("\""))) return false;
    // Escape small UTF-16 chunks; never split a surrogate pair between chunks.
    for (int offset = 0; offset < value.size();) {
        int count = qMin(4096, int(value.size() - offset));
        if (offset + count < value.size() && value.at(offset + count - 1).isHighSurrogate()
            && value.at(offset + count).isLowSurrogate()) --count;
        QJsonArray array;
        array.append(QJsonValue(value.mid(offset, count)));
        const QByteArray encoded = QJsonDocument(array).toJson(QJsonDocument::Compact);
        if (encoded.size() < 4)
            return fail(QStringLiteral("snapshot JSON string encoding failed"));
        if (!writeRaw(encoded.mid(2, encoded.size() - 4))) return false;
        offset += count;
    }
    return writeRaw(QByteArrayLiteral("\""));
}

inline bool SnapshotJsonWriter::writeJsonValue(const QVariant &value)
{
    QJsonValue json;
    if (value.userType() == QMetaType::QJsonObject) json = value.value<QJsonObject>();
    else if (value.userType() == QMetaType::QJsonArray) json = value.value<QJsonArray>();
    else json = value.value<QJsonValue>();
    if (json.isObject()) {
        if (!writeRaw(QByteArrayLiteral("{"))) return false;
        Frame frame; frame.kind = Frame::Object; frame.expectingKey = true; m_stack.append(frame);
        const QJsonObject object = json.toObject();
        for (auto it = object.constBegin(); it != object.constEnd(); ++it)
            if (!field(it.key(), QVariant::fromValue(QJsonValue(it.value())))) return false;
        return endObject();
    }
    if (json.isArray()) {
        if (!writeRaw(QByteArrayLiteral("["))) return false;
        Frame frame; frame.kind = Frame::Array; m_stack.append(frame);
        const QJsonArray array = json.toArray();
        for (const QJsonValue &item : array)
            if (!this->value(QVariant::fromValue(item))) return false;
        return endArray();
    }
    if (json.isString()) return writeString(json.toString());
    // Raw QJson numbers already have Qt's JSON semantics (including Qt6 int64).
    QJsonArray scalar;
    scalar.append(json);
    const QByteArray encoded = QJsonDocument(scalar).toJson(QJsonDocument::Compact);
    return writeRaw(encoded.mid(1, encoded.size() - 2));
}

inline bool SnapshotJsonWriter::writeVariant(const QVariant &value)
{
    if (!value.isValid()) return writeToken(QByteArrayLiteral("null"));
    const int type = value.userType();
    switch (type) {
    case QMetaType::Nullptr: return writeToken(QByteArrayLiteral("null"));
    case QMetaType::Bool: return writeToken(value.toBool() ? QByteArrayLiteral("true") : QByteArrayLiteral("false"));
    case QMetaType::Int: return writeToken(QByteArray::number(value.toInt()));
    case QMetaType::UInt: return writeToken(QByteArray::number(value.toUInt()));
    case QMetaType::LongLong: {
        const qlonglong number = value.toLongLong();
        if (number < -snapshot_json_writer_detail::JsonExactIntegerLimit || number > snapshot_json_writer_detail::JsonExactIntegerLimit) return fail(QStringLiteral("unsafe JSON integer"));
        return writeToken(QByteArray::number(number));
    }
    case QMetaType::ULongLong: {
        const qulonglong number = value.toULongLong();
        if (number > static_cast<qulonglong>(snapshot_json_writer_detail::JsonExactIntegerLimit)) return fail(QStringLiteral("unsafe JSON integer"));
        return writeToken(QByteArray::number(number));
    }
    case QMetaType::Double: {
        const double number = value.toDouble();
        if (!std::isfinite(number)) return fail(QStringLiteral("non-finite JSON number"));
        return writeToken(QString::number(number, 'g', 17).toLatin1());
    }
    case QMetaType::QString: return writeString(value.toString());
    case QMetaType::QStringList: {
        if (!writeRaw(QByteArrayLiteral("["))) return false;
        Frame frame; frame.kind = Frame::Array; m_stack.append(frame);
        for (const QString &item : value.toStringList()) if (!this->value(item)) return false;
        return endArray();
    }
    case QMetaType::QVariantList: {
        if (!writeRaw(QByteArrayLiteral("["))) return false;
        Frame frame; frame.kind = Frame::Array; m_stack.append(frame);
        for (const QVariant &item : value.toList()) if (!this->value(item)) return false;
        return endArray();
    }
    case QMetaType::QVariantMap: {
        if (!writeRaw(QByteArrayLiteral("{"))) return false;
        Frame frame; frame.kind = Frame::Object; frame.expectingKey = true; m_stack.append(frame);
        const QVariantMap map = value.toMap();
        for (auto it = map.constBegin(); it != map.constEnd(); ++it) if (!field(it.key(), it.value())) return false;
        return endObject();
    }
    case QMetaType::QJsonValue:
    case QMetaType::QJsonObject:
    case QMetaType::QJsonArray: return writeJsonValue(value);
    default: return fail(QStringLiteral("unsupported JSON metatype: %1").arg(QString::fromLatin1(value.typeName())));
    }
}

inline bool SnapshotJsonWriter::value(const QVariant &value)
{ return beforeValue() && writeVariant(value); }

inline bool SnapshotJsonWriter::finish()
{
    if (m_finished) return !m_failed;
    m_finished = true;
    if (m_failed) return false;
    if (!m_stack.isEmpty()) return fail(QStringLiteral("snapshot JSON has unclosed containers"));
    if (!m_rootWritten) return fail(QStringLiteral("snapshot JSON has no root value"));
    return flushBuffer();
}

inline QString SnapshotJsonWriter::error() const { return m_error; }

#endif // SNAPSHOT_JSON_WRITER_H
