#include "CsvReader.h"
#include <QStringDecoder>

namespace mcp {
CsvReader::CsvReader(QIODevice *dev) : m_dev(dev) {}

bool CsvReader::fill()
{
    if (m_dev->atEnd()) return false;
    QByteArray chunk = m_dev->readLine();
    if (m_first) {
        if (chunk.startsWith("\xEF\xBB\xBF")) chunk.remove(0, 3);
        const int c = chunk.count(','), s = chunk.count(';'), t = chunk.count('\t');
        m_delim = (t > c && t > s) ? QChar('\t') : (s > c ? QChar(';') : QChar(','));
        m_first = false;
    }
    m_buf = m_buf.mid(m_pos) + QString::fromUtf8(chunk);
    m_pos = 0;
    return true;
}

bool CsvReader::readRow(QStringList *row)
{
    row->clear();
    if (m_pos >= m_buf.size() && !fill()) return false;
    ++m_line;
    QString field;
    bool inQuotes = false;
    while (true) {
        if (m_pos >= m_buf.size()) {
            if (!fill()) { row->append(field); return true; }
        }
        const QChar c = m_buf[m_pos++];
        if (inQuotes) {
            if (c == '"') {
                if (m_pos >= m_buf.size()) fill();
                if (m_pos < m_buf.size() && m_buf[m_pos] == '"') { field += '"'; ++m_pos; }
                else inQuotes = false;
            } else {
                if (c == '\n') ++m_line;
                field += c;
            }
        } else if (c == '"' && field.isEmpty()) {
            inQuotes = true;
        } else if (c == m_delim) {
            row->append(field);
            field.clear();
        } else if (c == '\r') {
            // ignore
        } else if (c == '\n') {
            row->append(field);
            return true;
        } else {
            field += c;
        }
    }
}
}
