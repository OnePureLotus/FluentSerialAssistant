#include "app/core/app_i18n.h"
#include "app/core/quick_commands.h"
#include "app/view/fluent_tooltip_helper.h"
#include "app/view/muted_text.h"
#include "app/view/workbench/workbench_page_internal.h"

#include <FluentQtWidgets/Settings/SettingCard.h>
#include <FluentQtWidgets/Widgets/CardWidget.h>
#include <FluentQtWidgets/Widgets/Label.h>
#include <FluentQtWidgets/Widgets/LineEdit.h>
#include <FluentQtWidgets/Widgets/Menu.h>
#include <FluentQtWidgets/Widgets/ScrollArea.h>

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QMap>
#include <QtCore/QEvent>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QTimer>
#include <QtWidgets/QAbstractButton>
#include <QtWidgets/QDialog>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QBoxLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtCore/QtMath>
#include <QtGui/QPainter>
#include <QtGui/QResizeEvent>
#include <QtGui/QTextCursor>
#include <QtGui/QTextDocument>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QtGui/QEnterEvent>
#endif
#include <QtGui/QMouseEvent>
#include <QtWidgets/QInputDialog>
#include <QtWidgets/QFileDialog>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QLayout>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QSizePolicy>
#include <QtWidgets/QToolButton>

using namespace FluentQt;
using namespace WorkbenchPagePrivate;
using namespace AppQuickCommands;

static const char PropFolderCard[] = "qcFolderCard";
static const char PropCommandCard[] = "qcCommandCard";
static const char PropCommandData[] = "qcCommandData";
static const char PropSendBtn[] = "qcSendBtn";
static const char PropCommandRowOuter[] = "qcCommandRowOuter";

namespace {

enum class QuickCmdChevronKind
{
    Up,
    Down
};

/** Rounded hit target; icon uses Fluent RightArrow (bar + chevron), rotated for up/down reorder. */
class QuickCmdChevronButton : public QToolButton
{
  public:
    explicit QuickCmdChevronButton(QuickCmdChevronKind kind, QWidget *parent = nullptr, int box = 30)
        : QToolButton(parent), m_kind(kind), m_box(qMax(24, box))
    {
        setFixedSize(m_box, m_box);
        setCursor(Qt::PointingHandCursor);
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setAttribute(Qt::WA_TranslucentBackground);
        FluentStyleSheet::setRole(this, QStringLiteral("ExpandButton"));
    }

  protected:
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    void enterEvent(QEnterEvent *event) override
#else
    void enterEvent(QEvent *event) override
#endif
    {
        m_hover = true;
        update();
        QToolButton::enterEvent(event);
    }
    void leaveEvent(QEvent *event) override
    {
        m_hover = false;
        m_pressed = false;
        update();
        QToolButton::leaveEvent(event);
    }
    void mousePressEvent(QMouseEvent *event) override
    {
        m_pressed = true;
        update();
        QToolButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        QToolButton::mouseReleaseEvent(event);
        m_pressed = false;
        update();
    }
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
        painter.setPen(Qt::NoPen);

        const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
        const int channel = dark ? 255 : 0;
        QColor background = Qt::transparent;
        if (isEnabled()) {
            if (m_pressed) {
                background = QColor(channel, channel, channel, 10);
            } else if (m_hover) {
                background = QColor(channel, channel, channel, 14);
            }
        } else {
            painter.setOpacity(0.36);
        }
        painter.setBrush(background);
        painter.drawRoundedRect(rect(), 4, 4);

        // RightArrow is the Fluent "—>" style (stem + head); rotate to vertical for list reorder.
        painter.translate(width() / 2.0, height() / 2.0);
        const qreal angle = (m_kind == QuickCmdChevronKind::Up) ? -90.0 : 90.0;
        painter.rotate(angle);
        constexpr int kIconPx = 10;
        const int half = kIconPx / 2;
        FluentQt::icon(FluentIcon::RightArrow).paint(&painter, QRect(-half, -half, kIconPx, kIconPx));
    }

  private:
    QuickCmdChevronKind m_kind;
    int m_box = 30;
    bool m_hover = false;
    bool m_pressed = false;
};

QString commandNameMap(const QVariantMap &m) { return m.value(QStringLiteral("name")).toString(); }

QString elidePreviewText(const QString &text, int maxChars = 480)
{
    if (text.size() <= maxChars) {
        return text;
    }
    return text.left(maxChars).trimmed() + QStringLiteral("…");
}

void applyQuickCommandWrappingLabel(FluentLabelBase *label)
{
    if (!label) {
        return;
    }
    label->setWordWrap(true);
    label->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    label->setMinimumWidth(0);
}

/** 按控件宽度排版换行（不依赖 QLabel 的 sizeHint，避免长串始终单行）。 */
class QuickCommandPreviewPane final : public QWidget
{
  public:
    explicit QuickCommandPreviewPane(QWidget *parent = nullptr) : QWidget(parent)
    {
        setObjectName(QStringLiteral("qcPreviewBody"));
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        setMinimumWidth(0);
        setFocusPolicy(Qt::NoFocus);
        m_doc.setDocumentMargin(0);
        QTextOption opt;
        opt.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
        m_doc.setDefaultTextOption(opt);
        BodyLabel fontProbe(this);
        fontProbe.hide();
        m_doc.setDefaultFont(fontProbe.font());
    }

    void setPreviewText(const QString &text, bool muted)
    {
        m_muted = muted;
        m_doc.setPlainText(text);
        applyTextColor();
        refreshLayout();
        update();
    }

    void refreshLayout()
    {
        if (width() > 2) {
            m_doc.setTextWidth(width());
        }
        const int h =
            qMax(fontMetrics().height() + 2, static_cast<int>(qCeil(m_doc.size().height())) + 2);
        setFixedHeight(h);
    }

  protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        refreshLayout();
    }

    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setClipRect(rect());
        m_doc.drawContents(&painter, QRectF(0, 0, width(), height()));
    }

  private:
    QTextDocument m_doc;
    bool m_muted = false;

    void applyTextColor()
    {
        const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
        QTextCharFormat fmt;
        if (m_muted) {
            const int g = dark ? 180 : 96;
            fmt.setForeground(QColor(g, g, g));
        } else {
            fmt.setForeground(palette().color(QPalette::WindowText));
        }
        QTextCursor cursor(&m_doc);
        cursor.select(QTextCursor::Document);
        cursor.mergeCharFormat(fmt);
    }
};

QuickCommandPreviewPane *findQuickCommandPreviewPane(QWidget *scope)
{
    if (!scope) {
        return nullptr;
    }
    auto *w = scope->findChild<QWidget *>(QStringLiteral("qcPreviewBody"));
    return dynamic_cast<QuickCommandPreviewPane *>(w);
}

void relayoutAllQuickCommandPreviews(QWidget *listHost)
{
    if (!listHost) {
        return;
    }
    const auto panes = listHost->findChildren<QWidget *>(QStringLiteral("qcPreviewBody"));
    for (QWidget *w : panes) {
        if (auto *pane = dynamic_cast<QuickCommandPreviewPane *>(w)) {
            pane->refreshLayout();
            pane->update();
        }
    }
}

/** Lift nested folders under one folder into flat command rows (single folder level). */
void flattenFolderChildrenJson(QJsonArray &children)
{
    int i = 0;
    while (i < children.size()) {
        if (!children.at(i).isObject()) {
            ++i;
            continue;
        }
        QJsonObject o = children.at(i).toObject();
        if (o.value(QStringLiteral("type")).toString() != QStringLiteral("folder")) {
            ++i;
            continue;
        }
        QJsonArray nested = o.value(QStringLiteral("children")).toArray();
        children.removeAt(i);
        for (int k = 0; k < nested.size(); ++k) {
            children.insert(i + k, nested.at(k));
        }
    }
}

void flattenQuickCommandsDataJson(QJsonArray &nodes)
{
    for (int i = 0; i < nodes.size(); ++i) {
        if (!nodes.at(i).isObject()) {
            continue;
        }
        QJsonObject o = nodes.at(i).toObject();
        if (o.value(QStringLiteral("type")).toString() != QStringLiteral("folder")) {
            continue;
        }
        QJsonArray ch = o.value(QStringLiteral("children")).toArray();
        flattenFolderChildrenJson(ch);
        o.insert(QStringLiteral("children"), ch);
        nodes[i] = o;
    }
}

int countCommandsInFolderLayout(const QVBoxLayout *lay)
{
    if (!lay) {
        return 0;
    }
    int n = 0;
    for (int i = 0; i < lay->count(); ++i) {
        QLayoutItem *it = lay->itemAt(i);
        QWidget *w = it ? it->widget() : nullptr;
        if (!w) {
            continue;
        }
        if (w->property(PropCommandRowOuter).toBool()) {
            if (w->findChild<SimpleCardWidget *>(QStringLiteral("qcCommandCard"))) {
                ++n;
            }
        }
    }
    return n;
}

void updateFolderCardSubtitle(ExpandSettingCard *card)
{
    if (!card || !card->property(PropFolderCard).toBool()) {
        return;
    }
    QVBoxLayout *vl = card->viewLayout();
    const int n = countCommandsInFolderLayout(vl);
    if (HeaderSettingCard *hdr = card->card()) {
        hdr->setContent(AppI18n::text("含 %1 条命令").arg(n));
    }
}

QStringList quickCommandParamTextsFromRow(QWidget *rowWidget)
{
    QStringList out;
    if (!rowWidget) {
        return out;
    }
    const QList<LineEdit *> edits = rowWidget->findChildren<LineEdit *>();
    int maxIx = -1;
    QMap<int, QString> map;
    for (LineEdit *e : edits) {
        const QVariant ix = e->property("qcParamIndex");
        if (!ix.isValid()) {
            continue;
        }
        const int k = ix.toInt();
        maxIx = qMax(maxIx, k);
        map.insert(k, e->text());
    }
    for (int i = 0; i <= maxIx; ++i) {
        out.append(map.value(i, QString()));
    }
    return out;
}

void setQuickCommandRowPreview(QWidget *rowWidget, const QVariantMap &map)
{
    if (!rowWidget) {
        return;
    }
    auto *preview = findQuickCommandPreviewPane(rowWidget);
    auto *incomplete = rowWidget->findChild<QLabel *>(QStringLiteral("qcIncompleteHint"));
    auto *flIncomplete = incomplete ? static_cast<FluentLabelBase *>(incomplete) : nullptr;
    if (!preview) {
        return;
    }
    const auto applyPreview = [&](const QString &text, bool muted) {
        preview->setPreviewText(text, muted);
        QTimer::singleShot(0, preview, [preview]() { preview->refreshLayout(); });
    };
    const bool isTemplate = map.value(QStringLiteral("template")).toBool();
    if (!isTemplate) {
        if (flIncomplete) {
            flIncomplete->hide();
        }
        const QString raw = map.value(QStringLiteral("payload")).toString();
        if (raw.trimmed().isEmpty()) {
            applyPreview(AppI18n::text("（空）"), true);
        } else {
            applyPreview(elidePreviewText(raw), false);
        }
        return;
    }
    QString err;
    const QStringList params = quickCommandParamTextsFromRow(rowWidget);
    const QString resolved =
        applyTemplatePlaceholders(map.value(QStringLiteral("templateText")).toString(), params, &err);
    if (!err.isEmpty()) {
        if (flIncomplete) {
            flIncomplete->setText(AppI18n::text("填写参数后可预览完整内容"));
            flIncomplete->show();
        }
        applyPreview(elidePreviewText(resolved.isEmpty() ? map.value(QStringLiteral("templateText")).toString()
                                                         : resolved),
                     true);
        return;
    }
    if (flIncomplete) {
        flIncomplete->hide();
    }
    if (resolved.trimmed().isEmpty()) {
        applyPreview(AppI18n::text("（空）"), true);
    } else {
        applyPreview(elidePreviewText(resolved), false);
    }
}

SimpleCardWidget *buildCommandRowWidget(const QVariantMap &map, bool isTemplate)
{
    auto *card = new SimpleCardWidget;
    card->setObjectName(QStringLiteral("qcCommandCard"));
    card->setProperty(PropCommandCard, true);
    card->setClickEnabled(true);

    auto *inner = new QWidget(card);
    inner->setObjectName(QStringLiteral("qcCommandInner"));
    inner->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    auto *vl = new QVBoxLayout(inner);
    vl->setContentsMargins(10, 8, 10, 8);
    vl->setSpacing(8);

    const QString displayName =
        commandNameMap(map).isEmpty() ? AppI18n::text("（未命名）") : commandNameMap(map);

    auto *titleRow = new QWidget(inner);
    auto *hlTop = new QHBoxLayout(titleRow);
    hlTop->setContentsMargins(0, 0, 0, 0);
    hlTop->setSpacing(8);
    auto *title = new StrongBodyLabel(displayName, titleRow);
    applyQuickCommandWrappingLabel(title);
    hlTop->addWidget(title, 1);

    auto *meta = new CaptionLabel(
        modeLabel(map.value(QStringLiteral("mode")).toString()) + QStringLiteral(u" · ") +
            lineEndingLabel(map.value(QStringLiteral("lineEnding")).toString()),
        titleRow);
    AppUi::setMutedTextColor(meta);
    hlTop->addWidget(meta, 0, Qt::AlignRight | Qt::AlignVCenter);
    vl->addWidget(titleRow);

    if (isTemplate) {
        QJsonParseError pe1;
        QJsonParseError pe2;
        const QJsonArray hints =
            QJsonDocument::fromJson(map.value(QStringLiteral("paramHintsJson")).toString().toUtf8(), &pe1).array();
        const QJsonArray defaults =
            QJsonDocument::fromJson(map.value(QStringLiteral("paramDefaultsJson")).toString().toUtf8(), &pe2).array();
        const int n = qMax(hints.size(), defaults.size());
        auto *paramSection = new QWidget(inner);
        auto *paramOuter = new QVBoxLayout(paramSection);
        paramOuter->setContentsMargins(0, 0, 0, 0);
        paramOuter->setSpacing(6);
        auto *paramCap = new CaptionLabel(AppI18n::text("参数"), paramSection);
        AppUi::setMutedTextColor(paramCap);
        paramOuter->addWidget(paramCap);

        for (int i = 0; i < n; ++i) {
            auto *row = new QWidget(paramSection);
            auto *phl = new QHBoxLayout(row);
            phl->setContentsMargins(0, 0, 0, 4);
            phl->setSpacing(6);
            const QString hint = i < hints.size() ? hints.at(i).toString() : QString();
            const QString def = i < defaults.size() ? defaults.at(i).toString() : QString();
            auto *edit = new LineEdit(row);
            edit->setPlaceholderText(hint.isEmpty() ? AppI18n::text("参数 %1").arg(i) : hint);
            edit->setText(def);
            edit->setProperty("qcParamIndex", i);
            edit->setMinimumHeight(CompactControlHeight);
            edit->setFixedHeight(CompactControlHeight);
            edit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
            phl->addWidget(edit, 1);
            paramOuter->addWidget(row);
        }
        vl->addWidget(paramSection);
    }

    auto *pvCap = new CaptionLabel(AppI18n::text("发送预览"), inner);
    AppUi::setMutedTextColor(pvCap);
    vl->addWidget(pvCap);

    auto *incomplete = new CaptionLabel(QString(), inner);
    incomplete->setObjectName(QStringLiteral("qcIncompleteHint"));
    incomplete->hide();
    AppUi::setMutedTextColor(incomplete);
    applyQuickCommandWrappingLabel(incomplete);
    vl->addWidget(incomplete);

    auto *preview = new QuickCommandPreviewPane(inner);

    auto *cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(6, 4, 6, 4);
    cardLayout->setSpacing(0);
    cardLayout->addWidget(inner);

    vl->addWidget(preview);

    card->setProperty(PropCommandData, map);
    setQuickCommandRowPreview(inner, map);
    return card;
}

class QuickCommandEditDialog : public QDialog
{
  public:
    QuickCommandEditDialog(QWidget *parent, const QString &title, bool templateMode, const QVariantMap &initial)
        : QDialog(parent), m_templateMode(templateMode)
    {
        setWindowTitle(title);
        auto *layout = new QVBoxLayout(this);
        auto *form = new QFormLayout;

        m_nameEdit = new LineEdit(this);
        m_nameEdit->setText(initial.value(QStringLiteral("name")).toString());

        m_modeCombo = new ComboBox(this);
        m_modeCombo->addItem(AppI18n::text("文本"), QIcon(), QStringLiteral("text"));
        m_modeCombo->addItem(QStringLiteral("HEX"), QIcon(), QStringLiteral("hex"));
        const QString mode = initial.value(QStringLiteral("mode"), QStringLiteral("text")).toString();
        const int modeIdx = m_modeCombo->findData(mode == QStringLiteral("hex") ? QStringLiteral("hex")
                                                                              : QStringLiteral("text"));
        m_modeCombo->setCurrentIndex(modeIdx >= 0 ? modeIdx : 0);

        m_lineEndingCombo = new ComboBox(this);
        m_lineEndingCombo->addItem(AppI18n::text("无换行"), QIcon(), QStringLiteral("none"));
        m_lineEndingCombo->addItem(QStringLiteral("CR"), QIcon(), QStringLiteral("cr"));
        m_lineEndingCombo->addItem(QStringLiteral("LF"), QIcon(), QStringLiteral("lf"));
        m_lineEndingCombo->addItem(QStringLiteral("CRLF"), QIcon(), QStringLiteral("crlf"));
        const QString le = initial.value(QStringLiteral("lineEnding"), QStringLiteral("none")).toString();
        const int leIdx = m_lineEndingCombo->findData(le);
        m_lineEndingCombo->setCurrentIndex(leIdx >= 0 ? leIdx : 0);

        m_enabledCheck = new CheckBox(AppI18n::text("启用"), this);
        m_enabledCheck->setChecked(initial.value(QStringLiteral("enabled"), true).toBool());

        form->addRow(AppI18n::text("名称"), m_nameEdit);
        form->addRow(AppI18n::text("模式"), m_modeCombo);
        form->addRow(AppI18n::text("换行"), m_lineEndingCombo);
        form->addRow(QString(), m_enabledCheck);

        layout->addLayout(form);

        if (m_templateMode) {
            m_templateEdit = new PlainTextEdit(this);
            m_templateEdit->setPlainText(initial.value(QStringLiteral("templateText")).toString());
            m_templateEdit->setMinimumHeight(88);
            layout->addWidget(new StrongBodyLabel(AppI18n::text("模板（使用 {{0}} {{1}} 占位）"), this));
            layout->addWidget(m_templateEdit);

            auto *paramHeader = new QWidget(this);
            auto *paramRow = new QHBoxLayout(paramHeader);
            paramRow->setContentsMargins(0, 0, 0, 0);
            paramRow->addWidget(new StrongBodyLabel(AppI18n::text("参数"), paramHeader));
            paramRow->addStretch(1);
            auto *addParamBtn = new PushButton(AppI18n::text("添加参数"), paramHeader);
            paramRow->addWidget(addParamBtn);
            layout->addWidget(paramHeader);

            m_paramContainer = new QWidget(this);
            m_paramLayout = new QVBoxLayout(m_paramContainer);
            m_paramLayout->setContentsMargins(0, 0, 0, 0);
            m_paramLayout->setSpacing(6);
            layout->addWidget(m_paramContainer);

            QJsonParseError pe1;
            QJsonParseError pe2;
            const QJsonDocument hd =
                QJsonDocument::fromJson(initial.value(QStringLiteral("paramHintsJson")).toString().toUtf8(), &pe1);
            const QJsonDocument dd =
                QJsonDocument::fromJson(initial.value(QStringLiteral("paramDefaultsJson")).toString().toUtf8(), &pe2);
            const QJsonArray hints = (pe1.error == QJsonParseError::NoError) ? hd.array() : QJsonArray();
            const QJsonArray defaults = (pe2.error == QJsonParseError::NoError) ? dd.array() : QJsonArray();
            const int n = qMax(hints.size(), defaults.size());
            if (n == 0) {
                addParamRow(QString(), QString());
            } else {
                for (int i = 0; i < n; ++i) {
                    addParamRow(hints.at(i).toString(), i < defaults.size() ? defaults.at(i).toString() : QString());
                }
            }
            connect(addParamBtn, &PushButton::clicked, this, [this]() { addParamRow(QString(), QString()); });
        } else {
            m_payloadEdit = new PlainTextEdit(this);
            m_payloadEdit->setPlainText(initial.value(QStringLiteral("payload")).toString());
            m_payloadEdit->setMinimumHeight(100);
            layout->addWidget(new StrongBodyLabel(AppI18n::text("内容"), this));
            layout->addWidget(m_payloadEdit);
        }

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    QVariantMap result() const
    {
        QVariantMap m;
        m.insert(QStringLiteral("name"), m_nameEdit->text().trimmed());
        m.insert(QStringLiteral("mode"), m_modeCombo->currentData().toString());
        m.insert(QStringLiteral("lineEnding"), m_lineEndingCombo->currentData().toString());
        m.insert(QStringLiteral("enabled"), m_enabledCheck->isChecked());
        if (m_templateMode) {
            m.insert(QStringLiteral("templateText"), m_templateEdit->toPlainText());
            QJsonArray hints;
            QJsonArray defaults;
            for (const ParamRow &row : m_paramRows) {
                hints.append(row.hint->text());
                defaults.append(row.defaultValue->text());
            }
            m.insert(QStringLiteral("paramHintsJson"),
                     QString::fromUtf8(QJsonDocument(hints).toJson(QJsonDocument::Compact)));
            m.insert(QStringLiteral("paramDefaultsJson"),
                     QString::fromUtf8(QJsonDocument(defaults).toJson(QJsonDocument::Compact)));
        } else {
            m.insert(QStringLiteral("payload"), m_payloadEdit->toPlainText());
        }
        return m;
    }

  private:
    struct ParamRow {
        QWidget *row = nullptr;
        LineEdit *hint = nullptr;
        LineEdit *defaultValue = nullptr;
    };

    void removeParamRow(QWidget *rowWidget)
    {
        if (!rowWidget || !m_paramLayout) {
            return;
        }
        for (int i = 0; i < m_paramRows.size(); ++i) {
            if (m_paramRows.at(i).row != rowWidget) {
                continue;
            }
            m_paramLayout->removeWidget(rowWidget);
            m_paramRows.removeAt(i);
            rowWidget->deleteLater();
            return;
        }
    }

    void addParamRow(const QString &hint, const QString &defVal)
    {
        auto *row = new QWidget(m_paramContainer);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(6);
        auto *h = new LineEdit(row);
        h->setPlaceholderText(AppI18n::text("提示（用于占位输入框）"));
        h->setText(hint);
        auto *v = new LineEdit(row);
        v->setPlaceholderText(AppI18n::text("默认值"));
        v->setText(defVal);
        hl->addWidget(h, 1);
        hl->addWidget(v, 1);
        auto *removeBtn = new TransparentToolButton(icon(FluentIcon::Delete), row);
        removeBtn->setFixedSize(CompactControlHeight, CompactControlHeight);
        removeBtn->setIconSize(QSize(16, 16));
        AppUi::setFluentToolTip(removeBtn, AppI18n::text("删除"), ToolTipPosition::Top);
        removeBtn->setAccessibleName(AppI18n::text("删除"));
        hl->addWidget(removeBtn, 0, Qt::AlignVCenter);
        connect(removeBtn, &TransparentToolButton::clicked, this, [this, row]() { removeParamRow(row); });
        m_paramLayout->addWidget(row);
        m_paramRows.append(ParamRow{row, h, v});
    }

    bool m_templateMode = false;
    LineEdit *m_nameEdit = nullptr;
    ComboBox *m_modeCombo = nullptr;
    ComboBox *m_lineEndingCombo = nullptr;
    CheckBox *m_enabledCheck = nullptr;
    PlainTextEdit *m_templateEdit = nullptr;
    PlainTextEdit *m_payloadEdit = nullptr;
    QWidget *m_paramContainer = nullptr;
    QVBoxLayout *m_paramLayout = nullptr;
    QList<ParamRow> m_paramRows;
};

} // namespace

void WorkbenchPage::setupQuickCommandsPanel(QWidget *terminalWidget, QSplitter *splitter)
{
    m_mainWorkbenchSplitter = splitter;

    m_quickCommandsPanelWidget = new QWidget(splitter);
    m_quickCommandsPanelWidget->setMinimumWidth(QuickCommandsPanelMinWidth);
    auto *panelLayout = new QVBoxLayout(m_quickCommandsPanelWidget);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(8);

    auto *toolbar = new QHBoxLayout;
    toolbar->setSpacing(4);

    auto *addMenu = new RoundMenu(m_quickCommandsPanelWidget);
    connect(addMenu->addAction(FluentIcon::Folder, AppI18n::text("文件夹")), &QAction::triggered, this,
            &WorkbenchPage::addQuickCommandsFolder);
    connect(addMenu->addAction(FluentIcon::Document, AppI18n::text("固定命令")), &QAction::triggered, this,
            &WorkbenchPage::addQuickCommandsFixedCommand);
    connect(addMenu->addAction(FluentIcon::Code, AppI18n::text("模板命令")), &QAction::triggered, this,
            &WorkbenchPage::addQuickCommandsTemplateCommand);
    auto *addMenuBtn = new TransparentDropDownPushButton(icon(FluentIcon::Add), AppI18n::text("添加"), m_quickCommandsPanelWidget);
    makeToolbarTextButton(addMenuBtn, 40);
    addMenuBtn->setDropDownMenu(addMenu);
    AppUi::setFluentToolTip(addMenuBtn, AppI18n::text("添加文件夹或命令"));
    toolbar->addWidget(addMenuBtn);

    auto *editBtn = new TransparentToolButton(icon(FluentIcon::Edit), m_quickCommandsPanelWidget);
    makeToolbarSquareButton(editBtn);
    AppUi::setFluentToolTip(editBtn, AppI18n::text("编辑"));
    connect(editBtn, &TransparentToolButton::clicked, this, &WorkbenchPage::editQuickCommandsSelection);
    toolbar->addWidget(editBtn);

    auto *delBtn = new TransparentToolButton(icon(FluentIcon::Delete), m_quickCommandsPanelWidget);
    makeToolbarSquareButton(delBtn);
    AppUi::setFluentToolTip(delBtn, AppI18n::text("删除"));
    connect(delBtn, &TransparentToolButton::clicked, this, &WorkbenchPage::deleteQuickCommandsSelection);
    toolbar->addWidget(delBtn);

    auto *importBtn = new TransparentToolButton(icon(FluentIcon::Download), m_quickCommandsPanelWidget);
    makeToolbarSquareButton(importBtn);
    AppUi::setFluentToolTip(importBtn, AppI18n::text("导入"));
    connect(importBtn, &TransparentToolButton::clicked, this, &WorkbenchPage::importQuickCommands);
    toolbar->addWidget(importBtn);

    auto *exportBtn = new TransparentToolButton(icon(FluentIcon::SaveAs), m_quickCommandsPanelWidget);
    makeToolbarSquareButton(exportBtn);
    AppUi::setFluentToolTip(exportBtn, AppI18n::text("导出"));
    connect(exportBtn, &TransparentToolButton::clicked, this, &WorkbenchPage::exportQuickCommands);
    toolbar->addWidget(exportBtn);

    toolbar->addStretch(1);
    panelLayout->addLayout(toolbar);

    m_quickCommandsScroll = new ScrollArea(m_quickCommandsPanelWidget);
    m_quickCommandsScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (m_quickCommandsScroll->horizontalFluentScrollBar()) {
        m_quickCommandsScroll->horizontalFluentScrollBar()->setForceHidden(true);
    }
    m_quickCommandsScroll->setFrameShape(QFrame::NoFrame);
    m_quickCommandsScroll->setWidgetResizable(true);
    m_quickCommandsScroll->enableTransparentBackground();

    m_quickCommandsListHost = new QWidget(m_quickCommandsScroll);
    m_quickCommandsListHost->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_quickCommandsListLayout = new QVBoxLayout(m_quickCommandsListHost);
    m_quickCommandsListLayout->setContentsMargins(0, 0, 0, 0);
    m_quickCommandsListLayout->setSpacing(10);
    m_quickCommandsListLayout->addStretch(1);

    m_quickCommandsScroll->setWidget(m_quickCommandsListHost);
    panelLayout->addWidget(m_quickCommandsScroll, 1);

    reinstallQuickCommandsClickFilters();

    splitter->addWidget(terminalWidget);
    splitter->addWidget(m_quickCommandsPanelWidget);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 0);
    splitter->setCollapsible(0, false);
    splitter->setCollapsible(1, false);
    connect(splitter, &QSplitter::splitterMoved, this, &WorkbenchPage::onQuickCommandsSplitterMoved);

    m_quickCommandsPanelWidget->hide();
}

void WorkbenchPage::toggleQuickCommandsPanel(bool visible)
{
    m_quickCommandsPanelVisible = visible;
    if (m_quickCommandsToggleButton) {
        const QSignalBlocker b(m_quickCommandsToggleButton);
        m_quickCommandsToggleButton->setChecked(visible);
    }
    if (!m_mainWorkbenchSplitter || !m_quickCommandsPanelWidget) {
        return;
    }
    m_quickCommandsPanelWidget->setVisible(visible);
    QList<int> sizes = m_mainWorkbenchSplitter->sizes();
    if (sizes.size() < 2) {
        return;
    }
    int total = sizes.at(0) + sizes.at(1);
    if (total <= 0) {
        total = 1000;
    }
    if (visible) {
        const int w = qBound(QuickCommandsPanelMinWidth, m_quickCommandsPanelWidth, total - 200);
        m_mainWorkbenchSplitter->setSizes({total - w, w});
    } else {
        m_quickCommandsPanelWidth = qBound(QuickCommandsPanelMinWidth, sizes.at(1), 800);
        m_mainWorkbenchSplitter->setSizes({total, 0});
    }

    AppSettings settings;
    settings.setValue(QStringLiteral("workbench/quickCommandsPanelVisible"), visible);
    settings.setValue(QStringLiteral("workbench/quickCommandsPanelWidth"), m_quickCommandsPanelWidth);
}

void WorkbenchPage::onQuickCommandsSplitterMoved(int, int)
{
    if (!m_mainWorkbenchSplitter || !m_quickCommandsPanelWidget || !m_quickCommandsPanelVisible) {
        return;
    }
    const QList<int> sizes = m_mainWorkbenchSplitter->sizes();
    if (sizes.size() >= 2) {
        m_quickCommandsPanelWidth = qBound(QuickCommandsPanelMinWidth, sizes.at(1), 800);
        AppSettings settings;
        settings.setValue(QStringLiteral("workbench/quickCommandsPanelWidth"), m_quickCommandsPanelWidth);
    }
    relayoutAllQuickCommandPreviews(m_quickCommandsListHost);
}

void WorkbenchPage::applyQuickCommandRowPreview(QWidget *rowWidget)
{
    if (!rowWidget) {
        return;
    }
    auto *inner = rowWidget->findChild<QWidget *>(QStringLiteral("qcCommandInner"));
    if (!inner) {
        return;
    }
    const QVariantMap map = rowWidget->property(PropCommandData).toMap();
    setQuickCommandRowPreview(inner, map);
}

void WorkbenchPage::refreshQuickCommandRowPreview(QWidget *rowWidget) { applyQuickCommandRowPreview(rowWidget); }

ExpandSettingCard *WorkbenchPage::quickCommandsParentFolderFromLayout(QVBoxLayout *lay) const
{
    if (!lay) {
        return nullptr;
    }
    for (QWidget *w = lay->parentWidget(); w; w = w->parentWidget()) {
        if (w == m_quickCommandsListHost) {
            return nullptr;
        }
        if (auto *esc = qobject_cast<ExpandSettingCard *>(w)) {
            return esc->property(PropFolderCard).toBool() ? esc : nullptr;
        }
    }
    return nullptr;
}

QVBoxLayout *WorkbenchPage::quickCommandsTargetLayoutForAdd()
{
    if (!m_quickCommandsListLayout) {
        return nullptr;
    }
    QObject *ctx = m_quickCommandsSelectedContext;
    if (!ctx) {
        return m_quickCommandsListLayout;
    }
    if (auto *w = qobject_cast<QWidget *>(ctx)) {
        if (w->property(PropCommandCard).toBool()) {
            for (QWidget *par = w->parentWidget(); par; par = par->parentWidget()) {
                if (auto *esc = qobject_cast<ExpandSettingCard *>(par)) {
                    if (esc->property(PropFolderCard).toBool()) {
                        return esc->viewLayout();
                    }
                }
            }
            return m_quickCommandsListLayout;
        }
    }
    if (auto *esc = qobject_cast<ExpandSettingCard *>(ctx)) {
        if (esc->property(PropFolderCard).toBool()) {
            return esc->viewLayout();
        }
    }
    return m_quickCommandsListLayout;
}

void WorkbenchPage::updateQuickCommandsSelectionHighlight()
{
    if (!m_quickCommandsListHost) {
        return;
    }
    const bool dark = ThemeManager::instance()->effectiveTheme() == Theme::Dark;
    const QString borderRule =
        dark ? QStringLiteral("#qcQuickCmdFolderHeader, #qcQuickCmdCommandBorder { border: 2px solid #60cdff; "
                              "border-radius: 6px; }")
             : QStringLiteral("#qcQuickCmdFolderHeader, #qcQuickCmdCommandBorder { border: 2px solid #0078d4; "
                              "border-radius: 6px; }");

    for (SimpleCardWidget *c : m_quickCommandsListHost->findChildren<SimpleCardWidget *>()) {
        if (!c->property(PropCommandCard).toBool()) {
            continue;
        }
        c->setClickEnabled(true);
        c->setSelected(false);
        QWidget *row = c->parentWidget();
        if (row && row->property(PropCommandRowOuter).toBool()) {
            row->setStyleSheet(c == m_quickCommandsSelectedContext ? borderRule : QString());
        }
    }
    for (ExpandSettingCard *f : m_quickCommandsListHost->findChildren<ExpandSettingCard *>()) {
        if (!f->property(PropFolderCard).toBool()) {
            continue;
        }
        HeaderSettingCard *hdr = f->card();
        if (!hdr) {
            continue;
        }
        hdr->setStyleSheet(f == m_quickCommandsSelectedContext ? borderRule : QString());
    }
}

void WorkbenchPage::reinstallQuickCommandsClickFilters()
{
    if (!m_quickCommandsScroll || !m_quickCommandsScroll->viewport()) {
        return;
    }
    m_quickCommandsScroll->viewport()->installEventFilter(this);
}

void WorkbenchPage::moveQuickCommandsBlock(QWidget *block, int delta)
{
    if (!block || delta == 0) {
        return;
    }
    QWidget *host = block->parentWidget();
    auto *parentLay = qobject_cast<QBoxLayout *>(host ? host->layout() : nullptr);
    if (!parentLay) {
        return;
    }
    const int oldIndex = parentLay->indexOf(block);
    if (oldIndex < 0) {
        return;
    }
    int lastIdx = parentLay->count() - 1;
    if (lastIdx >= 0 && parentLay->itemAt(lastIdx)->spacerItem()) {
        --lastIdx;
    }
    const int newIndex = qBound(0, oldIndex + delta, lastIdx);
    if (newIndex == oldIndex || lastIdx < 0) {
        return;
    }
    parentLay->removeWidget(block);
    parentLay->insertWidget(newIndex, block);

    syncQuickCommandsDataFromList();
    saveQuickCommands();

    if (auto *vl = qobject_cast<QVBoxLayout *>(parentLay)) {
        if (ExpandSettingCard *fc = quickCommandsParentFolderFromLayout(vl)) {
            updateFolderCardSubtitle(fc);
        }
    }
}

void WorkbenchPage::wireQuickCommandsFolderReorderButtons(ExpandSettingCard *folderCard)
{
    if (!folderCard || !folderCard->property(PropFolderCard).toBool()) {
        return;
    }
    HeaderSettingCard *hdr = folderCard->card();
    ExpandButton *expandBtn = folderCard->expandButton();
    if (!hdr || !expandBtn) {
        return;
    }
    auto *actionHL = hdr->actionLayout();
    if (!actionHL) {
        return;
    }
    // Replace HeaderSettingCard's default [expand] strip with one compact row: up | down | expand,
    // right-aligned — avoids HeaderSettingCard::addWidget placing each extra control with 19px gaps.
    while (actionHL->count() > 0) {
        delete actionHL->takeAt(0);
    }
    auto *group = new QWidget(hdr);
    auto *hl = new QHBoxLayout(group);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(2);
    auto *upBtn = new QuickCmdChevronButton(QuickCmdChevronKind::Up, group, 30);
    AppUi::setFluentToolTip(upBtn, AppI18n::text("上移"));
    connect(upBtn, &QToolButton::clicked, this, [this, folderCard]() { moveQuickCommandsBlock(folderCard, -1); });
    auto *downBtn = new QuickCmdChevronButton(QuickCmdChevronKind::Down, group, 30);
    AppUi::setFluentToolTip(downBtn, AppI18n::text("下移"));
    connect(downBtn, &QToolButton::clicked, this, [this, folderCard]() { moveQuickCommandsBlock(folderCard, 1); });
    hl->addWidget(upBtn, 0, Qt::AlignVCenter);
    hl->addWidget(downBtn, 0, Qt::AlignVCenter);
    hl->addWidget(expandBtn, 0, Qt::AlignVCenter);
    actionHL->addWidget(group, 0, Qt::AlignRight | Qt::AlignVCenter);
    actionHL->addSpacing(8);
}

void WorkbenchPage::quickCommandsHandleListClick(QWidget *widget)
{
    if (!widget) {
        return;
    }
    QObject *prevSelection = m_quickCommandsSelectedContext;
    if (auto *folder = qobject_cast<ExpandSettingCard *>(widget)) {
        if (folder->property(PropFolderCard).toBool()) {
            m_quickCommandsSelectedContext = folder;
            if (prevSelection != m_quickCommandsSelectedContext) {
                updateQuickCommandsSelectionHighlight();
            }
            return;
        }
    }
    for (QWidget *p = widget; p; p = p->parentWidget()) {
        if (auto *cmd = qobject_cast<SimpleCardWidget *>(p)) {
            if (cmd->property(PropCommandCard).toBool()) {
                m_quickCommandsSelectedContext = cmd;
                if (prevSelection != m_quickCommandsSelectedContext) {
                    updateQuickCommandsSelectionHighlight();
                }
                return;
            }
        }
        if (auto *folder = qobject_cast<ExpandSettingCard *>(p)) {
            if (folder->property(PropFolderCard).toBool()) {
                m_quickCommandsSelectedContext = folder;
                if (prevSelection != m_quickCommandsSelectedContext) {
                    updateQuickCommandsSelectionHighlight();
                }
                return;
            }
        }
    }
}

SimpleCardWidget *WorkbenchPage::insertQuickCommandRow(QVBoxLayout *targetLay, int index, const QVariantMap &map,
                                                       bool isTemplate)
{
    if (!targetLay) {
        return nullptr;
    }
    QWidget *host = targetLay->parentWidget();
    if (!host) {
        return nullptr;
    }
    ExpandSettingCard *parentFolder = quickCommandsParentFolderFromLayout(targetLay);

    SimpleCardWidget *cmdRow = buildCommandRowWidget(map, isTemplate);
    auto *rowOuter = new QWidget(host);
    rowOuter->setObjectName(QStringLiteral("qcQuickCmdCommandBorder"));
    rowOuter->setProperty(PropCommandRowOuter, true);
    auto *rowHl = new QHBoxLayout(rowOuter);
    rowHl->setContentsMargins(0, 0, 0, 0);
    rowHl->setSpacing(6);
    rowHl->addWidget(cmdRow, 1);
    auto *rightCol = new QWidget(rowOuter);
    auto *rvl = new QVBoxLayout(rightCol);
    rvl->setContentsMargins(0, 0, 0, 0);
    rvl->setSpacing(4);
    auto *sendBtn = new TransparentToolButton(icon(FluentIcon::Send), rightCol);
    sendBtn->setObjectName(QStringLiteral("qcSendBtn"));
    sendBtn->setProperty(PropSendBtn, true);
    makeToolbarSquareButton(sendBtn);
    AppUi::setFluentToolTip(sendBtn, AppI18n::text("发送"));
    sendBtn->setEnabled(m_serial.isOpen() && map.value(QStringLiteral("enabled"), true).toBool());
    connect(sendBtn, &QToolButton::clicked, this, [this, cmdRow]() { sendQuickCommandRow(cmdRow); });
    auto *upMoveBtn = new QuickCmdChevronButton(QuickCmdChevronKind::Up, rightCol, CompactControlHeight);
    AppUi::setFluentToolTip(upMoveBtn, AppI18n::text("上移"));
    connect(upMoveBtn, &QToolButton::clicked, this, [this, rowOuter]() { moveQuickCommandsBlock(rowOuter, -1); });
    auto *downMoveBtn = new QuickCmdChevronButton(QuickCmdChevronKind::Down, rightCol, CompactControlHeight);
    AppUi::setFluentToolTip(downMoveBtn, AppI18n::text("下移"));
    connect(downMoveBtn, &QToolButton::clicked, this, [this, rowOuter]() { moveQuickCommandsBlock(rowOuter, 1); });
    rvl->addWidget(sendBtn, 0, Qt::AlignHCenter);
    rvl->addWidget(upMoveBtn, 0, Qt::AlignHCenter);
    rvl->addWidget(downMoveBtn, 0, Qt::AlignHCenter);
    connect(cmdRow, &SimpleCardWidget::clicked, this, [this, cmdRow]() {
        m_quickCommandsSelectedContext = cmdRow;
        updateQuickCommandsSelectionHighlight();
    });
    rowHl->addWidget(rightCol, 0, Qt::AlignTop);

    int ix = index;
    if (targetLay->count() > 0) {
        QLayoutItem *last = targetLay->itemAt(targetLay->count() - 1);
        if (last && last->spacerItem()) {
            const int maxBeforeStretch = targetLay->count() - 1;
            ix = qBound(0, index, maxBeforeStretch);
        } else {
            ix = qBound(0, index, targetLay->count());
        }
    } else {
        ix = 0;
    }
    targetLay->insertWidget(ix, rowOuter);

    QWidget *inner = cmdRow->findChild<QWidget *>(QStringLiteral("qcCommandInner"));
    if (inner) {
        for (LineEdit *e : inner->findChildren<LineEdit *>()) {
            if (!e->property("qcParamIndex").isValid()) {
                continue;
            }
            connect(e, &QLineEdit::textChanged, this, [this, cmdRow, parentFolder]() {
                refreshQuickCommandRowPreview(cmdRow);
                syncQuickCommandsDataFromList();
                saveQuickCommands();
                if (parentFolder) {
                    updateFolderCardSubtitle(parentFolder);
                }
            });
        }
    }
    if (parentFolder) {
        updateFolderCardSubtitle(parentFolder);
    }
    m_quickCommandsSelectedContext = cmdRow;
    updateQuickCommandsSelectionHighlight();
    return cmdRow;
}

void WorkbenchPage::rebuildQuickCommandsList()
{
    if (!m_quickCommandsListLayout || !m_quickCommandsScroll) {
        return;
    }
    m_quickCommandsSelectedContext = nullptr;

    QLayoutItem *child;
    while ((child = m_quickCommandsListLayout->takeAt(0)) != nullptr) {
        if (child->widget()) {
            child->widget()->deleteLater();
        }
        delete child;
    }
    m_quickCommandsListLayout->addStretch(1);

    QJsonArray copy = m_quickCommandsData;
    flattenQuickCommandsDataJson(copy);
    for (const QJsonValue &v : copy) {
        if (v.isObject()) {
            quickCommandsJsonToItem(v.toObject());
        }
    }
    reinstallQuickCommandsClickFilters();
    updateQuickCommandsActionState();
    updateQuickCommandsSelectionHighlight();
    QTimer::singleShot(0, this, [this]() { relayoutAllQuickCommandPreviews(m_quickCommandsListHost); });
}

void WorkbenchPage::quickCommandsJsonToItem(const QJsonObject &object)
{
    if (!m_quickCommandsListLayout || !m_quickCommandsListHost) {
        return;
    }
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("folder")) {
        QString name;
        bool expanded = true;
        QJsonArray children;
        QString err;
        if (!jsonToFolder(object, &name, &expanded, &children, &err)) {
            return;
        }

        auto *folderCard = new ExpandSettingCard(FluentIcon::Folder,
                                                name.isEmpty() ? AppI18n::text("文件夹") : name, QString(),
                                                m_quickCommandsListHost);
        folderCard->setProperty(PropFolderCard, true);
        folderCard->setExpanded(expanded);
        updateFolderCardSubtitle(folderCard);
        if (HeaderSettingCard *hdr = folderCard->card()) {
            hdr->setObjectName(QStringLiteral("qcQuickCmdFolderHeader"));
        }
        wireQuickCommandsFolderReorderButtons(folderCard);

        QVBoxLayout *body = folderCard->viewLayout();
        body->setContentsMargins(8, 0, 8, 8);
        body->setSpacing(8);

        for (const QJsonValue &cv : children) {
            if (!cv.isObject()) {
                continue;
            }
            const QJsonObject co = cv.toObject();
            if (co.value(QStringLiteral("type")).toString() != QStringLiteral("command")) {
                continue;
            }
            QString cname, mode, lineEnding, payloadOrTpl;
            QStringList hints, defaults;
            bool enabled = true;
            bool isTemplate = false;
            QString parseErr;
            if (!jsonToCommandNode(co, &cname, &mode, &lineEnding, &enabled, &isTemplate, &payloadOrTpl, &hints,
                                   &defaults, &parseErr)) {
                continue;
            }
            QVariantMap m;
            m.insert(QStringLiteral("name"), cname);
            m.insert(QStringLiteral("mode"), mode);
            m.insert(QStringLiteral("lineEnding"), lineEnding);
            m.insert(QStringLiteral("enabled"), enabled);
            m.insert(QStringLiteral("template"), isTemplate);
            if (isTemplate) {
                m.insert(QStringLiteral("templateText"), payloadOrTpl);
                QJsonArray hj;
                QJsonArray dj;
                for (const QString &x : hints) {
                    hj.append(x);
                }
                for (const QString &x : defaults) {
                    dj.append(x);
                }
                m.insert(QStringLiteral("paramHintsJson"),
                         QString::fromUtf8(QJsonDocument(hj).toJson(QJsonDocument::Compact)));
                m.insert(QStringLiteral("paramDefaultsJson"),
                         QString::fromUtf8(QJsonDocument(dj).toJson(QJsonDocument::Compact)));
            } else {
                m.insert(QStringLiteral("payload"), payloadOrTpl);
            }

            insertQuickCommandRow(body, body->count(), m, isTemplate);
        }

        connect(folderCard, &ExpandSettingCard::expandedChanged, this, [this, folderCard](bool) {
            syncQuickCommandsDataFromList();
            saveQuickCommands(false);
            updateFolderCardSubtitle(folderCard);
        });

        const int ins = m_quickCommandsListLayout->count() - 1;
        m_quickCommandsListLayout->insertWidget(ins, folderCard);
        return;
    }

    if (type == QStringLiteral("command")) {
        QString name, mode, lineEnding, payloadOrTpl;
        QStringList hints, defaults;
        bool enabled = true;
        bool isTemplate = false;
        QString parseErr;
        if (!jsonToCommandNode(object, &name, &mode, &lineEnding, &enabled, &isTemplate, &payloadOrTpl, &hints,
                               &defaults, &parseErr)) {
            return;
        }
        QVariantMap m;
        m.insert(QStringLiteral("name"), name);
        m.insert(QStringLiteral("mode"), mode);
        m.insert(QStringLiteral("lineEnding"), lineEnding);
        m.insert(QStringLiteral("enabled"), enabled);
        m.insert(QStringLiteral("template"), isTemplate);
        if (isTemplate) {
            m.insert(QStringLiteral("templateText"), payloadOrTpl);
            QJsonArray hj;
            QJsonArray dj;
            for (const QString &x : hints) {
                hj.append(x);
            }
            for (const QString &x : defaults) {
                dj.append(x);
            }
            m.insert(QStringLiteral("paramHintsJson"),
                     QString::fromUtf8(QJsonDocument(hj).toJson(QJsonDocument::Compact)));
            m.insert(QStringLiteral("paramDefaultsJson"),
                     QString::fromUtf8(QJsonDocument(dj).toJson(QJsonDocument::Compact)));
        } else {
            m.insert(QStringLiteral("payload"), payloadOrTpl);
        }
        const int ins = m_quickCommandsListLayout->count() - 1;
        insertQuickCommandRow(m_quickCommandsListLayout, ins, m, isTemplate);
    }
}

void WorkbenchPage::rebuildQuickCommandsTree() { rebuildQuickCommandsList(); }

QJsonObject WorkbenchPage::quickCommandsFolderCardToJson(ExpandSettingCard *card) const
{
    if (!card || !card->property(PropFolderCard).toBool()) {
        return QJsonObject();
    }
    QJsonArray ch;
    QVBoxLayout *body = card->viewLayout();
    if (body) {
        for (int i = 0; i < body->count(); ++i) {
            QLayoutItem *it = body->itemAt(i);
            QWidget *rowOuter = it ? it->widget() : nullptr;
            if (!rowOuter) {
                continue;
            }
            SimpleCardWidget *cmdRow = rowOuter->findChild<SimpleCardWidget *>(QStringLiteral("qcCommandCard"));
            if (!cmdRow) {
                continue;
            }
            QJsonObject co = quickCommandsCommandRowToJson(cmdRow);
            if (!co.isEmpty()) {
                ch.append(co);
            }
        }
    }
    HeaderSettingCard *hdr = card->card();
    const QString title = hdr && hdr->titleLabel() ? hdr->titleLabel()->text().trimmed() : QString();
    return folderToJson(title.isEmpty() ? AppI18n::text("文件夹") : title, card->isExpanded(), ch);
}

QJsonObject WorkbenchPage::quickCommandsCommandRowToJson(QWidget *rowWidget) const
{
    if (!rowWidget || !rowWidget->property(PropCommandCard).toBool()) {
        return QJsonObject();
    }
    QVariantMap m = rowWidget->property(PropCommandData).toMap();
    const bool isTemplate = m.value(QStringLiteral("template")).toBool();
    QStringList hints;
    QStringList defaults;
    QJsonParseError pe1;
    QJsonParseError pe2;
    const QJsonArray ha =
        QJsonDocument::fromJson(m.value(QStringLiteral("paramHintsJson")).toString().toUtf8(), &pe1).array();
    const QJsonArray da =
        QJsonDocument::fromJson(m.value(QStringLiteral("paramDefaultsJson")).toString().toUtf8(), &pe2).array();
    for (const QJsonValue &v : ha) {
        hints.append(v.toString());
    }
    for (const QJsonValue &v : da) {
        defaults.append(v.toString());
    }
    if (isTemplate) {
        QWidget *w = rowWidget;
        const QList<LineEdit *> edits = w->findChildren<LineEdit *>();
        int maxIx = -1;
        QMap<int, QString> vals;
        for (LineEdit *e : edits) {
            const QVariant ix = e->property("qcParamIndex");
            if (!ix.isValid()) {
                continue;
            }
            const int k = ix.toInt();
            maxIx = qMax(maxIx, k);
            vals.insert(k, e->text());
        }
        const int count = qMax(hints.size(), qMax(defaults.size(), maxIx + 1));
        QStringList nh;
        QStringList nd;
        nh.reserve(count);
        nd.reserve(count);
        for (int i = 0; i < count; ++i) {
            nh.append(i < hints.size() ? hints.at(i) : QString());
            QString def = i < defaults.size() ? defaults.at(i) : QString();
            if (vals.contains(i)) {
                def = vals.value(i);
            }
            nd.append(def);
        }
        hints = nh;
        defaults = nd;
    }
    const QString body = isTemplate ? m.value(QStringLiteral("templateText")).toString()
                                    : m.value(QStringLiteral("payload")).toString();
    return commandNodeToJson(commandNameMap(m), m.value(QStringLiteral("mode")).toString(),
                           m.value(QStringLiteral("lineEnding")).toString(),
                           m.value(QStringLiteral("enabled"), true).toBool(), isTemplate, body, hints, defaults);
}

void WorkbenchPage::syncQuickCommandsDataFromList()
{
    m_quickCommandsData = QJsonArray();
    if (!m_quickCommandsListLayout) {
        return;
    }
    for (int i = 0; i < m_quickCommandsListLayout->count(); ++i) {
        QLayoutItem *it = m_quickCommandsListLayout->itemAt(i);
        QWidget *w = it ? it->widget() : nullptr;
        if (!w) {
            continue;
        }
        if (auto *folderCard = qobject_cast<ExpandSettingCard *>(w)) {
            if (folderCard->property(PropFolderCard).toBool()) {
                QJsonObject o = quickCommandsFolderCardToJson(folderCard);
                if (!o.isEmpty()) {
                    m_quickCommandsData.append(o);
                }
            }
            continue;
        }
        SimpleCardWidget *cmdRow = w->findChild<SimpleCardWidget *>(QStringLiteral("qcCommandCard"));
        if (cmdRow) {
            QJsonObject o = quickCommandsCommandRowToJson(cmdRow);
            if (!o.isEmpty()) {
                m_quickCommandsData.append(o);
            }
        }
    }
}

void WorkbenchPage::loadQuickCommands()
{
    m_quickCommandsData = QJsonArray();
    AppSettings settings;
    const QByteArray json = settings.value(QStringLiteral("send/quickCommands")).toString().toUtf8();
    if (!json.isEmpty()) {
        QJsonParseError pe;
        const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
        if (pe.error == QJsonParseError::NoError && doc.isObject()) {
            const QJsonObject root = doc.object();
            if (root.value(QStringLiteral("schema")).toString() == QLatin1String(QuickCommandsSchemaName) &&
                root.value(QStringLiteral("version")).toInt() == QuickCommandsSchemaVersion) {
                m_quickCommandsData = root.value(QStringLiteral("tree")).toArray();
            }
        }
    }
    rebuildQuickCommandsList();

    m_quickCommandsPanelVisible =
        settings.value(QStringLiteral("workbench/quickCommandsPanelVisible"), false).toBool();
    m_quickCommandsPanelWidth = qBound(
        QuickCommandsPanelMinWidth,
        settings.value(QStringLiteral("workbench/quickCommandsPanelWidth"), QuickCommandsPanelDefaultWidth).toInt(),
        800);
    toggleQuickCommandsPanel(m_quickCommandsPanelVisible);
}

void WorkbenchPage::saveQuickCommands(bool publishToOtherPages) const
{
    WorkbenchPage *page = const_cast<WorkbenchPage *>(this);
    page->syncQuickCommandsDataFromList();
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(QuickCommandsSchemaName));
    root.insert(QStringLiteral("version"), QuickCommandsSchemaVersion);
    root.insert(QStringLiteral("tree"), m_quickCommandsData);

    AppSettings settings;
    settings.setValue(QStringLiteral("send/quickCommands"),
                      QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact)));
    if (publishToOtherPages) {
        // Defer so other pages reload after this call stack unwinds — avoids re-entering
        // rebuildQuickCommandsList while we are still mutating widgets (crash on add/save).
        QTimer::singleShot(0, page, [page]() { page->publishSharedList(SharedList::QuickCommands); });
    }
}

int WorkbenchPage::quickCommandsCountCommands() const { return countCommandsInJsonArray(m_quickCommandsData); }

void WorkbenchPage::updateQuickCommandsActionState()
{
    if (!m_quickCommandsListLayout) {
        return;
    }
    const bool open = m_serial.isOpen();
    auto updateRow = [&](QWidget *rowOuter) {
        if (!rowOuter) {
            return;
        }
        SimpleCardWidget *cmdRow = rowOuter->findChild<SimpleCardWidget *>(QStringLiteral("qcCommandCard"));
        if (!cmdRow) {
            return;
        }
        auto *btn = rowOuter->findChild<QAbstractButton *>(QStringLiteral("qcSendBtn"));
        if (!btn) {
            return;
        }
        const QVariantMap m = cmdRow->property(PropCommandData).toMap();
        btn->setEnabled(open && m.value(QStringLiteral("enabled"), true).toBool());
    };
    for (int i = 0; i < m_quickCommandsListLayout->count(); ++i) {
        QLayoutItem *it = m_quickCommandsListLayout->itemAt(i);
        QWidget *w = it ? it->widget() : nullptr;
        if (!w) {
            continue;
        }
        if (auto *folderCard = qobject_cast<ExpandSettingCard *>(w)) {
            if (!folderCard->property(PropFolderCard).toBool()) {
                continue;
            }
            QVBoxLayout *body = folderCard->viewLayout();
            if (!body) {
                continue;
            }
            for (int j = 0; j < body->count(); ++j) {
                QLayoutItem *bit = body->itemAt(j);
                updateRow(bit ? bit->widget() : nullptr);
            }
            continue;
        }
        updateRow(w);
    }
}

QStringList WorkbenchPage::collectTemplateParamValues(QWidget *rowWidget) const
{
    QStringList out;
    if (!rowWidget) {
        return out;
    }
    return quickCommandParamTextsFromRow(rowWidget->findChild<QWidget *>(QStringLiteral("qcCommandInner")));
}

void WorkbenchPage::sendQuickCommandRow(QWidget *rowWidget)
{
    if (!rowWidget || !rowWidget->property(PropCommandCard).toBool()) {
        return;
    }
    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法发送"), AppI18n::text("请先连接串口"));
        return;
    }
    QVariantMap m = rowWidget->property(PropCommandData).toMap();
    if (!m.value(QStringLiteral("enabled"), true).toBool()) {
        showWarning(AppI18n::text("无法发送"), AppI18n::text("该命令已停用"));
        return;
    }
    const QString mode = m.value(QStringLiteral("mode")).toString();
    const QString lineEnding = m.value(QStringLiteral("lineEnding")).toString();
    QString payload;
    if (m.value(QStringLiteral("template")).toBool()) {
        QString err;
        QWidget *inner = rowWidget->findChild<QWidget *>(QStringLiteral("qcCommandInner"));
        payload = applyTemplatePlaceholders(m.value(QStringLiteral("templateText")).toString(),
                                            quickCommandParamTextsFromRow(inner), &err);
        if (err == QStringLiteral("missing_param") || err == QStringLiteral("unresolved")) {
            showWarning(AppI18n::text("无法发送"), AppI18n::text("模板参数未填写或占位符未全部替换"));
            return;
        }
    } else {
        payload = m.value(QStringLiteral("payload")).toString();
    }

    SendPacket pkt;
    pkt.group = QString();
    pkt.name = commandNameMap(m);
    pkt.note = QString();
    pkt.mode = mode;
    pkt.payload = payload;
    pkt.lineEnding = lineEnding;
    pkt.enabled = true;
    sendPacketPayload(pkt);
}

void WorkbenchPage::addQuickCommandsFolder()
{
    if (!m_quickCommandsListLayout || !m_quickCommandsListHost) {
        return;
    }
    bool ok = false;
    const QString name = QInputDialog::getText(window(), AppI18n::text("新建文件夹"), AppI18n::text("名称："),
                                                QLineEdit::Normal, AppI18n::text("新建分组"), &ok);
    if (!ok || name.trimmed().isEmpty()) {
        return;
    }
    auto *folderCard = new ExpandSettingCard(FluentIcon::Folder, name.trimmed(), QString(), m_quickCommandsListHost);
    folderCard->setProperty(PropFolderCard, true);
    folderCard->setExpanded(true);
    if (HeaderSettingCard *hdr = folderCard->card()) {
        hdr->setObjectName(QStringLiteral("qcQuickCmdFolderHeader"));
    }
    wireQuickCommandsFolderReorderButtons(folderCard);
    folderCard->viewLayout()->setContentsMargins(8, 0, 8, 8);
    folderCard->viewLayout()->setSpacing(8);
    updateFolderCardSubtitle(folderCard);
    connect(folderCard, &ExpandSettingCard::expandedChanged, this, [this, folderCard](bool) {
        syncQuickCommandsDataFromList();
        saveQuickCommands(false);
        updateFolderCardSubtitle(folderCard);
    });
    const int ins = m_quickCommandsListLayout->count() - 1;
    m_quickCommandsListLayout->insertWidget(ins, folderCard);
    m_quickCommandsSelectedContext = folderCard;
    updateQuickCommandsSelectionHighlight();
    syncQuickCommandsDataFromList();
    saveQuickCommands();
}

void WorkbenchPage::addQuickCommandsFixedCommand()
{
    if (!m_quickCommandsListLayout || !m_quickCommandsListHost) {
        return;
    }
    if (quickCommandsCountCommands() >= MaxQuickCommandItems) {
        showWarning(AppI18n::text("无法添加"), AppI18n::text("常用命令数量已达上限"));
        return;
    }
    QVariantMap initial;
    initial.insert(QStringLiteral("name"), AppI18n::text("新命令"));
    initial.insert(QStringLiteral("mode"), QStringLiteral("text"));
    initial.insert(QStringLiteral("lineEnding"), QStringLiteral("none"));
    initial.insert(QStringLiteral("enabled"), true);
    QuickCommandEditDialog dlg(window(), AppI18n::text("添加固定命令"), false, initial);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    QVariantMap m = dlg.result();
    if (m.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        showWarning(AppI18n::text("无法添加"), AppI18n::text("名称不能为空"));
        return;
    }
    m.insert(QStringLiteral("template"), false);

    QVBoxLayout *targetLay = quickCommandsTargetLayoutForAdd();
    if (!targetLay) {
        return;
    }
    insertQuickCommandRow(targetLay, targetLay->count(), m, false);
    syncQuickCommandsDataFromList();
    saveQuickCommands();
}

void WorkbenchPage::addQuickCommandsTemplateCommand()
{
    if (!m_quickCommandsListLayout || !m_quickCommandsListHost) {
        return;
    }
    if (quickCommandsCountCommands() >= MaxQuickCommandItems) {
        showWarning(AppI18n::text("无法添加"), AppI18n::text("常用命令数量已达上限"));
        return;
    }
    QVariantMap initial;
    initial.insert(QStringLiteral("name"), AppI18n::text("新模板"));
    initial.insert(QStringLiteral("mode"), QStringLiteral("text"));
    initial.insert(QStringLiteral("lineEnding"), QStringLiteral("none"));
    initial.insert(QStringLiteral("enabled"), true);
    initial.insert(QStringLiteral("templateText"), QStringLiteral("AT+CMD={{0}}"));
    QJsonArray h;
    h.append(AppI18n::text("参数"));
    QJsonArray d;
    d.append(QString());
    initial.insert(QStringLiteral("paramHintsJson"), QString::fromUtf8(QJsonDocument(h).toJson(QJsonDocument::Compact)));
    initial.insert(QStringLiteral("paramDefaultsJson"), QString::fromUtf8(QJsonDocument(d).toJson(QJsonDocument::Compact)));

    QuickCommandEditDialog dlg(window(), AppI18n::text("添加模板命令"), true, initial);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    QVariantMap m = dlg.result();
    if (m.value(QStringLiteral("name")).toString().trimmed().isEmpty()) {
        showWarning(AppI18n::text("无法添加"), AppI18n::text("名称不能为空"));
        return;
    }
    m.insert(QStringLiteral("template"), true);

    QVBoxLayout *targetLay = quickCommandsTargetLayoutForAdd();
    if (!targetLay) {
        return;
    }
    insertQuickCommandRow(targetLay, targetLay->count(), m, true);
    syncQuickCommandsDataFromList();
    saveQuickCommands();
}

void WorkbenchPage::deleteQuickCommandsSelection()
{
    QObject *ctx = m_quickCommandsSelectedContext;
    if (!ctx) {
        showWarning(AppI18n::text("无法删除"), AppI18n::text("请先选择项目"));
        return;
    }
    if (auto *folder = qobject_cast<ExpandSettingCard *>(ctx)) {
        if (!folder->property(PropFolderCard).toBool()) {
            return;
        }
        if (countCommandsInFolderLayout(folder->viewLayout()) > 0) {
            const int r = QMessageBox::question(window(), AppI18n::text("确认删除"), AppI18n::text("确定删除该项及其子项？"),
                                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (r != QMessageBox::Yes) {
                return;
            }
        }
        folder->deleteLater();
        m_quickCommandsSelectedContext = nullptr;
        syncQuickCommandsDataFromList();
        saveQuickCommands();
        return;
    }
    if (auto *cmdRow = qobject_cast<SimpleCardWidget *>(ctx)) {
        if (!cmdRow->property(PropCommandCard).toBool()) {
            return;
        }
        ExpandSettingCard *folderForSubtitle = nullptr;
        for (QWidget *p = cmdRow->parentWidget(); p; p = p->parentWidget()) {
            if (auto *esc = qobject_cast<ExpandSettingCard *>(p)) {
                if (esc->property(PropFolderCard).toBool()) {
                    folderForSubtitle = esc;
                }
                break;
            }
        }
        QWidget *rowOuter = cmdRow->parentWidget();
        if (rowOuter) {
            rowOuter->deleteLater();
        } else {
            cmdRow->deleteLater();
        }
        m_quickCommandsSelectedContext = nullptr;
        if (folderForSubtitle) {
            updateFolderCardSubtitle(folderForSubtitle);
        }
        updateQuickCommandsSelectionHighlight();
        syncQuickCommandsDataFromList();
        saveQuickCommands();
    }
}

void WorkbenchPage::editQuickCommandsSelection()
{
    QObject *ctx = m_quickCommandsSelectedContext;
    if (!ctx) {
        return;
    }
    if (auto *folder = qobject_cast<ExpandSettingCard *>(ctx)) {
        if (!folder->property(PropFolderCard).toBool()) {
            return;
        }
        bool ok = false;
        HeaderSettingCard *hdr = folder->card();
        const QString cur = hdr && hdr->titleLabel() ? hdr->titleLabel()->text() : QString();
        const QString name = QInputDialog::getText(window(), AppI18n::text("重命名文件夹"), AppI18n::text("名称："),
                                                  QLineEdit::Normal, cur, &ok);
        if (ok && !name.trimmed().isEmpty()) {
            if (hdr) {
                hdr->setTitle(name.trimmed());
            }
            syncQuickCommandsDataFromList();
            saveQuickCommands();
            updateFolderCardSubtitle(folder);
        }
        return;
    }
    auto *cmdRow = qobject_cast<SimpleCardWidget *>(ctx);
    if (!cmdRow || !cmdRow->property(PropCommandCard).toBool()) {
        return;
    }
    QWidget *rowOuter = cmdRow->parentWidget();
    if (!rowOuter || !rowOuter->parentWidget()) {
        return;
    }
    QVBoxLayout *targetLay = qobject_cast<QVBoxLayout *>(rowOuter->parentWidget()->layout());
    if (!targetLay) {
        return;
    }
    const int rowIndex = targetLay->indexOf(rowOuter);
    if (rowIndex < 0) {
        return;
    }

    QVariantMap m = cmdRow->property(PropCommandData).toMap();
    const bool isTemplate = m.value(QStringLiteral("template")).toBool();
    QuickCommandEditDialog dlg(window(), AppI18n::text("编辑命令"), isTemplate, m);
    if (dlg.exec() != QDialog::Accepted) {
        return;
    }
    m = dlg.result();
    m.insert(QStringLiteral("template"), isTemplate);

    ExpandSettingCard *folderForSubtitle = nullptr;
    for (QWidget *p = rowOuter->parentWidget(); p; p = p->parentWidget()) {
        if (auto *esc = qobject_cast<ExpandSettingCard *>(p)) {
            if (esc->property(PropFolderCard).toBool()) {
                folderForSubtitle = esc;
            }
            break;
        }
    }

    targetLay->removeWidget(rowOuter);
    rowOuter->deleteLater();
    insertQuickCommandRow(targetLay, rowIndex, m, isTemplate);
    syncQuickCommandsDataFromList();
    saveQuickCommands();
    updateQuickCommandsSelectionHighlight();
    if (folderForSubtitle) {
        updateFolderCardSubtitle(folderForSubtitle);
    }
}

void WorkbenchPage::importQuickCommands()
{
    AppSettings settings;
    const QString initialFolder =
        settings.value(QStringLiteral("export/folder"), WorkbenchPagePrivate::defaultExportFolder()).toString();
    const QString path = QFileDialog::getOpenFileName(window(), AppI18n::text("导入常用命令"), initialFolder,
                                                      QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        showError(AppI18n::text("导入失败"), file.errorString());
        return;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        showError(AppI18n::text("导入失败"), AppI18n::text("JSON 格式无效"));
        return;
    }
    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("schema")).toString() != QLatin1String(QuickCommandsSchemaName) ||
        root.value(QStringLiteral("version")).toInt() != QuickCommandsSchemaVersion) {
        showError(AppI18n::text("导入失败"), AppI18n::text("不是常用命令 JSON 格式"));
        return;
    }
    const QJsonArray tree = root.value(QStringLiteral("tree")).toArray();
    QString err;
    if (!validateCommandLimit(tree, MaxQuickCommandItems, &err)) {
        showWarning(AppI18n::text("无法导入"), AppI18n::text("命令数量超过上限"));
        return;
    }
    m_quickCommandsData = tree;
    rebuildQuickCommandsList();
    saveQuickCommands();
    showSuccess(AppI18n::text("导入成功"), QFileInfo(path).fileName());
}

void WorkbenchPage::exportQuickCommands()
{
    syncQuickCommandsDataFromList();
    QJsonObject root;
    root.insert(QStringLiteral("schema"), QString::fromLatin1(QuickCommandsSchemaName));
    root.insert(QStringLiteral("version"), QuickCommandsSchemaVersion);
    root.insert(QStringLiteral("tree"), m_quickCommandsData);

    AppSettings settings;
    const QString initialFolder =
        settings.value(QStringLiteral("export/folder"), WorkbenchPagePrivate::defaultExportFolder()).toString();
    const QString initialName = QDir(initialFolder).filePath(QStringLiteral("fluentserial-quick-commands.json"));
    QString path = QFileDialog::getSaveFileName(window(), AppI18n::text("导出常用命令"), initialName,
                                                QStringLiteral("JSON (*.json)"));
    if (path.isEmpty()) {
        return;
    }
    if (!path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive)) {
        path += QStringLiteral(".json");
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        showError(AppI18n::text("导出失败"), file.errorString());
        return;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    settings.setValue(QStringLiteral("export/folder"), QFileInfo(path).path());
    showSuccess(AppI18n::text("导出成功"), QFileInfo(path).fileName());
}

void WorkbenchPage::reloadQuickCommandsList() { loadQuickCommands(); }
