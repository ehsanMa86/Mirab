/*
 * Anahita - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Anahita
 *
 * File:    src/app/Theme.cpp
 * Purpose: Light application theme: palette, style sheet and shared colors.
 *
 * Copyright (C) 2026 Ehsan Madadi
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the Free
 * Software Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "Theme.h"

#include <QApplication>
#include <QDir>
#include <QPainter>
#include <QPixmap>
#include <QStandardPaths>
#include <QFont>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace anahita::app::theme
{

namespace
{
QString hex(const QColor& c)
{
    return c.name(QColor::HexRgb);
}
} // namespace

namespace
{
/// Writes a small chevron image for combo boxes and spin boxes and returns its path.
QString chevron(bool up)
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty())
        dir = QDir::tempPath();
    QDir().mkpath(dir);
    const QString file = QDir(dir).filePath(up ? "chevron-up.png" : "chevron-down.png");
    QPixmap pm(20, 20);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(QColor(90, 104, 124), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (up)
        p.drawPolyline(QPolygonF({QPointF(5, 12.5), QPointF(10, 7.5), QPointF(15, 12.5)}));
    else
        p.drawPolyline(QPolygonF({QPointF(5, 7.5), QPointF(10, 12.5), QPointF(15, 7.5)}));
    p.end();
    pm.save(file);
    return QDir::fromNativeSeparators(file);
}
} // namespace

QString styleSheet()
{
    QString s = R"(
QMainWindow, QDialog { background: @window; }
QMainWindow::separator { background: @border; width: 1px; height: 1px; }
QMainWindow::separator:hover { background: @accent; }

QMenuBar { background: @surface; border-bottom: 1px solid @border; padding: 2px 4px; }
QMenuBar::item { background: transparent; padding: 5px 10px; border-radius: 4px; color: @text; }
QMenuBar::item:selected { background: @accentSoft; color: @text; }
QMenuBar::item:pressed { background: @accentSoft; }
QMenu { background: @surface; border: 1px solid @borderStrong; padding: 4px; }
QMenu::item { padding: 6px 28px 6px 26px; border-radius: 4px; color: @text; }
QMenu::item:selected { background: @accentSoft; color: @text; }
QMenu::item:disabled { color: #9aa5b1; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QMenu::icon { padding-left: 6px; }

QToolBar { background: @surface; border: none; border-bottom: 1px solid @border; padding: 3px 6px; spacing: 2px; }
QToolBar::separator { background: @border; width: 1px; margin: 5px 6px; }
QToolButton { background: transparent; border: 1px solid transparent; border-radius: 6px; padding: 4px; color: @text; }
QToolButton:hover { background: #eef3fb; border-color: #d6e2f3; }
QToolButton:pressed { background: @accentSoft; }
QToolButton:checked { background: @accentSoft; border-color: #b9d3fb; }
QToolButton:disabled { color: #a3adb8; }
QToolButton[popupMode="1"] { padding-right: 14px; }
QToolButton#runButton { background: @accent; color: white; border: 1px solid @accent; padding: 4px 12px; font-weight: 600; }
QToolButton#runButton:hover { background: @accentHover; border-color: @accentHover; }
QToolButton#runButton:disabled { background: #b7c6dc; border-color: #b7c6dc; color: #f4f6f9; }

QDockWidget { color: @text; font-weight: 600; titlebar-close-icon: none; }
QDockWidget::title { background: @surfaceAlt; border-bottom: 1px solid @border; padding: 7px 10px; text-align: left; }
QDockWidget > QWidget { background: @surface; }

QTabWidget::pane { border: none; background: @surface; }
QTabBar { background: transparent; }
QTabBar::tab { background: transparent; color: @textMuted; padding: 7px 14px; border: none; border-bottom: 2px solid transparent; margin-right: 2px; }
QTabBar::tab:hover { color: @text; background: #eef2f7; }
QTabBar::tab:selected { color: @accent; border-bottom: 2px solid @accent; font-weight: 600; }
QTabBar::close-button { subcontrol-position: right; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit, QDateTimeEdit {
    background: @surface; border: 1px solid @borderStrong; border-radius: 5px; padding: 4px 6px; color: @text;
    selection-background-color: @accent; selection-color: white; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QComboBox { padding-right: 18px; min-height: 18px; }
QComboBox::drop-down { border: none; width: 22px; subcontrol-origin: padding; subcontrol-position: center right; }
QComboBox::down-arrow { image: url(@chevronDown); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: none; }
QAbstractSpinBox { padding-right: 20px; }
QAbstractSpinBox::up-button, QAbstractSpinBox::down-button { border: none; width: 18px; subcontrol-origin: border; }
QAbstractSpinBox::up-button { subcontrol-position: top right; }
QAbstractSpinBox::down-button { subcontrol-position: bottom right; }
QAbstractSpinBox::up-arrow { image: url(@chevronUp); width: 10px; height: 10px; }
QAbstractSpinBox::down-arrow { image: url(@chevronDown); width: 10px; height: 10px; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @borderStrong; selection-background-color: @accentSoft; selection-color: @text; outline: none; }

QPushButton { background: @surface; border: 1px solid @borderStrong; border-radius: 6px; padding: 6px 14px; color: @text; }
QPushButton:hover { background: #f1f5fb; border-color: #b6c4d6; }
QPushButton:pressed { background: @accentSoft; }
QPushButton:default, QPushButton[primary="true"] { background: @accent; border-color: @accent; color: white; font-weight: 600; }
QPushButton:default:hover, QPushButton[primary="true"]:hover { background: @accentHover; }
QPushButton:disabled { color: #a3adb8; background: #f3f5f8; border-color: @border; }

QTreeView, QListView, QTableView, QTreeWidget, QListWidget, QTableWidget {
    background: @surface; alternate-background-color: @surfaceAlt; border: none; color: @text;
    selection-background-color: @accentSoft; selection-color: @text; outline: none; gridline-color: #edf1f5; }
QTreeView::item, QListView::item { padding: 3px 2px; }
QTreeView::item:hover, QListView::item:hover { background: #f1f5fb; }
QTreeView::item:selected, QListView::item:selected, QTableView::item:selected { background: @accentSoft; color: @text; }
QHeaderView::section { background: @surfaceAlt; color: @textMuted; border: none; border-bottom: 1px solid @border; border-right: 1px solid #edf1f5; padding: 5px 6px; font-weight: 600; }

QScrollBar:vertical { background: transparent; width: 11px; margin: 0; }
QScrollBar::handle:vertical { background: #c9d2dd; border-radius: 4px; min-height: 28px; margin: 2px; }
QScrollBar::handle:vertical:hover { background: #a9b6c6; }
QScrollBar:horizontal { background: transparent; height: 11px; margin: 0; }
QScrollBar::handle:horizontal { background: #c9d2dd; border-radius: 4px; min-width: 28px; margin: 2px; }
QScrollBar::handle:horizontal:hover { background: #a9b6c6; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }

QStatusBar { background: @surface; border-top: 1px solid @border; color: @textMuted; }
QStatusBar::item { border: none; }
QStatusBar QLabel { color: @textMuted; padding: 0 8px; }

QProgressBar { background: #e8edf3; border: none; border-radius: 4px; text-align: center; color: @text; min-height: 14px; }
QProgressBar::chunk { background: @accent; border-radius: 4px; }

QSlider::groove:horizontal { height: 4px; background: #dbe2ea; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @surface; border: 2px solid @accent; width: 12px; height: 12px; margin: -6px 0; border-radius: 8px; }

QGroupBox { border: 1px solid @border; border-radius: 8px; margin-top: 14px; padding: 10px 8px 8px 8px; background: @surface; }
QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; color: @textMuted; font-weight: 600; }

QToolTip { background: #1f2a37; color: white; border: none; padding: 6px 8px; border-radius: 4px; }
QCheckBox, QRadioButton, QLabel { color: @text; }
QSplitter::handle { background: @border; }

QFrame#card { background: @surface; border: 1px solid @border; border-radius: 10px; }
QLabel#muted { color: @textMuted; }
QLabel#heading { font-size: 15pt; font-weight: 600; color: @text; }
QLabel#subheading { color: @textMuted; font-size: 10pt; }
QWidget#mapBar { background: @surface; border-bottom: 1px solid @border; }
QWidget#mapBar QLabel { color: @textMuted; }
QWidget#panelHeader { background: @surface; border-bottom: 1px solid @border; }
QLabel#panelTitle { font-size: 11pt; font-weight: 600; color: @text; }
QLabel#panelSubtitle { color: @textMuted; }
)";
    const std::pair<const char*, QColor> tokens[] = {
        {"@window", kWindow},           {"@surfaceAlt", kSurfaceAlt}, {"@surface", kSurface},
        {"@borderStrong", kBorderStrong}, {"@border", kBorder},       {"@textMuted", kTextMuted},
        {"@text", kText},               {"@accentHover", kAccentHover}, {"@accentSoft", kAccentSoft},
        {"@accent", kAccent}};
    for (const auto& [name, color] : tokens)
        s.replace(QString::fromLatin1(name), hex(color));
    s.replace("@chevronDown", chevron(false));
    s.replace("@chevronUp", chevron(true));
    return s;
}

void apply(QApplication& app)
{
    // Always light, also when the operating system uses a dark scheme.
    app.styleHints()->setColorScheme(Qt::ColorScheme::Light);
    if (QStyle* fusion = QStyleFactory::create("Fusion"))
        QApplication::setStyle(fusion);

    QPalette p;
    p.setColor(QPalette::Window, kWindow);
    p.setColor(QPalette::WindowText, kText);
    p.setColor(QPalette::Base, kSurface);
    p.setColor(QPalette::AlternateBase, kSurfaceAlt);
    p.setColor(QPalette::Text, kText);
    p.setColor(QPalette::PlaceholderText, QColor(140, 150, 162));
    p.setColor(QPalette::Button, kSurface);
    p.setColor(QPalette::ButtonText, kText);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, kAccent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, kText);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::Link, kAccent);
    p.setColor(QPalette::Light, Qt::white);
    p.setColor(QPalette::Midlight, QColor(235, 239, 244));
    p.setColor(QPalette::Mid, kBorderStrong);
    p.setColor(QPalette::Dark, QColor(160, 170, 182));
    p.setColor(QPalette::Shadow, QColor(120, 130, 142));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(160, 170, 182));
    QApplication::setPalette(p);

    QFont f = QApplication::font();
#if defined(Q_OS_WIN)
    f.setFamily("Segoe UI");
#endif
    if (f.pointSizeF() < 9.5)
        f.setPointSizeF(9.5);
    QApplication::setFont(f);
    app.setStyleSheet(styleSheet());
}

} // namespace anahita::app::theme
