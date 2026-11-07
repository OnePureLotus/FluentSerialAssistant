#pragma once

#include <QtCore/QJsonArray>
#include <QtCore/QJsonObject>
#include <QtCore/QString>
#include <QtCore/QStringList>

namespace AppQuickCommands {

constexpr const char QuickCommandsSchemaName[] = "fluent-serial-assistant.quick-commands";
constexpr int QuickCommandsSchemaVersion = 1;

QString applyTemplatePlaceholders(const QString &templateText, const QStringList &paramValues, QString *error);

QJsonObject commandNodeToJson(const QString &name, const QString &mode, const QString &lineEnding, bool enabled,
                            bool isTemplate, const QString &payloadOrTemplate, const QStringList &paramHints,
                            const QStringList &paramDefaults);
bool jsonToCommandNode(const QJsonObject &object, QString *name, QString *mode, QString *lineEnding, bool *enabled,
                        bool *isTemplate, QString *payloadOrTemplate, QStringList *paramHints,
                        QStringList *paramDefaults, QString *parseError);

QJsonObject folderToJson(const QString &name, bool expanded, const QJsonArray &children);
bool jsonToFolder(const QJsonObject &object, QString *name, bool *expanded, QJsonArray *children, QString *parseError);

int countCommandsInJsonArray(const QJsonArray &nodes);
bool validateCommandLimit(const QJsonArray &nodes, int maxCommands, QString *error);

} // namespace AppQuickCommands
