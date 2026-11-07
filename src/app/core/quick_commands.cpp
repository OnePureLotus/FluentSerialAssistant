#include "app/core/quick_commands.h"

#include <QtCore/QRegularExpression>
#include <algorithm>

namespace AppQuickCommands {

QString applyTemplatePlaceholders(const QString &templateText, const QStringList &paramValues, QString *error)
{
    if (error) {
        error->clear();
    }
    QString result = templateText;
    static const QRegularExpression re(QStringLiteral(R"(\{\{(\d+)\}\})"));
    int maxIndex = -1;
    {
        QRegularExpressionMatchIterator it = re.globalMatch(templateText);
        while (it.hasNext()) {
            const QRegularExpressionMatch m = it.next();
            const int idx = m.capturedView(1).toInt();
            maxIndex = std::max(maxIndex, idx);
        }
    }
    if (maxIndex >= 0 && paramValues.size() <= maxIndex) {
        if (error) {
            *error = QStringLiteral("missing_param");
        }
        return QString();
    }
    QRegularExpressionMatchIterator it2 = re.globalMatch(templateText);
    while (it2.hasNext()) {
        const QRegularExpressionMatch m = it2.next();
        const int idx = m.capturedView(1).toInt();
        if (idx < 0 || idx >= paramValues.size()) {
            if (error) {
                *error = QStringLiteral("missing_param");
            }
            return QString();
        }
        result.replace(m.captured(0), paramValues.at(idx));
    }
    if (re.match(result).hasMatch()) {
        if (error) {
            *error = QStringLiteral("unresolved");
        }
        return QString();
    }
    return result;
}

QJsonObject commandNodeToJson(const QString &name, const QString &mode, const QString &lineEnding, bool enabled,
                              bool isTemplate, const QString &payloadOrTemplate, const QStringList &paramHints,
                              const QStringList &paramDefaults)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("command"));
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("mode"), mode);
    o.insert(QStringLiteral("lineEnding"), lineEnding);
    o.insert(QStringLiteral("enabled"), enabled);
    o.insert(QStringLiteral("template"), isTemplate);
    if (isTemplate) {
        o.insert(QStringLiteral("templateText"), payloadOrTemplate);
        QJsonArray hints;
        for (const QString &h : paramHints) {
            hints.append(h);
        }
        QJsonArray defaults;
        for (const QString &d : paramDefaults) {
            defaults.append(d);
        }
        o.insert(QStringLiteral("paramHints"), hints);
        o.insert(QStringLiteral("paramDefaults"), defaults);
    } else {
        o.insert(QStringLiteral("payload"), payloadOrTemplate);
    }
    return o;
}

bool jsonToCommandNode(const QJsonObject &object, QString *name, QString *mode, QString *lineEnding, bool *enabled,
                       bool *isTemplate, QString *payloadOrTemplate, QStringList *paramHints,
                       QStringList *paramDefaults, QString *parseError)
{
    if (!name || !mode || !lineEnding || !enabled || !isTemplate || !payloadOrTemplate) {
        return false;
    }
    *name = object.value(QStringLiteral("name")).toString().trimmed();
    *mode = object.value(QStringLiteral("mode")).toString(QStringLiteral("text"));
    *lineEnding = object.value(QStringLiteral("lineEnding")).toString(QStringLiteral("none"));
    *enabled = object.value(QStringLiteral("enabled")).toBool(true);
    *isTemplate = object.value(QStringLiteral("template")).toBool(false);
    if (*mode != QStringLiteral("hex")) {
        *mode = QStringLiteral("text");
    }
    if (*lineEnding != QStringLiteral("cr") && *lineEnding != QStringLiteral("lf") &&
        *lineEnding != QStringLiteral("crlf")) {
        *lineEnding = QStringLiteral("none");
    }
    if (*isTemplate) {
        *payloadOrTemplate = object.value(QStringLiteral("templateText")).toString();
        paramHints->clear();
        paramDefaults->clear();
        const QJsonArray hints = object.value(QStringLiteral("paramHints")).toArray();
        const QJsonArray defaults = object.value(QStringLiteral("paramDefaults")).toArray();
        const int n = qMax(hints.size(), defaults.size());
        for (int i = 0; i < n; ++i) {
            paramHints->append(i < hints.size() ? hints.at(i).toString() : QString());
            paramDefaults->append(i < defaults.size() ? defaults.at(i).toString() : QString());
        }
    } else {
        *payloadOrTemplate = object.value(QStringLiteral("payload")).toString();
        if (paramHints) {
            paramHints->clear();
        }
        if (paramDefaults) {
            paramDefaults->clear();
        }
    }
    if (name->isEmpty()) {
        if (parseError) {
            *parseError = QStringLiteral("empty_name");
        }
        return false;
    }
    return true;
}

QJsonObject folderToJson(const QString &name, bool expanded, const QJsonArray &children)
{
    QJsonObject o;
    o.insert(QStringLiteral("type"), QStringLiteral("folder"));
    o.insert(QStringLiteral("name"), name);
    o.insert(QStringLiteral("expanded"), expanded);
    o.insert(QStringLiteral("children"), children);
    return o;
}

bool jsonToFolder(const QJsonObject &object, QString *name, bool *expanded, QJsonArray *children, QString *parseError)
{
    if (!name || !expanded || !children) {
        return false;
    }
    *name = object.value(QStringLiteral("name")).toString().trimmed();
    *expanded = object.value(QStringLiteral("expanded")).toBool(true);
    *children = object.value(QStringLiteral("children")).toArray();
    if (name->isEmpty()) {
        if (parseError) {
            *parseError = QStringLiteral("empty_folder_name");
        }
        return false;
    }
    return true;
}

static int countCommandsRecursive(const QJsonArray &nodes)
{
    int count = 0;
    for (const QJsonValue &v : nodes) {
        if (!v.isObject()) {
            continue;
        }
        const QJsonObject o = v.toObject();
        const QString type = o.value(QStringLiteral("type")).toString();
        if (type == QStringLiteral("command")) {
            ++count;
        } else if (type == QStringLiteral("folder")) {
            count += countCommandsRecursive(o.value(QStringLiteral("children")).toArray());
        }
    }
    return count;
}

int countCommandsInJsonArray(const QJsonArray &nodes) { return countCommandsRecursive(nodes); }

bool validateCommandLimit(const QJsonArray &nodes, int maxCommands, QString *error)
{
    if (countCommandsInJsonArray(nodes) > maxCommands) {
        if (error) {
            *error = QStringLiteral("limit");
        }
        return false;
    }
    return true;
}

} // namespace AppQuickCommands
