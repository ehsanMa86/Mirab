/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/ScenarioPanel.cpp
 * Purpose: Scenarios dock: the scenarios of a project, their changes, and runs of several scenarios.
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
#include "ScenarioPanel.h"

#include "Icons.h"
#include "SolverProcess.h"
#include "Theme.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QTableWidget>
#include <QThread>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>

namespace mirab::app
{

namespace
{
constexpr int NameRole = Qt::UserRole + 1;
constexpr int BaseRole = Qt::UserRole + 2;

QString qs(const std::string& s)
{
    return QString::fromStdString(s);
}

QToolButton* toolButton(const QString& icon, const QString& tip, QWidget* parent)
{
    auto* b = new QToolButton(parent);
    b->setIcon(icons::get(icon));
    b->setIconSize(QSize(18, 18));
    b->setToolTip(tip);
    b->setAutoRaise(true);
    return b;
}

QString joinTokens(const std::vector<std::string>& t)
{
    return qs(text::join(t, " "));
}

/// Name of the value a change touches, from the section's columns.
QString propertyName(core::Dialect dialect, const core::ScenarioChange& c)
{
    using K = core::ScenarioChange::Kind;
    switch (c.kind)
    {
    case K::Add:
        return QObject::tr("New row");
    case K::Remove:
        return QObject::tr("Removed row");
    case K::Text:
        return QObject::tr("Text");
    case K::Row:
        return QObject::tr("Row");
    case K::Set:
        break;
    }
    const core::SectionDef* def = core::findSectionDef(dialect, c.section);
    if (def && c.column >= 0 && static_cast<std::size_t>(c.column) < def->columns.size())
        return qs(def->columns[static_cast<std::size_t>(c.column)].name);
    if (def && def->columns.empty())
        return QObject::tr("Value");
    return QObject::tr("Value %1").arg(c.column + 1);
}

QString changeValues(const core::ScenarioChange& c)
{
    using K = core::ScenarioChange::Kind;
    switch (c.kind)
    {
    case K::Set:
        return qs(c.from.empty() ? std::string() : c.from.front()) + "  →  " +
               qs(c.to.empty() ? std::string() : c.to.front());
    case K::Row:
        return joinTokens(c.from) + "  →  " + joinTokens(c.to);
    case K::Add:
        return joinTokens(c.to);
    case K::Remove:
        return joinTokens(c.from);
    case K::Text:
        return c.to.empty() ? QString() : qs(c.to.front());
    }
    return {};
}

/// Scenarios that must not become the parent of @p name: itself and those based on it.
QStringList descendants(const std::vector<core::Scenario>& list, const QString& name)
{
    QStringList out;
    if (name.isEmpty())
        return out;
    out << name;
    bool grew = true;
    while (grew)
    {
        grew = false;
        for (const auto& s : list)
        {
            const QString n = qs(s.name);
            if (!out.contains(n) && out.contains(qs(s.parent)))
            {
                out << n;
                grew = true;
            }
        }
    }
    return out;
}
} // namespace

// ---------------------------------------------------------------------------
// Dock
// ---------------------------------------------------------------------------
ScenarioPanel::ScenarioPanel(ProjectDocument* doc, const engine::EngineRegistry* registry, QWidget* parent)
    : QWidget(parent), doc_(doc), registry_(registry)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* head = new QWidget(this);
    head->setObjectName("panelHeader");
    head->setAttribute(Qt::WA_StyledBackground, true);
    auto* hl = new QVBoxLayout(head);
    hl->setContentsMargins(10, 8, 10, 8);
    hl->setSpacing(4);
    auto* title = new QLabel(tr("Scenarios"), head);
    title->setObjectName("panelTitle");
    auto* subtitle = new QLabel(tr("Alternatives of the base model. Edits go to the active scenario."), head);
    subtitle->setObjectName("panelSubtitle");
    subtitle->setWordWrap(true);
    hl->addWidget(title);
    hl->addWidget(subtitle);
    auto* buttons = new QHBoxLayout;
    buttons->setSpacing(2);
    newButton_ = toolButton("new", tr("New scenario based on the selected one"), head);
    duplicateButton_ = toolButton("layers", tr("Duplicate the selected scenario"), head);
    editButton_ = toolButton("properties", tr("Name, description, parent and engine of the selected scenario"), head);
    deleteButton_ = toolButton("delete", tr("Delete the selected scenario"), head);
    auto* runButton = toolButton("run", tr("Run several scenarios"), head);
    auto* compareButton = toolButton("compare", tr("Compare the results of scenarios"), head);
    for (QToolButton* b : {newButton_, duplicateButton_, editButton_, deleteButton_})
        buttons->addWidget(b);
    buttons->addStretch(1);
    buttons->addWidget(runButton);
    buttons->addWidget(compareButton);
    hl->addLayout(buttons);
    layout->addWidget(head);

    tree_ = new QTreeWidget(this);
    tree_->setColumnCount(2);
    tree_->setHeaderLabels({tr("Scenario"), tr("Changes")});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->setIndentation(14);
    tree_->header()->setStretchLastSection(false);
    tree_->setRootIsDecorated(true);
    tree_->setUniformRowHeights(true);
    tree_->setContextMenuPolicy(Qt::ActionsContextMenu);
    layout->addWidget(tree_, 2);

    auto* bar = new QWidget(this);
    auto* bl = new QHBoxLayout(bar);
    bl->setContentsMargins(8, 6, 8, 6);
    activateButton_ = new QPushButton(icons::get("play"), tr("Activate"), bar);
    activateButton_->setToolTip(tr("Show and edit the selected scenario (double-click also activates)"));
    bl->addWidget(activateButton_);
    bl->addStretch(1);
    layout->addWidget(bar);

    auto* changesHead = new QWidget(this);
    changesHead->setObjectName("panelHeader");
    changesHead->setAttribute(Qt::WA_StyledBackground, true);
    auto* ch = new QVBoxLayout(changesHead);
    ch->setContentsMargins(10, 6, 10, 6);
    ch->setSpacing(2);
    changesTitle_ = new QLabel(changesHead);
    changesTitle_->setObjectName("panelTitle");
    inherited_ = new QLabel(changesHead);
    inherited_->setObjectName("panelSubtitle");
    inherited_->setWordWrap(true);
    ch->addWidget(changesTitle_);
    ch->addWidget(inherited_);
    layout->addWidget(changesHead);

    changes_ = new QTreeWidget(this);
    changes_->setColumnCount(3);
    changes_->setHeaderLabels({tr("Item"), tr("Property"), tr("Parent  →  scenario")});
    changes_->header()->setSectionResizeMode(QHeaderView::Interactive);
    changes_->header()->setStretchLastSection(true);
    changes_->setColumnWidth(0, 70);
    changes_->setColumnWidth(1, 80);
    changes_->setRootIsDecorated(false);
    changes_->setUniformRowHeights(true);
    changes_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    changes_->setAlternatingRowColors(true);
    layout->addWidget(changes_, 3);
    auto* changesBar = new QWidget(this);
    auto* cbl = new QHBoxLayout(changesBar);
    cbl->setContentsMargins(8, 6, 8, 6);
    revertButton_ = new QPushButton(icons::get("undo"), tr("Revert"), changesBar);
    revertButton_->setToolTip(tr("Remove the selected changes from the scenario"));
    cbl->addStretch(1);
    cbl->addWidget(revertButton_);
    layout->addWidget(changesBar);

    auto addMenuAction = [this](const QString& icon, const QString& text, const std::function<void()>& fn) {
        auto* a = new QAction(icons::get(icon), text, tree_);
        connect(a, &QAction::triggered, this, fn);
        tree_->addAction(a);
    };
    addMenuAction("play", tr("Activate"), [this] { activate(selected()); });
    addMenuAction("new", tr("New scenario based on this..."), [this] { newScenario(); });
    addMenuAction("layers", tr("Duplicate..."), [this] { duplicateScenario(); });
    addMenuAction("properties", tr("Properties..."), [this] { editScenario(); });
    addMenuAction("delete", tr("Delete"), [this] { deleteScenario(); });

    connect(newButton_, &QToolButton::clicked, this, &ScenarioPanel::newScenario);
    connect(duplicateButton_, &QToolButton::clicked, this, &ScenarioPanel::duplicateScenario);
    connect(editButton_, &QToolButton::clicked, this, &ScenarioPanel::editScenario);
    connect(deleteButton_, &QToolButton::clicked, this, &ScenarioPanel::deleteScenario);
    connect(runButton, &QToolButton::clicked, this, &ScenarioPanel::runRequested);
    connect(compareButton, &QToolButton::clicked, this, &ScenarioPanel::compareRequested);
    connect(activateButton_, &QPushButton::clicked, this, [this] { activate(selected()); });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this] { activate(selected()); });
    connect(tree_, &QTreeWidget::itemSelectionChanged, this, &ScenarioPanel::updateButtons);
    connect(changes_, &QTreeWidget::itemSelectionChanged, this, &ScenarioPanel::updateButtons);
    connect(changes_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item) {
        // Select the element the change belongs to.
        const QString key = item->data(0, NameRole).toString();
        const auto& m = doc_->model();
        const std::string id = key.toStdString();
        if (m.findNode(id))
            doc_->select({core::Category::Node, key});
        else if (m.findLink(id))
            doc_->select({core::Category::Link, key});
        else if (m.findArea(id))
            doc_->select({core::Category::Area, key});
        else if (m.findGage(id))
            doc_->select({core::Category::Gage, key});
    });
    connect(revertButton_, &QPushButton::clicked, this, [this] {
        std::vector<int> indices;
        for (QTreeWidgetItem* item : changes_->selectedItems())
            indices.push_back(item->data(0, BaseRole).toInt());
        doc_->revertChanges(indices);
    });
    connect(doc_, &ProjectDocument::scenariosChanged, this, &ScenarioPanel::rebuild);
    connect(doc_, &ProjectDocument::activeScenarioChanged, this, [this] {
        tree_->clearSelection(); // the active scenario becomes the selected one
        rebuild();
    });
    connect(doc_, &ProjectDocument::resultsChanged, this, &ScenarioPanel::rebuild);
    connect(doc_, &ProjectDocument::modelChanged, this, &ScenarioPanel::rebuildChanges);
    rebuild();
}

QString ScenarioPanel::selected() const
{
    const auto items = tree_->selectedItems();
    return items.isEmpty() ? QString() : items.front()->data(0, NameRole).toString();
}

void ScenarioPanel::rebuild()
{
    const QString keep = tree_->selectedItems().isEmpty() ? doc_->activeScenario() : selected();
    tree_->clear();
    const QString active = doc_->activeScenario();
    QFont bold = tree_->font();
    bold.setBold(true);
    auto decorate = [&](QTreeWidgetItem* item, const QString& name) {
        item->setData(0, NameRole, name);
        const bool isActive = name == active;
        if (isActive)
        {
            for (int c = 0; c < 2; ++c)
                item->setFont(c, bold);
            item->setForeground(0, theme::kAccent);
            item->setIcon(0, icons::get("play"));
            item->setToolTip(0, tr("Active: shown on the map and edited") + "\n" + item->toolTip(0));
        }
        // A chart icon marks scenarios with results.
        const bool hasResults = (isActive && doc_->hasResults()) || QFileInfo::exists(doc_->resultsPath(name));
        if (hasResults)
            item->setIcon(1, icons::get("chart"));
        item->setToolTip(1, hasResults ? tr("Results: %1").arg(doc_->resultsPath(name)) : tr("Not run yet"));
        if (name == keep)
            item->setSelected(true);
    };
    auto* base = new QTreeWidgetItem(tree_, {tr("Base model")});
    base->setIcon(0, icons::get("project"));
    base->setToolTip(0, tr("The input file. Scenarios record their differences from it."));
    decorate(base, {});
    std::function<void(QTreeWidgetItem*, const std::string&)> addChildren = [&](QTreeWidgetItem* parent,
                                                                               const std::string& parentName) {
        for (const auto& s : doc_->scenarios())
        {
            if (s.parent != parentName)
                continue;
            auto* item = new QTreeWidgetItem(parent, {qs(s.name), QString::number(s.changes.size())});
            item->setIcon(0, icons::get("scenarios"));
            QString tip = s.description.empty() ? qs(s.name) : qs(s.description);
            if (!s.engineId.empty())
                tip += "\n" + tr("Engine: %1").arg(qs(s.engineId));
            item->setToolTip(0, tip);
            decorate(item, qs(s.name));
            addChildren(item, s.name);
        }
    };
    addChildren(base, {});
    // Scenarios whose parent is missing are listed at the top level.
    for (const auto& s : doc_->scenarios())
    {
        if (!s.parent.empty() && !core::findScenario(doc_->scenarios(), s.parent))
        {
            auto* item = new QTreeWidgetItem(tree_, {qs(s.name), QString::number(s.changes.size())});
            item->setIcon(0, icons::get("warning"));
            item->setToolTip(0, tr("Based on \"%1\", which does not exist").arg(qs(s.parent)));
            decorate(item, qs(s.name));
        }
    }
    tree_->expandAll();
    rebuildChanges();
    updateButtons();
}

void ScenarioPanel::rebuildChanges()
{
    changes_->clear();
    const QString active = doc_->activeScenario();
    const core::Scenario* s = core::findScenario(doc_->scenarios(), active.toStdString());
    if (!s)
    {
        changesTitle_->setText(tr("Base model"));
        inherited_->setText(doc_->scenarios().empty()
                                ? tr("Create a scenario to try an alternative without changing the base model.")
                                : tr("Edits change the base model. Activate a scenario to record its changes instead."));
        updateButtons();
        return;
    }
    changesTitle_->setText(tr("Changes in %1").arg(active));
    QString note = s->parent.empty() ? tr("Based on the base model.")
                                     : tr("Based on %1, whose changes it inherits.").arg(qs(s->parent));
    if (!doc_->conflicts().empty())
        note += " " + tr("%n change(s) no longer apply cleanly to the base model.", nullptr,
                         static_cast<int>(doc_->conflicts().size()));
    inherited_->setText(note);
    const core::Dialect dialect = doc_->model().dialect();
    for (std::size_t i = 0; i < s->changes.size(); ++i)
    {
        const auto& c = s->changes[i];
        QString item = c.kind == core::ScenarioChange::Kind::Text ? tr("Title") : qs(c.key);
        if (c.ordinal > 0)
            item += QString(" #%1").arg(c.ordinal + 1);
        auto* row = new QTreeWidgetItem(changes_, {item, propertyName(dialect, c), changeValues(c)});
        row->setData(0, NameRole, qs(c.key));
        row->setData(0, BaseRole, static_cast<int>(i));
        const core::SectionDef* def = core::findSectionDef(dialect, c.section);
        const QString where = "[" + qs(c.section) + "]" + (def && !def->description.empty() ? " " + qs(def->description) : QString());
        row->setToolTip(0, where);
        row->setToolTip(1, where);
        row->setToolTip(2, changeValues(c));
        for (const auto& k : doc_->conflicts())
        {
            if (qs(k.scenario) == active && k.section == c.section && k.key == c.key && k.ordinal == c.ordinal &&
                (c.kind != core::ScenarioChange::Kind::Set || k.column == c.column))
            {
                row->setIcon(0, icons::get("warning"));
                row->setToolTip(0, qs(k.message));
                row->setForeground(2, theme::kWarning);
            }
        }
    }
    updateButtons();
}

void ScenarioPanel::updateButtons()
{
    const bool any = !tree_->selectedItems().isEmpty();
    const QString name = selected();
    const bool scenario = any && !name.isEmpty();
    duplicateButton_->setEnabled(scenario);
    editButton_->setEnabled(scenario);
    deleteButton_->setEnabled(scenario);
    activateButton_->setEnabled(any && name != doc_->activeScenario());
    activateButton_->setText(any && name.isEmpty() ? tr("Edit the base model") : tr("Activate"));
    revertButton_->setEnabled(doc_->isScenarioActive() && !changes_->selectedItems().isEmpty());
}

void ScenarioPanel::activate(const QString& name)
{
    QString error;
    if (!doc_->setActiveScenario(name, &error))
        QMessageBox::warning(this, tr("Scenarios"), error);
    else
        emit statusMessage(name.isEmpty() ? tr("The base model is active; edits change the input file.")
                                          : tr("Scenario %1 is active; edits are recorded in it.").arg(name));
}

void ScenarioPanel::newScenario()
{
    core::Scenario s;
    s.parent = selected().toStdString();
    int n = static_cast<int>(doc_->scenarios().size()) + 1;
    do
    {
        s.name = tr("Scenario %1").arg(n++).toStdString();
    } while (core::findScenario(doc_->scenarios(), s.name));
    ScenarioDialog dlg(doc_, registry_, s, {}, this);
    dlg.setWindowTitle(tr("New scenario"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString error;
    const core::Scenario made = dlg.scenario();
    if (!doc_->addScenario(made, &error))
    {
        QMessageBox::warning(this, tr("New scenario"), error);
        return;
    }
    activate(qs(made.name));
}

void ScenarioPanel::duplicateScenario()
{
    const core::Scenario* s = core::findScenario(doc_->scenarios(), selected().toStdString());
    if (!s)
        return;
    core::Scenario copy = *s;
    int n = 2;
    do
    {
        copy.name = s->name + " " + std::to_string(n++);
    } while (!core::checkScenarioName(doc_->scenarios(), copy.name).empty() && n < 1000);
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Duplicate scenario"), tr("Name of the copy:"),
                                               QLineEdit::Normal, qs(copy.name), &ok);
    if (!ok)
        return;
    copy.name = name.trimmed().toStdString();
    QString error;
    if (!doc_->addScenario(copy, &error))
        QMessageBox::warning(this, tr("Duplicate scenario"), error);
}

void ScenarioPanel::editScenario()
{
    const QString name = selected();
    const core::Scenario* s = core::findScenario(doc_->scenarios(), name.toStdString());
    if (!s)
        return;
    ScenarioDialog dlg(doc_, registry_, *s, name, this);
    dlg.setWindowTitle(tr("Scenario properties"));
    if (dlg.exec() != QDialog::Accepted)
        return;
    const core::Scenario edited = dlg.scenario();
    QString error;
    if (!doc_->renameScenario(name, qs(edited.name), &error) ||
        !doc_->updateScenario(qs(edited.name), edited, &error))
        QMessageBox::warning(this, tr("Scenario properties"), error);
}

void ScenarioPanel::deleteScenario()
{
    const QString name = selected();
    const core::Scenario* s = core::findScenario(doc_->scenarios(), name.toStdString());
    if (!s)
        return;
    bool hasChildren = false;
    for (const auto& x : doc_->scenarios())
        hasChildren = hasChildren || x.parent == s->name;
    QString text = tr("Delete the scenario \"%1\" and its %n change(s)?", nullptr, static_cast<int>(s->changes.size()))
                       .arg(name);
    if (hasChildren)
        text += "\n\n" + tr("Scenarios based on it keep their models: they take over its changes.");
    if (QMessageBox::question(this, tr("Delete scenario"), text) != QMessageBox::Yes)
        return;
    doc_->removeScenario(name);
}

// ---------------------------------------------------------------------------
// Properties dialog
// ---------------------------------------------------------------------------
ScenarioDialog::ScenarioDialog(const ProjectDocument* doc, const engine::EngineRegistry* registry,
                               const core::Scenario& scenario, const QString& original, QWidget* parent)
    : QDialog(parent), doc_(doc), original_(original), scenario_(scenario)
{
    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;
    name_ = new QLineEdit(qs(scenario.name), this);
    description_ = new QPlainTextEdit(qs(scenario.description), this);
    description_->setPlaceholderText(tr("What this alternative tries, for example \"New 300 mm main along First Street\""));
    description_->setFixedHeight(70);
    parent_ = new QComboBox(this);
    parent_->addItem(icons::get("project"), tr("Base model"), QString());
    const QStringList excluded = descendants(doc->scenarios(), original);
    for (const auto& s : doc->scenarios())
    {
        if (!excluded.contains(qs(s.name)))
            parent_->addItem(icons::get("scenarios"), qs(s.name), qs(s.name));
    }
    parent_->setCurrentIndex(std::max(0, parent_->findData(qs(scenario.parent))));
    parent_->setToolTip(tr("The scenario inherits the changes of the one it is based on"));
    engine_ = new QComboBox(this);
    engine_->addItem(tr("Same as the parent"), QString());
    const core::Dialect d = doc->model().dialect();
    for (const auto* e : registry->forDialect(d, false))
        engine_->addItem(icons::get("engine"), qs(e->name), qs(e->id));
    engine_->setCurrentIndex(std::max(0, engine_->findData(qs(scenario.engineId))));
    form->addRow(tr("Name"), name_);
    form->addRow(tr("Description"), description_);
    form->addRow(tr("Based on"), parent_);
    form->addRow(tr("Engine"), engine_);
    if (d == core::Dialect::Epanet)
    {
        msx_ = new QComboBox(this);
        msx_->addItem(tr("Same as the parent"), -1);
        msx_->addItem(tr("Off"), 0);
        msx_->addItem(tr("On"), 1);
        msx_->setCurrentIndex(std::max(0, msx_->findData(scenario.useMsx)));
        msxFile_ = new QLineEdit(qs(scenario.msxFile), this);
        msxFile_->setPlaceholderText(tr("Same as the parent"));
        auto* browse = new QPushButton(tr("Browse..."), this);
        auto* row = new QHBoxLayout;
        row->addWidget(msxFile_, 1);
        row->addWidget(browse);
        form->addRow(tr("EPANET-MSX"), msx_);
        form->addRow(tr("MSX file"), row);
        connect(browse, &QPushButton::clicked, this, [this] {
            const QString dir = doc_->path().isEmpty() ? QDir::homePath() : QFileInfo(doc_->path()).absolutePath();
            const QString f = QFileDialog::getOpenFileName(this, tr("MSX file"), dir, tr("EPANET-MSX files (*.msx);;All files (*)"));
            if (!f.isEmpty())
                msxFile_->setText(doc_->path().isEmpty() ? f : QDir(dir).relativeFilePath(f));
        });
    }
    layout->addLayout(form);
    error_ = new QLabel(this);
    error_->setStyleSheet(QString("color: %1;").arg(theme::kDanger.name()));
    error_->setWordWrap(true);
    error_->setVisible(false);
    layout->addWidget(error_);
    auto* box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    layout->addWidget(box);
    connect(box, &QDialogButtonBox::accepted, this, &ScenarioDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &ScenarioDialog::reject);
    resize(460, sizeHint().height());
    name_->selectAll();
}

core::Scenario ScenarioDialog::scenario() const
{
    core::Scenario s = scenario_;
    s.name = name_->text().trimmed().toStdString();
    s.description = description_->toPlainText().trimmed().toStdString();
    s.parent = parent_->currentData().toString().toStdString();
    s.engineId = engine_->currentData().toString().toStdString();
    if (msx_)
    {
        s.useMsx = msx_->currentData().toInt();
        s.msxFile = msxFile_->text().trimmed().toStdString();
    }
    return s;
}

void ScenarioDialog::accept()
{
    const std::string name = name_->text().trimmed().toStdString();
    std::vector<core::Scenario> others;
    for (const auto& s : doc_->scenarios())
    {
        if (qs(s.name) != original_)
            others.push_back(s);
    }
    const std::string why = core::checkScenarioName(others, name);
    if (!why.empty())
    {
        error_->setText(tr("This name cannot be used: %1.").arg(qs(why)));
        error_->setVisible(true);
        return;
    }
    QDialog::accept();
}

// ---------------------------------------------------------------------------
// Running several scenarios
// ---------------------------------------------------------------------------
ScenarioRunDialog::ScenarioRunDialog(ProjectDocument* doc, const engine::EngineRegistry* registry, QWidget* parent)
    : QDialog(parent), doc_(doc), registry_(registry)
{
    setWindowTitle(tr("Run scenarios"));
    auto* layout = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Each scenario runs in its own solver process. Results are saved next to the project "
                                "as <project>.<scenario>.anr."),
                             this);
    intro->setWordWrap(true);
    layout->addWidget(intro);
    table_ = new QTableWidget(this);
    table_->setColumnCount(3);
    table_->setHorizontalHeaderLabels({tr("Scenario"), tr("Engine"), tr("Progress")});
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
    table_->setColumnWidth(2, 230);
    table_->verticalHeader()->hide();
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setShowGrid(false);
    QStringList names = {QString()};
    for (const auto& s : doc->scenarios())
        names << qs(s.name);
    table_->setRowCount(static_cast<int>(names.size()));
    for (int r = 0; r < names.size(); ++r)
    {
        const QString& n = names[r];
        auto* item = new QTableWidgetItem(icons::get(n.isEmpty() ? "project" : "scenarios"),
                                          n.isEmpty() ? tr("Base model") : n);
        item->setData(NameRole, n);
        item->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        table_->setItem(r, 0, item);
        const auto choice = doc->engineChoice(n);
        std::string id = choice.engineId;
        if (id.empty() || !registry->descriptor(id))
            id = engine::EngineRegistry::defaultEngineId(doc->model().dialect());
        const auto* desc = registry->descriptor(id);
        QString engine = desc ? qs(desc->name) : qs(id);
        if (choice.useMsx && doc->model().dialect() == core::Dialect::Epanet)
            engine += " + MSX";
        auto* e = new QTableWidgetItem(engine);
        e->setFlags(Qt::ItemIsEnabled);
        table_->setItem(r, 1, e);
        auto* bar = new QProgressBar(table_);
        bar->setRange(0, 100);
        bar->setValue(0);
        bar->setFormat(QFileInfo::exists(doc->resultsPath(n)) ? tr("Has results") : tr("Not run"));
        bar->setTextVisible(true);
        table_->setCellWidget(r, 2, bar);
    }
    layout->addWidget(table_, 1);
    auto* row = new QHBoxLayout;
    row->addWidget(new QLabel(tr("Simultaneous runs"), this));
    jobs_ = new QSpinBox(this);
    const int cores = std::max(1, QThread::idealThreadCount());
    jobs_->setRange(1, std::max(1, cores));
    jobs_->setValue(std::clamp(QSettings().value("scenarios/jobs", std::min(4, cores)).toInt(), 1, std::max(1, cores)));
    jobs_->setToolTip(tr("How many solver processes run at the same time"));
    row->addWidget(jobs_);
    row->addStretch(1);
    summary_ = new QLabel(this);
    row->addWidget(summary_);
    layout->addLayout(row);
    auto* box = new QDialogButtonBox(this);
    runButton_ = box->addButton(tr("Run"), QDialogButtonBox::AcceptRole);
    runButton_->setIcon(icons::get("run"));
    stopButton_ = box->addButton(tr("Stop"), QDialogButtonBox::ActionRole);
    stopButton_->setIcon(icons::get("stop"));
    stopButton_->setEnabled(false);
    QPushButton* close = box->addButton(QDialogButtonBox::Close);
    layout->addWidget(box);
    connect(runButton_, &QPushButton::clicked, this, &ScenarioRunDialog::start);
    connect(stopButton_, &QPushButton::clicked, this, &ScenarioRunDialog::stop);
    connect(close, &QPushButton::clicked, this, &ScenarioRunDialog::reject);
    resize(620, 380);
}

void ScenarioRunDialog::select(const QStringList& names)
{
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        QTableWidgetItem* item = table_->item(r, 0);
        item->setCheckState(names.contains(item->data(NameRole).toString()) ? Qt::Checked : Qt::Unchecked);
    }
}

bool ScenarioRunDialog::isRunning() const
{
    for (const auto& j : queue_)
    {
        if (j.queued || (j.process && j.process->isRunning()))
            return true;
    }
    return false;
}

void ScenarioRunDialog::setStatus(int row, const QString& text, int progress)
{
    if (auto* bar = qobject_cast<QProgressBar*>(table_->cellWidget(row, 2)))
    {
        if (progress >= 0)
            bar->setValue(progress);
        bar->setFormat(text);
    }
}

void ScenarioRunDialog::start()
{
    if (isRunning())
        return;
    for (auto& j : queue_)
        delete j.process;
    queue_.clear();
    finished_ = failed_ = 0;
    QSettings().setValue("scenarios/jobs", jobs_->value());
    for (int r = 0; r < table_->rowCount(); ++r)
    {
        QTableWidgetItem* item = table_->item(r, 0);
        if (item->checkState() != Qt::Checked)
            continue;
        Job j;
        j.name = item->data(NameRole).toString();
        j.row = r;
        j.queued = true;
        queue_.push_back(j);
        setStatus(r, tr("Waiting"), 0);
    }
    if (queue_.empty())
    {
        summary_->setText(tr("Check the scenarios to run."));
        return;
    }
    runButton_->setEnabled(false);
    stopButton_->setEnabled(true);
    jobs_->setEnabled(false);
    summary_->setText(tr("Running %n scenario(s)...", nullptr, static_cast<int>(queue_.size())));
    startNext();
}

bool ScenarioRunDialog::launch(Job& job, QString* error)
{
    const core::Dialect dialect = doc_->model().dialect();
    const auto choice = doc_->engineChoice(job.name);
    std::string id = choice.engineId;
    if (id.empty() || !registry_->descriptor(id))
        id = engine::EngineRegistry::defaultEngineId(dialect);
    QString msx;
    if (choice.useMsx && dialect == core::Dialect::Epanet)
    {
        if (const auto* v = registry_->msxVariantOf(id))
            id = v->id;
        const QFileInfo fi(qs(choice.msxFile));
        msx = choice.msxFile.empty() ? QString()
              : fi.isAbsolute() || doc_->path().isEmpty()
                  ? fi.absoluteFilePath()
                  : QFileInfo(doc_->path()).dir().filePath(qs(choice.msxFile));
        if (msx.isEmpty())
        {
            *error = tr("Choose an MSX file for this scenario");
            return false;
        }
    }
    std::string why;
    if (!registry_->isAvailable(id, &why))
    {
        *error = qs(why);
        return false;
    }
    // The scenario's model is written next to the project, so that relative
    // file references keep working.
    const QString base = doc_->runBase(job.name);
    job.input = QFileInfo(base).dir().filePath("." + QFileInfo(base).fileName() + ".mirab-run.inp");
    QFile f(job.input);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        *error = tr("Cannot write %1").arg(job.input);
        return false;
    }
    f.write(QByteArray::fromStdString(doc_->scenarioModel(job.name).toText()));
    f.close();
    QStringList args = {"run", job.input, "--engine", qs(id), "--out", base + ".anr", "--report", base + ".rpt"};
    if (!msx.isEmpty())
        args << "--msx" << msx;
    job.process = new SolverProcess(this);
    const int row = job.row;
    const QString name = job.name;
    const QString input = job.input;
    connect(job.process, &SolverProcess::progress, this, [this, row](double fraction, const QString& stage) {
        setStatus(row, stage + "  " + QString::number(static_cast<int>(fraction * 100)) + "%",
                  static_cast<int>(fraction * 100));
    });
    connect(job.process, &SolverProcess::finished, this, [this, row, name, input](bool ok, const QJsonObject& r) {
        QFile::remove(input);
        ++finished_;
        if (ok)
        {
            setStatus(row, tr("Finished in %1 s").arg(r.value("seconds").toDouble(), 0, 'f', 2), 100);
            doc_->reloadResults(name);
        }
        else
        {
            ++failed_;
            const QString message = r.value("cancelled").toBool() ? tr("Stopped") : r.value("message").toString();
            setStatus(row, message.isEmpty() ? tr("Failed") : message, 0);
            if (auto* bar = qobject_cast<QProgressBar*>(table_->cellWidget(row, 2)))
                bar->setToolTip(message);
        }
        emit scenarioFinished(name, ok);
        startNext();
    });
    return job.process->start(args);
}

void ScenarioRunDialog::startNext()
{
    int running = 0;
    for (const auto& j : queue_)
        running += j.process && j.process->isRunning() ? 1 : 0;
    for (auto& j : queue_)
    {
        if (running >= jobs_->value())
            break;
        if (!j.queued)
            continue;
        j.queued = false;
        QString error;
        setStatus(j.row, tr("Starting"), 0);
        if (launch(j, &error))
        {
            ++running;
        }
        else
        {
            ++finished_;
            ++failed_;
            setStatus(j.row, error, 0);
        }
    }
    if (finished_ < static_cast<int>(queue_.size()))
    {
        summary_->setText(tr("%1 of %2 done").arg(finished_).arg(queue_.size()));
        return;
    }
    runButton_->setEnabled(true);
    stopButton_->setEnabled(false);
    jobs_->setEnabled(true);
    summary_->setText(failed_ ? tr("%1 of %2 failed").arg(failed_).arg(queue_.size())
                              : tr("All %n run(s) finished", nullptr, static_cast<int>(queue_.size())));
    emit allFinished();
}

void ScenarioRunDialog::stop()
{
    for (auto& j : queue_)
    {
        if (j.queued)
        {
            j.queued = false;
            ++finished_;
            ++failed_;
            setStatus(j.row, tr("Not started"), 0);
        }
    }
    for (auto& j : queue_)
    {
        if (j.process && j.process->isRunning())
            j.process->cancel();
    }
    if (!isRunning())
        startNext();
}

void ScenarioRunDialog::reject()
{
    if (isRunning() &&
        QMessageBox::question(this, tr("Run scenarios"), tr("Stop the runs that are still in progress?")) !=
            QMessageBox::Yes)
        return;
    stop();
    QDialog::reject();
}

} // namespace mirab::app
