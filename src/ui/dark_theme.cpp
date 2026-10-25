#include "dark_theme.h"

namespace vdj {

QString darkThemeStylesheet(const QString& family)
{
    const QString fontFamily = family.isEmpty() ? QStringLiteral("'Liberation Sans', 'DejaVu Sans', sans-serif")
                                                : QStringLiteral("'%1'").arg(QString(family).remove(QLatin1Char('\'')));
    return QStringLiteral("QWidget { background-color: #1a1a1a; color: #e0e0e0; font-family: %1; }").arg(fontFamily) + QStringLiteral(
        "QMenuBar { background-color: #242424; color: #e0e0e0;"
        "  border-bottom: 1px solid #333333; }"
        "QMenuBar::item:selected { background-color: #3a3a3a; }"
        "QMenu { background-color: #242424; color: #e0e0e0;"
        "  border: 1px solid #333333; }"
        "QMenu::item:selected { background-color: #3a3a3a; }"
        "QStatusBar { background-color: #242424; color: #e0e0e0;"
        "  border-top: 1px solid #333333; }"
        "QToolBar { background-color: #242424; border: none;"
        "  border-bottom: 1px solid #333333; spacing: 4px; }"
        "QToolButton { background-color: transparent; color: #e0e0e0;"
        "  padding: 4px; border-radius: 4px; }"
        "QToolButton:hover { background-color: #3a3a3a; }"
        "QToolButton:pressed { background-color: #4a4a4a; }"
        "QToolButton:checked { background-color: #2a6a3a; }"
        "QScrollArea { background-color: #1a1a1a; border: none; }"
        "QScrollBar:vertical { background: #242424; width: 12px; }"
        "QScrollBar::handle:vertical { background: #3a3a3a;"
        "  border-radius: 6px; min-height: 24px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical"
        "  { height: 0; }"
        "QLabel { background: transparent; }"
    );
}

} // namespace vdj
