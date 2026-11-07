#include "app/core/quick_commands.h"

#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtTest/QTest>

using namespace AppQuickCommands;

class tst_quick_commands : public QObject
{
    Q_OBJECT

private slots:
    void templateReplace_basic();
    void templateReplace_reportsMissing();
    void jsonRoundTrip();
};

void tst_quick_commands::templateReplace_basic()
{
    QString err;
    const QString out =
        applyTemplatePlaceholders(QStringLiteral("AT+OTA={{0}},{{1}}"), {QStringLiteral("192.168.1.1"),
                                                                           QStringLiteral("8080")}, &err);
    QVERIFY(err.isEmpty());
    QCOMPARE(out, QStringLiteral("AT+OTA=192.168.1.1,8080"));
}

void tst_quick_commands::templateReplace_reportsMissing()
{
    QString err;
    const QString out = applyTemplatePlaceholders(QStringLiteral("X{{1}}"), {QStringLiteral("a")}, &err);
    QVERIFY(out.isEmpty());
    QVERIFY(!err.isEmpty());
}

void tst_quick_commands::jsonRoundTrip()
{
    QJsonArray tree;
    tree.append(folderToJson(QStringLiteral("G1"), true,
                             {commandNodeToJson(QStringLiteral("C1"), QStringLiteral("text"),
                                                QStringLiteral("none"), true, false, QStringLiteral("AT"),
                                                QStringList(), QStringList())}));

    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(QuickCommandsSchemaName));
    root.insert(QStringLiteral("version"), QuickCommandsSchemaVersion);
    root.insert(QStringLiteral("tree"), tree);

    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Compact);
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &pe);
    QVERIFY(pe.error == QJsonParseError::NoError);
    const QJsonObject back = doc.object();
    QCOMPARE(back.value(QStringLiteral("version")).toInt(), QuickCommandsSchemaVersion);
    QCOMPARE(countCommandsInJsonArray(back.value(QStringLiteral("tree")).toArray()), 1);
}

QTEST_MAIN(tst_quick_commands)
#include "tst_quick_commands.moc"
