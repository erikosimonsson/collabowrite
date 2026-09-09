#pragma once

#include "textedit.h"

#include <QByteArray>
#include <QDataStream>
#include <QIODevice>
#include <QStringDecoder>
#include <QUuid>
#include <QtGlobal>

#include <limits>

namespace Protocol {
    inline constexpr qsizetype HeaderSize = 4;
    inline constexpr quint32 MaxPayloadSize = 1024 * 1024;
    inline constexpr qsizetype EditMetadataSize = 32;
    inline constexpr qsizetype SnapshotMetadataSize = 8;
    inline constexpr qsizetype MaxDocumentBytes = MaxPayloadSize - 1 - SnapshotMetadataSize;

    enum class MessageType : quint8 {
        DocumentSnapshot = 1,
        Error = 2,
        EditRequest = 3
    };

    struct EditRequest {
        QUuid operationId;
        quint64 baseRevision = 0;
        TextEdit edit;
    };

    inline QByteArray encodeEditRequest(const EditRequest &request) {
        const TextEdit &edit = request.edit;
        const quint64 maxIndex = std::numeric_limits<quint32>::max();

        if (request.operationId.isNull() || edit.position < 0 || edit.removedLength < 0 || quint64(edit.position) > maxIndex || quint64(edit.removedLength) > maxIndex) {
            return {};
        }

        const QByteArray inserted = edit.insertedText.toUtf8();

        if (inserted.size() > MaxPayloadSize -1 -EditMetadataSize) {
            return {};
        }

        QByteArray body;
        QDataStream stream(&body, QIODevice::WriteOnly);
        stream.setVersion(QDataStream::Qt_6_5);
        stream.setByteOrder(QDataStream::BigEndian);

        stream << request.operationId << request.baseRevision << quint32(edit.position) << quint32(edit.removedLength);

        if (stream.status() != QDataStream::Ok) {
            return {};
        }

        body.append(inserted);
        return body;
    }

    inline bool decodeEditRequest(const QByteArray &body, EditRequest &request) {
        if (body.size() < EditMetadataSize || body.size() > MaxPayloadSize - 1) {
            return false;
        }

        EditRequest decoded;
        quint32 position = 0;
        quint32 removedLength = 0;

        QDataStream stream(body);
        stream.setVersion(QDataStream::Qt_6_5);
        stream.setByteOrder(QDataStream::BigEndian);

        stream >> decoded.operationId >> decoded.baseRevision >> position >> removedLength;

        const quint64 maxIndex = std::numeric_limits<qsizetype>::max();

        if (stream.status() != QDataStream::Ok || decoded.operationId.isNull() || quint64(position) > maxIndex || quint64(removedLength) > maxIndex) {
            return false;
        }

        QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);

        const QString inserted = decoder(body.mid(EditMetadataSize));

        if (decoder.hasError()) {
            return false;
        }

        decoded.edit = {
            qsizetype(position),
            qsizetype(removedLength),
            inserted
        };

        request = decoded;
        return true;
    }
}