#include <QtTest>
#include <QRegularExpression>
#include <QSet>
#include <QTranslator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "ui/commandcatalogmodel.h"

class TestCommandCatalog : public QObject
{
    Q_OBJECT

private slots:
    void loadsBundledCatalog();
    void everyEntryIsUsable();
    void stableIdsAreUnique();
    void knownCommandsArePresent();
    void filtersByCategory();
    void searchesCommandAndDescription();
    void reportsEmptySearch();
    void loadsLocalizedDescriptions();
    void translationsCoverEveryCommand();
    void englishDescriptionsMatchCatalog();
    void languageSwitchReloadsDescriptions();
    void chineseSearchFindsCommands();
    void catalogContextIsNotVanished();
    void tooltipsExposeFullText();
    void rejectsInvalidJson();
};

void TestCommandCatalog::loadsBundledCatalog()
{
    CommandCatalogModel model;
    QVERIFY2(model.loadFromResource(), "bundled :/commands/commands.json must load");
    QVERIFY(model.totalCount() >= 1000);
    QCOMPARE(model.visibleCount(), model.totalCount());
    QCOMPARE(model.rowCount(), model.totalCount());
}

void TestCommandCatalog::everyEntryIsUsable()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    for(int row = 0; row < model.rowCount(); ++row)
    {
        const QString command = model.commandAt(row);
        const QString id = model.idAt(row);
        const QString description = model.descriptionAt(row);
        const QString category = model.categoryAt(row);

        QVERIFY2(!command.isEmpty(), qPrintable(QStringLiteral("row %1 has no command").arg(row)));
        QVERIFY2(command == command.trimmed(), "command text must not be padded");
        QVERIFY2(!id.isEmpty(), qPrintable(QStringLiteral("row %1 has no id").arg(row)));
        QVERIFY2(!description.isEmpty(),
                 qPrintable(QStringLiteral("row %1 has no description").arg(row)));
        QVERIFY2(description != id, "description must not fall back to the id");
        QVERIFY2(CommandCatalogModel::categoryFilters().contains(category),
                 qPrintable(QStringLiteral("row %1 has category %2").arg(row).arg(category)));
    }
}

void TestCommandCatalog::stableIdsAreUnique()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());
    QSet<QString> ids;
    QSet<QString> commands;
    static const QRegularExpression idPattern(QStringLiteral("^cmd\\.[a-z0-9_]+$"));
    for(int row = 0; row < model.rowCount(); ++row)
    {
        const QString id = model.idAt(row);
        QVERIFY2(idPattern.match(id).hasMatch(), qPrintable(id));
        QVERIFY2(!ids.contains(id), qPrintable(QStringLiteral("duplicate id %1").arg(id)));
        QVERIFY2(!commands.contains(model.commandAt(row)),
                 qPrintable(QStringLiteral("duplicate command %1").arg(model.commandAt(row))));
        ids.insert(id);
        commands.insert(model.commandAt(row));
    }
}

void TestCommandCatalog::knownCommandsArePresent()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    const QStringList expected = {
        QStringLiteral("hw version"), QStringLiteral("hf 14a info"),
        QStringLiteral("hf mf rdbl"), QStringLiteral("hf 15 info"),
        QStringLiteral("lf em 410x reader"), QStringLiteral("prefs show")
    };
    for(const QString &command : expected)
    {
        model.setSearchText(command);
        model.setCategoryFilter(QStringLiteral("all"));
        bool found = false;
        for(int row = 0; row < model.rowCount(); ++row)
        {
            if(model.commandAt(row) == command)
            {
                found = true;
                break;
            }
        }
        QVERIFY2(found, qPrintable(command));
    }
}

void TestCommandCatalog::filtersByCategory()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());
    const int total = model.totalCount();

    model.setCategoryFilter(QStringLiteral("hf"));
    const int hfCount = model.visibleCount();
    QVERIFY(hfCount > 0 && hfCount < total);
    for(int row = 0; row < model.visibleCount(); ++row)
        QVERIFY(model.commandAt(row).startsWith(QStringLiteral("hf ")));

    model.setCategoryFilter(QStringLiteral("lf"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < model.visibleCount(); ++row)
        QVERIFY(model.commandAt(row).startsWith(QStringLiteral("lf ")));

    model.setCategoryFilter(QStringLiteral("hf14a"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < model.visibleCount(); ++row)
        QVERIFY(model.commandAt(row).startsWith(QStringLiteral("hf 14a ")));

    model.setCategoryFilter(QStringLiteral("hfmf"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < model.visibleCount(); ++row)
        QVERIFY(model.categoryAt(row) == QStringLiteral("hfmf"));

    model.setCategoryFilter(QStringLiteral("hf15"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < model.visibleCount(); ++row)
        QVERIFY(model.commandAt(row).startsWith(QStringLiteral("hf 15 ")));

    model.setCategoryFilter(QStringLiteral("lfem"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < model.visibleCount(); ++row)
        QVERIFY(model.commandAt(row).startsWith(QStringLiteral("lf em ")));

    model.setCategoryFilter(QStringLiteral("other"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < model.visibleCount(); ++row)
    {
        QVERIFY(!model.commandAt(row).startsWith(QStringLiteral("hf ")));
        QVERIFY(!model.commandAt(row).startsWith(QStringLiteral("lf ")));
    }

    model.setCategoryFilter(QStringLiteral("all"));
    QCOMPARE(model.visibleCount(), total);
}

void TestCommandCatalog::searchesCommandAndDescription()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    model.setSearchText(QStringLiteral("hf 14a info"));
    QVERIFY(model.visibleCount() >= 1);
    QCOMPARE(model.commandAt(0), QStringLiteral("hf 14a info"));

    model.setSearchText(QStringLiteral("tag information"));
    QVERIFY(model.visibleCount() > 0);
    for(int row = 0; row < qMin(model.visibleCount(), 20); ++row)
    {
        const bool commandMatches =
            model.commandAt(row).toLower().contains(QStringLiteral("tag information"));
        const bool descriptionMatches =
            model.descriptionAt(row).toLower().contains(QStringLiteral("tag information"));
        QVERIFY(commandMatches || descriptionMatches);
    }

    model.setSearchText(QStringLiteral("HF 14A INFO"));
    QVERIFY(model.visibleCount() >= 1);

    model.setSearchText(QStringLiteral("hw version"));
    model.setCategoryFilter(QStringLiteral("other"));
    QVERIFY(model.visibleCount() >= 1);
}

void TestCommandCatalog::reportsEmptySearch()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());
    model.setSearchText(QStringLiteral("no-such-command-zzzz"));
    QCOMPARE(model.visibleCount(), 0);
    QCOMPARE(model.rowCount(), 0);
}

void TestCommandCatalog::loadsLocalizedDescriptions()
{
    QTranslator translator;
    QVERIFY2(translator.load(QStringLiteral(":/i18n/zh_CN.qm")),
             "zh_CN.qm must be available in the test resources");
    QCoreApplication::installTranslator(&translator);

    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());
    model.setSearchText(QStringLiteral("hf 14a info"));
    QVERIFY(model.visibleCount() >= 1);
    QCOMPARE(model.commandAt(0), QStringLiteral("hf 14a info"));
    const QString description = model.descriptionAt(0);
    QVERIFY2(description != QStringLiteral("Tag information"),
             "zh_CN must translate the stable command id");
    bool hasHan = false;
    for (const QChar ch : description) {
        if (ch.unicode() >= 0x4e00 && ch.unicode() <= 0x9fff) {
            hasHan = true;
            break;
        }
    }
    QVERIFY2(hasHan, qPrintable(description));

    QCoreApplication::removeTranslator(&translator);
}

void TestCommandCatalog::translationsCoverEveryCommand()
{
    QTranslator translator;
    QVERIFY2(translator.load(QStringLiteral(":/i18n/zh_CN.qm")),
             "zh_CN.qm must be available in the test resources");
    QCoreApplication::installTranslator(&translator);

    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    QFile json(QStringLiteral(PROJECT_ROOT "/resources/commands/commands.json"));
    QVERIFY(json.open(QFile::ReadOnly));
    const QJsonArray rows = QJsonDocument::fromJson(json.readAll())
                                .object().value(QStringLiteral("commands")).toArray();
    QHash<QString, QString> english;
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        english.insert(row.value(QStringLiteral("id")).toString(),
                       row.value(QStringLiteral("description")).toString().trimmed());
    }
    QCOMPARE(model.totalCount(), english.size());

    for (int row = 0; row < model.rowCount(); ++row) {
        const QString id = model.idAt(row);
        const QString description = model.descriptionAt(row);
        QVERIFY2(!description.isEmpty(), qPrintable(id));
        QVERIFY2(description != id, qPrintable(id));
        QVERIFY2(description != english.value(id),
                 qPrintable(QStringLiteral("%1 fell back to English: %2").arg(id, description)));
        bool hasHan = false;
        for (const QChar ch : description) {
            if (ch.unicode() >= 0x4e00 && ch.unicode() <= 0x9fff) {
                hasHan = true;
                break;
            }
        }
        QVERIFY2(hasHan, qPrintable(QStringLiteral("%1 has no Chinese text: %2").arg(id, description)));
    }

    QCoreApplication::removeTranslator(&translator);
}

void TestCommandCatalog::englishDescriptionsMatchCatalog()
{
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/en_US.qm")));
    QCoreApplication::installTranslator(&translator);

    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    QFile json(QStringLiteral(PROJECT_ROOT "/resources/commands/commands.json"));
    QVERIFY(json.open(QFile::ReadOnly));
    const QJsonArray rows = QJsonDocument::fromJson(json.readAll())
                                .object().value(QStringLiteral("commands")).toArray();
    QHash<QString, QString> english;
    for (const QJsonValue &value : rows) {
        const QJsonObject row = value.toObject();
        QString description = row.value(QStringLiteral("description")).toString().trimmed();
        if (description.isEmpty())
            description = QStringLiteral("No description provided");
        english.insert(row.value(QStringLiteral("id")).toString(), description);
    }

    for (int row = 0; row < model.rowCount(); ++row) {
        const QString id = model.idAt(row);
        const QString description = model.descriptionAt(row);
        QVERIFY2(!description.isEmpty(), qPrintable(id));
        QCOMPARE(description, english.value(id));
        for (const QChar ch : description) {
            QVERIFY2(!(ch.unicode() >= 0x4e00 && ch.unicode() <= 0x9fff),
                     qPrintable(QStringLiteral("%1 leaked Chinese: %2").arg(id, description)));
        }
    }

    QCoreApplication::removeTranslator(&translator);
}

void TestCommandCatalog::languageSwitchReloadsDescriptions()
{
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/zh_CN.qm")));
    QCoreApplication::installTranslator(&translator);

    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());
    QString chinese;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.commandAt(row) == QStringLiteral("hf 14a info")) {
            chinese = model.descriptionAt(row);
            break;
        }
    }
    QVERIFY(!chinese.isEmpty());
    QVERIFY(chinese != QStringLiteral("Tag information"));

    QCoreApplication::removeTranslator(&translator);
    QVERIFY(translator.load(QStringLiteral(":/i18n/en_US.qm")));
    QCoreApplication::installTranslator(&translator);
    QVERIFY(model.loadFromResource());
    QString english;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.commandAt(row) == QStringLiteral("hf 14a info")) {
            english = model.descriptionAt(row);
            break;
        }
    }
    QCOMPARE(english, QStringLiteral("Tag information"));

    QCoreApplication::removeTranslator(&translator);
    QVERIFY(translator.load(QStringLiteral(":/i18n/zh_CN.qm")));
    QCoreApplication::installTranslator(&translator);
    QVERIFY(model.loadFromResource());
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.commandAt(row) == QStringLiteral("hf 14a info")) {
            QCOMPARE(model.descriptionAt(row), chinese);
            break;
        }
    }

    QCoreApplication::removeTranslator(&translator);
}

void TestCommandCatalog::chineseSearchFindsCommands()
{
    QTranslator translator;
    QVERIFY(translator.load(QStringLiteral(":/i18n/zh_CN.qm")));
    QCoreApplication::installTranslator(&translator);

    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    model.setSearchText(QString::fromUtf8("身份认证"));
    QVERIFY(model.visibleCount() > 0);
    model.setSearchText(QString::fromUtf8("扇区"));
    QVERIFY(model.visibleCount() > 0);
    model.setSearchText(QString::fromUtf8("转储"));
    QVERIFY(model.visibleCount() > 0);

    // Command text search is unchanged by the localized descriptions.
    model.setSearchText(QStringLiteral("hf 14a info"));
    QVERIFY(model.visibleCount() >= 1);
    QCOMPARE(model.commandAt(0), QStringLiteral("hf 14a info"));

    // Copy/insert path only ever uses the command text and plain descriptions.
    const QString tooltip = model.data(model.index(0, 0), Qt::ToolTipRole).toString();
    QVERIFY(tooltip.startsWith(QStringLiteral("hf 14a info")));

    QCoreApplication::removeTranslator(&translator);
}

void TestCommandCatalog::catalogContextIsNotVanished()
{
    const QStringList languages = { QStringLiteral("zh_CN"), QStringLiteral("en_US") };
    for (const QString &language : languages) {
        QFile file(QStringLiteral(PROJECT_ROOT "/i18n/%1.ts").arg(language));
        QVERIFY2(file.open(QFile::ReadOnly), qPrintable(language));
        const QString text = QString::fromUtf8(file.readAll());
        const int marker = text.indexOf(QStringLiteral("<name>CommandCatalog</name>"));
        QVERIFY2(marker >= 0, qPrintable(language));
        const int start = text.lastIndexOf(QStringLiteral("<context>"), marker);
        const int end = text.indexOf(QStringLiteral("</context>"), marker);
        QVERIFY(start >= 0 && end > start);
        const QString context = text.mid(start, end - start);
        QCOMPARE(context.count(QStringLiteral("<message>")), 1024);
        QVERIFY2(!context.contains(QStringLiteral("type=\"vanished\"")),
                 qPrintable(QStringLiteral("%1: lupdate marked the catalog vanished").arg(language)));
        QVERIFY2(!context.contains(QStringLiteral("<translation></translation>")),
                 qPrintable(QStringLiteral("%1: blank catalog translation").arg(language)));
        QVERIFY2(!context.contains(QStringLiteral("type=\"unfinished\"")),
                 qPrintable(QStringLiteral("%1: unfinished catalog translation").arg(language)));
    }
}

void TestCommandCatalog::tooltipsExposeFullText()
{
    CommandCatalogModel model;
    QVERIFY(model.loadFromResource());

    for (int row = 0; row < qMin(model.rowCount(), 1024); ++row) {
        const QModelIndex index = model.index(row, 0);
        const QString tooltip = model.data(index, Qt::ToolTipRole).toString();
        const QString command = model.data(index, CommandCatalogModel::CommandRole).toString();
        const QString description =
            model.data(index, CommandCatalogModel::DescriptionRole).toString();
        QVERIFY2(tooltip.contains(command), qPrintable(command));
        QVERIFY2(tooltip.contains(description), qPrintable(description));
        QVERIFY2(!model.data(index, Qt::ToolTipRole).toString().isEmpty(),
                 qPrintable(command));
    }
}

void TestCommandCatalog::rejectsInvalidJson()
{
    CommandCatalogModel model;
    QString error;
    QVERIFY(!model.loadFromJson(QByteArray("not json"), &error));
    QVERIFY(!error.isEmpty());

    QVERIFY(!model.loadFromJson(QByteArray("{\"commands\":[]}"), &error));
    QVERIFY(!model.loadFromJson(
        QByteArray("{\"commands\":[{\"id\":\"x\",\"command\":\"\"}]}"), &error));
    QVERIFY(!model.isLoaded());
}

QTEST_GUILESS_MAIN(TestCommandCatalog)
#include "tst_commandcatalog.moc"
