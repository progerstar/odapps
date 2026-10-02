#ifndef ODRFIDSTYLE_H
#define ODRFIDSTYLE_H

#include <QString>

inline QString odRfidStyleSheet(bool dark)
{
    const QString window = dark ? QStringLiteral("#202226") : QStringLiteral("#F5F6FA");
    const QString surface = dark ? QStringLiteral("#292C31") : QStringLiteral("#FFFFFF");
    const QString base = dark ? QStringLiteral("#1D1F23") : QStringLiteral("#FFFFFF");
    const QString subtle = dark ? QStringLiteral("#34373D") : QStringLiteral("#EEF0F5");
    const QString border = dark ? QStringLiteral("#454952") : QStringLiteral("#D8DCE6");
    const QString text = dark ? QStringLiteral("#F1F3F5") : QStringLiteral("#20232A");
    const QString muted = dark ? QStringLiteral("#B5BAC4") : QStringLiteral("#667085");
    const QString disabled = dark ? QStringLiteral("#7D828C") : QStringLiteral("#98A2B3");
    const QString accent = dark ? QStringLiteral("#8B7CF6") : QStringLiteral("#5B45D6");
    const QString accentHover = dark ? QStringLiteral("#9E91FF") : QStringLiteral("#6C56E2");
    const QString accentPressed = dark ? QStringLiteral("#7566DC") : QStringLiteral("#4936B6");

    QString style = QStringLiteral(R"ODRFID(
QMainWindow, QDialog {
    background-color: @window@;
    color: @text@;
}

QMenuBar {
    background-color: @surface@;
    border-bottom: 1px solid @border@;
    padding: 2px 4px;
}
QMenuBar::item {
    background: transparent;
    border-radius: 4px;
    padding: 5px 8px;
}
QMenuBar::item:selected {
    background-color: @subtle@;
}
QMenu {
    background-color: @surface@;
    border: 1px solid @border@;
    padding: 5px;
}
QMenu::item {
    border-radius: 4px;
    padding: 6px 28px 6px 10px;
}
QMenu::item:selected {
    background-color: @accent@;
    color: white;
}
QMenu::separator {
    background-color: @border@;
    height: 1px;
    margin: 4px 8px;
}

QToolBar {
    background-color: @surface@;
    border: none;
    border-bottom: 1px solid @border@;
    padding: 5px 7px;
    spacing: 4px;
}
QToolBar::separator {
    background-color: @border@;
    width: 1px;
    margin: 4px 6px;
}
QToolBar QToolButton {
    background: transparent;
    border: 1px solid transparent;
    border-radius: 6px;
    min-height: 24px;
    padding: 4px 8px;
}
QToolBar QToolButton:hover,
QToolButton#mfcKeyCopyTool:hover,
QToolButton#mfcKeyPasteTool:hover,
QToolButton#modUIDTool:hover {
    background-color: @subtle@;
    border-color: @border@;
}
QToolBar QToolButton:pressed,
QToolButton#mfcKeyCopyTool:pressed,
QToolButton#mfcKeyPasteTool:pressed,
QToolButton#modUIDTool:pressed {
    background-color: @border@;
}
QToolBar QToolButton:focus,
QToolButton#mfcKeyCopyTool:focus,
QToolButton#mfcKeyPasteTool:focus,
QToolButton#modUIDTool:focus {
    border: 2px solid @accent@;
}
QToolButton#mfcKeyCopyTool,
QToolButton#mfcKeyPasteTool,
QToolButton#modUIDTool {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 6px;
    min-height: 24px;
    min-width: 24px;
    padding: 3px;
}

QPushButton {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 6px;
    min-height: 22px;
    padding: 5px 12px;
}
QPushButton:hover {
    background-color: @subtle@;
    border-color: @accent@;
}
QPushButton:pressed {
    background-color: @border@;
}
QPushButton:default {
    background-color: @accent@;
    border-color: @accent@;
    color: white;
}
QPushButton:default:hover {
    background-color: @accentHover@;
    border-color: @accentHover@;
}
QPushButton:default:pressed {
    background-color: @accentPressed@;
    border-color: @accentPressed@;
}
QPushButton:focus {
    border: 2px solid @accent@;
}
QPushButton:disabled {
    background-color: @subtle@;
    border-color: @border@;
    color: @disabled@;
}

QLineEdit, QComboBox {
    background-color: @base@;
    border: 1px solid @border@;
    border-radius: 6px;
    min-height: 22px;
    padding: 4px 7px;
    selection-background-color: @accent@;
    selection-color: white;
}
QLineEdit:hover, QComboBox:hover {
    border-color: @muted@;
}
QLineEdit:focus, QComboBox:focus {
    border: 2px solid @accent@;
}
QLineEdit[readOnly="true"] {
    background-color: @subtle@;
    color: @muted@;
}
QLineEdit:disabled, QComboBox:disabled {
    background-color: @subtle@;
    color: @disabled@;
}
QComboBox QAbstractItemView {
    background-color: @surface@;
    border: 1px solid @border@;
    outline: none;
    padding: 3px;
    selection-background-color: @accent@;
    selection-color: white;
}

QCheckBox, QRadioButton {
    min-height: 24px;
    spacing: 7px;
}

QGroupBox {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 8px;
    margin-top: 11px;
    padding-top: 7px;
}
QGroupBox::title {
    background-color: @surface@;
    font-weight: 600;
    left: 10px;
    padding: 0 5px;
    subcontrol-origin: margin;
    subcontrol-position: top left;
}

QTabWidget::pane {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 8px;
    top: -1px;
}
QTabBar::tab {
    background: transparent;
    border: none;
    border-bottom: 2px solid transparent;
    color: @muted@;
    min-width: 72px;
    padding: 8px 13px;
}
QTabBar::tab:hover {
    color: @text@;
}
QTabBar::tab:selected {
    border-bottom-color: @accent@;
    color: @accent@;
}

QAbstractItemView {
    background-color: @base@;
    alternate-background-color: @subtle@;
    border: 1px solid @border@;
    border-radius: 5px;
    outline: none;
    selection-background-color: @accent@;
    selection-color: white;
}
QAbstractItemView:focus {
    border: 2px solid @accent@;
}
QTableView {
    gridline-color: @border@;
}
QHeaderView::section, QTableCornerButton::section {
    background-color: @subtle@;
    border: none;
    border-bottom: 1px solid @border@;
    border-right: 1px solid @border@;
    font-weight: 600;
    padding: 5px 7px;
}

QLabel#deviceInfoLabel,
QLabel#readerVersionLabel,
QLabel#cardTypeLabel {
    background-color: @surface@;
    border: 1px solid @border@;
    border-radius: 6px;
    font-weight: 600;
    padding: 6px 10px;
}
QLabel#stubCardLabel {
    color: @disabled@;
}
QLineEdit#cardUIDLabel {
    background-color: @base@;
    color: @text@;
    font-weight: 600;
}

QStatusBar {
    background-color: @surface@;
    border-top: 1px solid @border@;
    color: @muted@;
}
QStatusBar::item {
    border: none;
}
QToolTip {
    background-color: @text@;
    border: 1px solid @border@;
    color: @window@;
    padding: 4px 6px;
}
)ODRFID");

    style.replace(QStringLiteral("@window@"), window);
    style.replace(QStringLiteral("@surface@"), surface);
    style.replace(QStringLiteral("@base@"), base);
    style.replace(QStringLiteral("@subtle@"), subtle);
    style.replace(QStringLiteral("@border@"), border);
    style.replace(QStringLiteral("@text@"), text);
    style.replace(QStringLiteral("@muted@"), muted);
    style.replace(QStringLiteral("@disabled@"), disabled);
    style.replace(QStringLiteral("@accent@"), accent);
    style.replace(QStringLiteral("@accentHover@"), accentHover);
    style.replace(QStringLiteral("@accentPressed@"), accentPressed);
    return style;
}

#endif // ODRFIDSTYLE_H
