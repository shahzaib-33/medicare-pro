#include <QTest>
#include "refdata/TextNormalizer.h"
#include "refdata/CsvReader.h"
#include <QBuffer>

using namespace mcp;

class TestNormalizer : public QObject {
    Q_OBJECT
private slots:
    void normalizeEnglish()
    {
        QCOMPARE(TextNormalizer::normalize("  PANADOL-Extra  Tab. "), QString("panadol extra tab"));
        QCOMPARE(TextNormalizer::normalize("2.5 mg"), QString("2.5 mg"));
    }
    void normalizeUrduVariants()
    {
        // Arabic yeh/kaf versus Urdu yeh/keheh must match.
        QCOMPARE(TextNormalizer::normalize(QString::fromUtf8("پيناڈول")), TextNormalizer::normalize(QString::fromUtf8("پیناڈول")));
        QCOMPARE(TextNormalizer::normalize(QString::fromUtf8("كيلپول")), TextNormalizer::normalize(QString::fromUtf8("کیلپول")));
        // Diacritics are ignored.
        QCOMPARE(TextNormalizer::normalize(QString::fromUtf8("پَیناڈول")), TextNormalizer::normalize(QString::fromUtf8("پیناڈول")));
        QCOMPARE(TextNormalizer::normalize(QString::fromUtf8("۵۰۰")), QString("500"));
    }
    void tokensJoinUnits()
    {
        QCOMPARE(TextNormalizer::tokens("Panadol 500 mg"), QStringList({"panadol", "500mg"}));
    }
    void phoneticCrossScript()
    {
        const QString en = TextNormalizer::phoneticKey("Panadol");
        QCOMPARE(en, QString("pndl"));
        QCOMPARE(TextNormalizer::phoneticKey(QString::fromUtf8("پیناڈول")), en);
        QCOMPARE(TextNormalizer::phoneticKey("Panadole"), en);
        QCOMPARE(TextNormalizer::phoneticKey("Paracetamol"), TextNormalizer::phoneticKey("Parasetamol"));
        QCOMPARE(TextNormalizer::phoneticKey(QString::fromUtf8("پیراسیٹامول")), TextNormalizer::phoneticKey("Paracetamol"));
    }
    void editDistance()
    {
        QCOMPARE(TextNormalizer::editDistance("paracetamol", "paracetmol"), 1);
        QCOMPARE(TextNormalizer::editDistance("brufen", "burfen"), 1);
    }
    void csvQuotes()
    {
        QByteArray data = "\xEF\xBB\xBF" "a;b;c\n\"x;1\";\"he said \"\"hi\"\"\";\"multi\nline\"\n";
        QBuffer b(&data);
        b.open(QIODevice::ReadOnly);
        CsvReader r(&b);
        QStringList row;
        QVERIFY(r.readRow(&row));
        QCOMPARE(row, QStringList({"a", "b", "c"}));
        QVERIFY(r.readRow(&row));
        QCOMPARE(row, QStringList({"x;1", "he said \"hi\"", "multi\nline"}));
    }
};

QTEST_GUILESS_MAIN(TestNormalizer)
#include "test_normalizer.moc"
