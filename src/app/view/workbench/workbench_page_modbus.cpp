#include "app/core/app_i18n.h"
#include "app/core/app_settings.h"
#include "app/view/muted_text.h"
#include "app/view/workbench/workbench_page_internal.h"

using namespace FluentQt;
using namespace WorkbenchPagePrivate;

QWidget *WorkbenchPage::createModbusSection()
{
    auto *section = new ExpandSettingCard(FluentIcon::CommandPrompt, QStringLiteral("Modbus RTU"), QString(), this);
    auto *root = cardBody(section);

    m_modbusSlaveEdit = createNumberEdit(section, 1, 1, 247);
    addFormRow(root, AppI18n::text("站号"), m_modbusSlaveEdit);

    m_modbusProbeCheck = new CheckBox(AppI18n::text("站号探测"), section);
    m_modbusProbeCheck->setToolTip(AppI18n::text("勾选后发送将依次向站号 0–255 发出当前读/写命令；站号 0 为广播，248–255 非标准 Modbus，请根据终端日志自行判断回复。"));
    root->addWidget(m_modbusProbeCheck);

    m_modbusProbeIntervalRow = new QWidget(section);
    auto *intervalRowLayout = new QHBoxLayout(m_modbusProbeIntervalRow);
    intervalRowLayout->setContentsMargins(0, 0, 0, 0);
    intervalRowLayout->setSpacing(8);
    auto *intervalLabel = new BodyLabel(AppI18n::text("探测间隔"), m_modbusProbeIntervalRow);
    setFixedControlWidth(intervalLabel, FormLabelWidth);
    intervalRowLayout->addWidget(intervalLabel, 0, Qt::AlignVCenter);
    m_modbusProbeIntervalEdit = createNumberEdit(m_modbusProbeIntervalRow, DefaultModbusProbeIntervalMs, 1, 60000);
    m_modbusProbeIntervalEdit->setMinimumWidth(0);
    m_modbusProbeIntervalEdit->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    intervalRowLayout->addWidget(m_modbusProbeIntervalEdit, 1, Qt::AlignVCenter);
    intervalRowLayout->addWidget(unitLabel(QStringLiteral("ms"), m_modbusProbeIntervalRow), 0, Qt::AlignVCenter);
    root->addWidget(m_modbusProbeIntervalRow);
    m_modbusProbeIntervalRow->hide();

    m_modbusFunctionCombo = new ComboBox(section);
    addModbusFunctionOptions(m_modbusFunctionCombo);
    makeCompactControl(m_modbusFunctionCombo);
    const int defaultFunctionIndex = m_modbusFunctionCombo->findData(AppModbus::defaultFunctionKey());
    if (defaultFunctionIndex >= 0) {
        m_modbusFunctionCombo->setCurrentIndex(defaultFunctionIndex);
    }
    addFormRow(root, AppI18n::text("功能"), m_modbusFunctionCombo);

    m_modbusAddressEdit = createNumberEdit(section, 0, 0, 65535);
    addFormRow(root, AppI18n::text("地址"), m_modbusAddressEdit);

    m_modbusQuantityEdit = createNumberEdit(section, 1, 1, 2000);
    addFormRow(root, AppI18n::text("数量"), m_modbusQuantityEdit);

    m_modbusValuesEdit = new PlainTextEdit(section);
    m_modbusValuesEdit->setPlaceholderText(AppI18n::text("写入值，例如：1 2 0x1234"));
    m_modbusValuesEdit->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_modbusValuesEdit->setWordWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    m_modbusValuesEdit->setFixedHeight(60);
    m_modbusValuesEdit->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    root->addWidget(m_modbusValuesEdit);

    auto *actionRow = new QHBoxLayout;
    actionRow->setSpacing(8);
    m_modbusFillButton = new PushButton(icon(FluentIcon::Edit), AppI18n::text("填入发送"), section);
    m_modbusSendButton = new PrimaryPushButton(icon(FluentIcon::Send), AppI18n::text("发送"), section);
    setButtonRowControlPolicy(m_modbusFillButton);
    setButtonRowControlPolicy(m_modbusSendButton);
    actionRow->addWidget(m_modbusFillButton);
    actionRow->addWidget(m_modbusSendButton);
    root->addLayout(actionRow);

    m_modbusStatusLabel = new CaptionLabel(AppI18n::text("生成帧会自动附加 CRC16-Modbus"), section);
    AppUi::setMutedTextColor(m_modbusStatusLabel);
    m_modbusStatusLabel->setWordWrap(true);
    root->addWidget(m_modbusStatusLabel);

    connect(m_modbusFunctionCombo, &ComboBox::currentIndexChanged, this, [this](int) {
        updateModbusUi();
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/function"), m_modbusFunctionCombo->currentData().toString());
    });
    connect(m_modbusSlaveEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/slave"), numberEditValue(m_modbusSlaveEdit, 1, 1, 247));
    });
    connect(m_modbusAddressEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/address"), numberEditValue(m_modbusAddressEdit, 0, 0, 65535));
    });
    connect(m_modbusQuantityEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/quantity"), numberEditValue(m_modbusQuantityEdit, 1, 1, 2000));
    });
    connect(m_modbusValuesEdit, &PlainTextEdit::textChanged, this, [this]() {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/values"), m_modbusValuesEdit->toPlainText());
    });
    connect(m_modbusProbeCheck, &CheckBox::toggled, this, [this](bool checked) {
        if (m_modbusProbeActive && !checked) {
            stopModbusProbe();
        }
        refreshModbusProbeIntervalVisibility();
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/probeEnabled"), checked);
    });
    connect(m_modbusProbeIntervalEdit, &LineEdit::textChanged, this, [this](const QString &) {
        AppSettings settings;
        settings.setValue(QStringLiteral("modbus/probeIntervalMs"),
                         numberEditValue(m_modbusProbeIntervalEdit, DefaultModbusProbeIntervalMs, 1, 60000));
    });
    connect(m_modbusFillButton, &PushButton::clicked, this, &WorkbenchPage::fillModbusRequest);
    connect(m_modbusSendButton, &PrimaryPushButton::clicked, this, &WorkbenchPage::sendModbusRequest);

    updateModbusUi();
    refreshModbusProbeIntervalVisibility();
    makeCollapsibleCard(section, QStringLiteral("modbus"));
    return section;
}

AppModbus::RequestConfig WorkbenchPage::currentModbusConfig(int slaveOverride) const
{
    AppModbus::RequestConfig config;
    if (slaveOverride < 0) {
        config.slaveId = static_cast<quint8>(numberEditValue(m_modbusSlaveEdit, 1, 1, 247));
    } else {
        config.slaveId = static_cast<quint8>(qBound(0, slaveOverride, 255));
    }
    config.functionKey =
        m_modbusFunctionCombo ? m_modbusFunctionCombo->currentData().toString() : AppModbus::defaultFunctionKey();
    const AppModbus::FunctionOption function = AppModbus::functionForKey(config.functionKey);
    config.address = static_cast<quint16>(numberEditValue(m_modbusAddressEdit, 0, 0, 65535));
    config.quantity =
        static_cast<quint16>(numberEditValue(m_modbusQuantityEdit, 1, 1, AppModbus::maximumQuantity(function)));
    config.valuesText = m_modbusValuesEdit ? m_modbusValuesEdit->toPlainText() : QString();
    return config;
}

QByteArray WorkbenchPage::currentModbusFrame(bool *ok, int slaveOverride, bool allowNonStandardSlaveId)
{
    if (ok) {
        *ok = false;
    }

    AppModbus::RequestConfig config = currentModbusConfig(slaveOverride);
    config.allowNonStandardSlaveId = allowNonStandardSlaveId;
    const AppModbus::BuildResult result = AppModbus::buildRequest(config);
    if (!result.ok) {
        setModbusStatusText(result.errorMessage);
        showWarning(AppI18n::text("Modbus 帧生成失败"), result.errorMessage);
        return {};
    }

    if (ok) {
        *ok = true;
    }
    setModbusStatusText(QStringLiteral("%1 · %2").arg(result.summary, bytesToHex(result.frame)));
    return result.frame;
}

void WorkbenchPage::fillModbusRequest()
{
    bool ok = false;
    const QByteArray frame = currentModbusFrame(&ok);
    if (!ok) {
        return;
    }

    const int eolIndex = m_lineEndingCombo->findData(QStringLiteral("none"));
    if (eolIndex >= 0) {
        m_lineEndingCombo->setCurrentIndex(eolIndex);
    }
    if (m_checksumAppendCheck) {
        m_checksumAppendCheck->setChecked(false);
    }
    applySendEditorState(true, bytesToHex(frame));
    setModbusStatusText(AppI18n::text("已填入发送区 · %1").arg(bytesToHex(frame)));
}

void WorkbenchPage::sendModbusFrame(const QByteArray &frame, const QString &statusText)
{
    QString error;
    if (!m_serial.writeData(frame, &error)) {
        showError(AppI18n::text("Modbus 发送失败"), error);
        stopModbusProbe();
        return;
    }

    SendHistoryItem historyItem;
    historyItem.mode = QStringLiteral("hex");
    historyItem.payload = bytesToHex(frame);
    historyItem.lineEnding = QStringLiteral("none");
    addSendHistory(historyItem);
    appendRecord(RecordDirection::Tx, frame, true, QString(), false, true);
    setModbusStatusText(statusText);
}

void WorkbenchPage::refreshModbusProbeIntervalVisibility()
{
    if (m_modbusProbeIntervalRow) {
        m_modbusProbeIntervalRow->setVisible(m_modbusProbeCheck && m_modbusProbeCheck->isChecked());
    }
}

void WorkbenchPage::updateModbusProbeUi(bool probing)
{
    const bool editable = !probing;
    if (m_modbusSlaveEdit) {
        m_modbusSlaveEdit->setEnabled(editable);
    }
    if (m_modbusProbeIntervalEdit) {
        m_modbusProbeIntervalEdit->setEnabled(editable);
    }
    if (m_modbusFunctionCombo) {
        m_modbusFunctionCombo->setEnabled(editable);
    }
    if (m_modbusAddressEdit) {
        m_modbusAddressEdit->setEnabled(editable);
    }
    updateModbusUi();
    if (m_modbusFillButton) {
        m_modbusFillButton->setEnabled(editable);
    }
    if (m_modbusSendButton) {
        m_modbusSendButton->setText(probing ? AppI18n::text("停止探测") : AppI18n::text("发送"));
    }
}

void WorkbenchPage::stopModbusProbe(const QString &message)
{
    if (!m_modbusProbeActive) {
        return;
    }
    m_modbusProbeTimer.stop();
    m_modbusProbeActive = false;
    m_modbusProbeNextSlave = 0;
    updateModbusProbeUi(false);
    if (!message.isEmpty()) {
        setModbusStatusText(message);
    }
}

void WorkbenchPage::startModbusProbe()
{
    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法发送 Modbus 请求"), AppI18n::text("请先连接串口"));
        return;
    }

    AppModbus::RequestConfig precheck = currentModbusConfig(1);
    precheck.allowNonStandardSlaveId = false;
    const AppModbus::BuildResult pre = AppModbus::buildRequest(precheck);
    if (!pre.ok) {
        showWarning(AppI18n::text("Modbus 帧生成失败"), pre.errorMessage);
        return;
    }

    m_modbusProbeNextSlave = 0;
    m_modbusProbeActive = true;
    updateModbusProbeUi(true);
    sendModbusProbeStep();
}

void WorkbenchPage::sendModbusProbeStep()
{
    if (!m_modbusProbeActive) {
        return;
    }
    if (!m_serial.isOpen()) {
        stopModbusProbe();
        showWarning(AppI18n::text("Modbus 探测中止"), AppI18n::text("串口已断开"));
        return;
    }

    if (m_modbusProbeNextSlave > 255) {
        stopModbusProbe(AppI18n::text("站号探测已完成（0–255）"));
        return;
    }

    const int slave = m_modbusProbeNextSlave;
    AppModbus::RequestConfig config = currentModbusConfig(slave);
    config.allowNonStandardSlaveId = true;
    const AppModbus::BuildResult result = AppModbus::buildRequest(config);
    if (!result.ok) {
        stopModbusProbe();
        showWarning(AppI18n::text("Modbus 探测中止"), result.errorMessage);
        return;
    }

    const QString status =
        AppI18n::text("探测发送 · 站号 %1/255 · %2").arg(slave).arg(bytesToHex(result.frame));
    sendModbusFrame(result.frame, status);
    if (!m_modbusProbeActive) {
        return;
    }

    ++m_modbusProbeNextSlave;
    if (m_modbusProbeNextSlave > 255) {
        stopModbusProbe(AppI18n::text("站号探测已完成（0–255）"));
        return;
    }

    const int intervalMs =
        numberEditValue(m_modbusProbeIntervalEdit, DefaultModbusProbeIntervalMs, 1, 60000);
    m_modbusProbeTimer.start(intervalMs);
}

void WorkbenchPage::sendModbusRequest()
{
    if (m_modbusProbeActive) {
        stopModbusProbe(AppI18n::text("站号探测已停止"));
        return;
    }

    if (m_modbusProbeCheck && m_modbusProbeCheck->isChecked()) {
        startModbusProbe();
        return;
    }

    if (!m_serial.isOpen()) {
        showWarning(AppI18n::text("无法发送 Modbus 请求"), AppI18n::text("请先连接串口"));
        return;
    }

    bool ok = false;
    const QByteArray frame = currentModbusFrame(&ok);
    if (!ok) {
        return;
    }

    sendModbusFrame(frame, AppI18n::text("已发送 · %1").arg(bytesToHex(frame)));
}

void WorkbenchPage::updateModbusUi()
{
    if (!m_modbusFunctionCombo) {
        return;
    }

    const AppModbus::FunctionOption function =
        AppModbus::functionForKey(m_modbusFunctionCombo->currentData().toString());
    const bool needsValues = function.write;
    const bool quantityEnabled = !function.write || function.multiple;
    if (m_modbusQuantityEdit) {
        const int maximumQuantity = AppModbus::maximumQuantity(function);
        m_modbusQuantityEdit->setEnabled(quantityEnabled && (!m_modbusProbeActive));
        m_modbusQuantityEdit->setValidator(new QIntValidator(1, maximumQuantity, m_modbusQuantityEdit));
        if (!quantityEnabled) {
            setNumberEditValue(m_modbusQuantityEdit, 1, 1, maximumQuantity);
        } else {
            setNumberEditValue(m_modbusQuantityEdit, numberEditValue(m_modbusQuantityEdit, 1, 1, maximumQuantity), 1,
                               maximumQuantity);
        }
    }
    if (m_modbusValuesEdit) {
        const bool valuesEnabled = needsValues && !m_modbusProbeActive;
        m_modbusValuesEdit->setEnabled(valuesEnabled);
        if (!needsValues) {
            m_modbusValuesEdit->setPlaceholderText(AppI18n::text("读取功能无需填写值"));
        } else if (function.coil) {
            m_modbusValuesEdit->setPlaceholderText(function.multiple
                                                       ? AppI18n::text("线圈值列表，例如：1 0 1 1")
                                                       : AppI18n::text("线圈值：0/1、true/false 或 on/off"));
        } else {
            m_modbusValuesEdit->setPlaceholderText(function.multiple ? AppI18n::text("寄存器值列表，例如：1 2 0x1234")
                                                                     : AppI18n::text("寄存器值：0-65535 或 0x1234"));
        }
    }
}

void WorkbenchPage::updateModbusResponseStatus(const QByteArray &data)
{
    if (!m_modbusStatusLabel || data.isEmpty()) {
        return;
    }

    const AppModbus::ParseResult result = AppModbus::parseResponse(data);
    if (result.recognized) {
        setModbusStatusText(result.summary);
    }
}

void WorkbenchPage::setModbusStatusText(const QString &text)
{
    if (m_modbusStatusLabel) {
        m_modbusStatusLabel->setText(text);
    }
}