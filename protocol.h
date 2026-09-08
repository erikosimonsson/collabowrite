#pragma once

#include <QtGlobal>

namespace Protocol {
    inline constexpr qsizetype HeaderSize = 4;
    inline constexpr quint32 MaxPayloadSize = 1024 * 1024;

    enum class MessageType : quint8 {
        DocumentSnapshot = 1,
        Error = 2
    };
}