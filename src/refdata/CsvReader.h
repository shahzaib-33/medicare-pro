#pragma once
#include <QIODevice>
#include <QStringList>

namespace mcp {
// RFC 4180 style CSV reader. Handles quotes, embedded newlines, UTF-8 BOM,
// and comma, semicolon or tab delimiters (detected from the header line).
class CsvReader {
public:
    explicit CsvReader(QIODevice *dev);
    bool readRow(QStringList *row);
    int lineNumber() const { return m_line; }

private:
    QIODevice *m_dev;
    QString m_buf;
    int m_pos = 0;
    int m_line = 0;
    QChar m_delim = ',';
    bool m_first = true;
    bool fill();
};
}
