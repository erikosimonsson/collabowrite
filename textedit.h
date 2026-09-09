#pragma once

#include <QString>

struct TextEdit {
    qsizetype position = 0;
    qsizetype removedLength = 0;
    QString insertedText;
};

inline TextEdit makeTextEdit(const QString &before, const QString &after) {
    auto splitsSurrogatePair = [](const QString &text, qsizetype position) {
        return position > 0 && position < text.size() && text.at(position-1).isHighSurrogate() && text.at(position).isLowSurrogate();
    };

    qsizetype start = 0;

    while (start < before.size() && start < after.size() && before.at(start) == after.at(start)) {
        ++start;
    }

    if (splitsSurrogatePair(before, start) || splitsSurrogatePair(after, start)) {
        --start;
    }

    qsizetype beforeEnd = before.size();
    qsizetype afterEnd = after.size();

    while (beforeEnd > start && afterEnd > start && before.at(beforeEnd - 1) == after.at(afterEnd - 1)) {
        --beforeEnd;
        --afterEnd;
    }

    if (splitsSurrogatePair(before, beforeEnd) || splitsSurrogatePair(after, afterEnd)) {
        ++beforeEnd;
        ++afterEnd;
    }

    return {
        start, 
        beforeEnd - start,
        after.mid(start, afterEnd - start)
    };
}