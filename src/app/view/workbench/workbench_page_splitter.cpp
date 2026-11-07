#include "app/view/workbench/workbench_page_internal.h"

#include <QtCore/QtMath>
#include <QtGui/QShowEvent>
#include <QtWidgets/QSplitter>

using namespace WorkbenchPagePrivate;

void WorkbenchPage::applyTerminalSendSplitterSizes()
{
    if (!m_terminalSendSplitter || !m_terminalSectionWidget || !m_sendSectionWidget) {
        return;
    }
    const int total = m_terminalSendSplitter->height();
    if (total <= 0) {
        return;
    }
    const int spacing = m_terminalSendSplitter->handleWidth();
    const int available = total - spacing;
    if (available <= 0) {
        return;
    }
    const int minTop = m_terminalSectionWidget->minimumSizeHint().height();
    const int minBottom = m_sendSectionWidget->minimumSizeHint().height();
    if (minTop + minBottom > available) {
        return;
    }
    double r = qBound(0.12, m_terminalSendTopRatio, 0.88);
    int top = qRound(static_cast<double>(available) * r);
    top = qBound(minTop, top, available - minBottom);
    m_terminalSendSplitter->setSizes({top, available - top});
}
