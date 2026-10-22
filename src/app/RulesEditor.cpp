/*
 * Mirab - Unified Water Infrastructure Modeling Environment
 * https://github.com/ehsanMa86/Mirab
 *
 * File:    src/app/RulesEditor.cpp
 * Purpose: Control rule editor with syntax highlighting, live checks, templates and a builder.
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
#include "RulesEditor.h"

#include "Icons.h"
#include "Language.h"
#include "Theme.h"

#include "mirab/core/Model.h"
#include "mirab/core/Text.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSplitter>
#include <QTextBlock>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <tuple>

namespace mirab::app
{

namespace
{
QString qs(const std::string& s)
{
    return QString::fromStdString(s);
}

QStringList qlist(const std::vector<std::string>& v)
{
    QStringList out;
    for (const auto& s : v)
        out << qs(s);
    return out;
}

constexpr int kBlockRole = Qt::UserRole;
constexpr int kLineRole = Qt::UserRole + 1;
} // namespace

// ---------------------------------------------------------------------------
// RuleHighlighter

RuleHighlighter::RuleHighlighter(QTextDocument* doc, core::RuleSyntax syntax, bool simpleControls)
    : QSyntaxHighlighter(doc)
{
    auto add = [](QSet<QString>& set, const std::vector<std::string>& words) {
        for (const auto& w : words)
            set.insert(qs(w));
    };
    if (simpleControls)
    {
        keywords_ = {"LINK"};
        objects_ = {"NODE"};
        others_ = {"AT", "IF", "TIME", "CLOCKTIME", "ABOVE", "BELOW", "OPEN", "CLOSED", "AM", "PM"};
        return;
    }
    add(keywords_, core::ruleKeywords(syntax));
    add(objects_, core::ruleObjectTypes(syntax, false));
    add(objects_, core::ruleObjectTypes(syntax, true));
    for (const auto& o : objects_)
    {
        add(attributes_, core::ruleAttributes(syntax, o.toStdString(), false));
        add(attributes_, core::ruleAttributes(syntax, o.toStdString(), true));
    }
    add(others_, core::ruleRelations(syntax));
    add(others_, core::ruleOtherWords(syntax));
}

void RuleHighlighter::setErrorLines(const QSet<int>& lines)
{
    if (lines == errors_)
        return;
    errors_ = lines;
    rehighlight();
}

void RuleHighlighter::highlightBlock(const QString& text)
{
    QTextCharFormat keyword, object, attribute, other, number, comment;
    keyword.setForeground(theme::kAccent);
    keyword.setFontWeight(QFont::Bold);
    object.setForeground(QColor(20, 125, 110));
    object.setFontWeight(QFont::DemiBold);
    attribute.setForeground(QColor(142, 72, 190));
    other.setForeground(QColor(196, 100, 0));
    number.setForeground(QColor(30, 120, 50));
    comment.setForeground(theme::kTextMuted);
    comment.setFontItalic(true);
    static const QRegularExpression word("\\S+");
    static const QRegularExpression numeric("^[-+]?(\\d+(\\.\\d*)?|\\.\\d+)([eE][-+]?\\d+)?$|^\\d+:\\d+(:\\d+)?$");
    static const QRegularExpression pastRain("^\\d+-?HR_PRECIP$", QRegularExpression::CaseInsensitiveOption);
    const int semi = text.indexOf(';');
    const QString code = semi >= 0 ? text.left(semi) : text;
    auto it = word.globalMatch(code);
    bool first = true;
    while (it.hasNext())
    {
        const auto m = it.next();
        const QString w = m.captured().toUpper();
        const QTextCharFormat* f = nullptr;
        if (first && keywords_.contains(w))
            f = &keyword;
        else if (objects_.contains(w))
            f = &object;
        else if (attributes_.contains(w) || pastRain.match(w).hasMatch())
            f = &attribute;
        else if (others_.contains(w))
            f = &other;
        else if (numeric.match(w).hasMatch())
            f = &number;
        if (f)
            setFormat(static_cast<int>(m.capturedStart()), static_cast<int>(m.capturedLength()), *f);
        first = false;
    }
    if (semi >= 0)
        setFormat(semi, static_cast<int>(text.size()) - semi, comment);
    if (errors_.contains(currentBlock().blockNumber()) && !text.trimmed().isEmpty())
    {
        for (int i = 0; i < text.size(); ++i)
        {
            QTextCharFormat f = format(i);
            f.setUnderlineStyle(QTextCharFormat::WaveUnderline);
            f.setUnderlineColor(theme::kDanger);
            setFormat(i, 1, f);
        }
    }
}

// ---------------------------------------------------------------------------
// ControlRulesEditor

ControlRulesEditor::ControlRulesEditor(ProjectDocument* doc, const QString& section, QWidget* parent)
    : SpecialEditor(doc, section, {}, parent)
{
    const bool swmm = doc->model().dialect() == core::Dialect::Swmm;
    syntax_ = swmm ? core::RuleSyntax::Swmm : core::RuleSyntax::Epanet;
    simple_ = !swmm && section == "CONTROLS";

    newRule_ = new QToolButton(this);
    newRule_->setText(tr("New rule"));
    newRule_->setIcon(icons::get("new"));
    newRule_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    newRule_->setPopupMode(QToolButton::InstantPopup);
    auto* menu = new QMenu(newRule_);
    newRule_->setMenu(menu);
    connect(menu, &QMenu::aboutToShow, this, &ControlRulesEditor::fillTemplateMenu);
    builder_ = new QPushButton(icons::get("settings"), tr("Rule builder..."), this);
    delete_ = new QPushButton(icons::get("delete"), tr("Delete rule"), this);
    headerTools()->addWidget(newRule_);
    headerTools()->addWidget(builder_);
    headerTools()->addWidget(delete_);
    headerTools()->addSpacing(12);
    newRule_->setVisible(!simple_);
    builder_->setVisible(!simple_);
    delete_->setVisible(!simple_);

    auto* split = new QSplitter(Qt::Horizontal, body());
    auto* bl = new QVBoxLayout(body());
    bl->setContentsMargins(0, 0, 0, 0);
    bl->addWidget(split);
    list_ = new QListWidget(split);
    list_->setMinimumWidth(170);
    list_->setVisible(!simple_);
    edit_ = new QPlainTextEdit(split);
    edit_->setLayoutDirection(Qt::LeftToRight); // rule text
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(std::max(9.5, mono.pointSizeF()));
    edit_->setFont(mono);
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabStopDistance(edit_->fontMetrics().horizontalAdvance(' ') * 4);
    highlighter_ = new RuleHighlighter(edit_->document(), syntax_, simple_);
    split->addWidget(list_);
    split->addWidget(edit_);
    split->setStretchFactor(1, 1);
    split->setSizes({220, 760});
    setMessageHeight(110);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(250);
    connect(timer_, &QTimer::timeout, this, &ControlRulesEditor::validate);
    connect(list_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (!updatingList_ && row >= 0)
            selectBlock(row);
    });
    connect(edit_, &QPlainTextEdit::textChanged, this, [this] {
        if (loading_ || current_ < 0 || current_ >= blocks_.size())
            return;
        blocks_[current_] = edit_->toPlainText();
        if (auto* item = list_->item(current_))
            item->setText(blockLabel(current_));
        setDirty(true);
        timer_->start();
    });
    connect(messageList(), &QListWidget::itemActivated, this, [this](QListWidgetItem* item) {
        if (item->data(kBlockRole).isNull())
            return;
        const int block = item->data(kBlockRole).toInt();
        const int line = item->data(kLineRole).toInt();
        if (block != current_)
            list_->setCurrentRow(block);
        QTextCursor c(edit_->document()->findBlockByNumber(line));
        edit_->setTextCursor(c);
        edit_->setFocus();
    });
    connect(messageList(), &QListWidget::itemClicked, messageList(), &QListWidget::itemActivated);
    connect(builder_, &QPushButton::clicked, this, &ControlRulesEditor::openBuilder);
    connect(delete_, &QPushButton::clicked, this, &ControlRulesEditor::deleteRule);
    load();
}

QString ControlRulesEditor::blockLabel(int index) const
{
    if (simple_)
        return tr("Simple controls");
    if (index == 0)
        return syntax_ == core::RuleSyntax::Swmm ? tr("Variables and notes") : tr("Notes");
    const QStringList lines = blocks_.value(index).split('\n');
    for (const QString& l : lines)
    {
        const QStringList t = l.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (t.size() >= 2 && t[0].compare("RULE", Qt::CaseInsensitive) == 0)
            return t[1];
    }
    return tr("(unnamed rule)");
}

std::vector<std::string> ControlRulesEditor::allLines(std::vector<std::pair<int, int>>* origin) const
{
    std::vector<std::string> lines;
    for (int b = 0; b < blocks_.size(); ++b)
    {
        if (blocks_[b].isEmpty())
            continue;
        const QStringList parts = blocks_[b].split('\n');
        for (int i = 0; i < parts.size(); ++i)
        {
            lines.push_back(parts[i].toStdString());
            if (origin)
                origin->emplace_back(b, i);
        }
    }
    return lines;
}

void ControlRulesEditor::setLines(const std::vector<std::string>& lines, const QString& select)
{
    blocks_.clear();
    if (simple_)
    {
        QStringList all;
        for (const auto& l : lines)
            all << qs(l);
        blocks_ << all.join('\n');
    }
    else
    {
        for (const auto& b : core::splitRuleBlocks(lines))
        {
            QStringList part;
            for (int i = 0; i < b.lineCount; ++i)
                part << qs(lines[static_cast<std::size_t>(b.firstLine + i)]);
            blocks_ << part.join('\n');
        }
    }
    updatingList_ = true;
    list_->clear();
    int target = simple_ ? 0 : (blocks_.size() > 1 ? 1 : 0);
    for (int i = 0; i < blocks_.size(); ++i)
    {
        auto* item = new QListWidgetItem(blockLabel(i), list_);
        if (i == 0 && !simple_)
        {
            QFont f = item->font();
            f.setItalic(true);
            item->setFont(f);
        }
        if (!select.isEmpty() && blockLabel(i) == select)
            target = i;
    }
    updatingList_ = false;
    current_ = -1;
    list_->setCurrentRow(target);
    selectBlock(target);
}

void ControlRulesEditor::selectBlock(int index)
{
    if (index < 0 || index >= blocks_.size())
        return;
    if (index == current_)
        return;
    current_ = index;
    const bool was = loading_;
    loading_ = true;
    edit_->setPlainText(blocks_[index]);
    loading_ = was;
    delete_->setEnabled(index > 0);
    validate();
}

void ControlRulesEditor::load()
{
    loading_ = true;
    const auto* s = doc_->model().document().find(section_.toStdString());
    const std::vector<std::string> lines = s ? s->lines : std::vector<std::string>{};
    const QString keep = current_ >= 0 ? blockLabel(current_) : QString();
    setLines(lines, keep);
    loading_ = false;
    if (simple_)
        setTitle(tr("Simple controls"));
    else
        setTitle(syntax_ == core::RuleSyntax::Swmm ? tr("Control rules") : tr("Rule-based controls"));
    setDirty(false);
    validate();
}

void ControlRulesEditor::validate()
{
    std::vector<std::pair<int, int>> origin;
    const auto lines = allLines(&origin);
    const auto ctx = core::RuleContext::fromModel(doc_->model());
    const auto msgs = simple_ ? core::validateSimpleControls(lines, &ctx)
                              : core::validateRules(lines, syntax_, &ctx);
    auto* list = messageList();
    list->clear();
    QSet<int> errorLines, badBlocks;
    int errors = 0, warnings = 0;
    for (const auto& m : msgs)
    {
        int block = 0, line = 0;
        if (m.line >= 1 && m.line <= static_cast<int>(origin.size()))
            std::tie(block, line) = origin[static_cast<std::size_t>(m.line - 1)];
        (m.error ? errors : warnings)++;
        if (m.error)
            badBlocks.insert(block);
        if (block == current_)
            errorLines.insert(line);
        const QString where = simple_ ? tr("Line %1").arg(line + 1)
                                      : tr("%1, line %2").arg(blockLabel(block)).arg(line + 1);
        auto* item = new QListWidgetItem(icons::get(m.error ? "error" : "warning"), where + ": " + qs(m.message), list);
        item->setData(kBlockRole, block);
        item->setData(kLineRole, line);
    }
    if (msgs.empty())
    {
        auto* item = new QListWidgetItem(icons::get("info"), tr("No problems found."), list);
        item->setForeground(theme::kSuccess);
    }
    highlighter_->setErrorLines(errorLines);
    QPixmap empty(16, 16);
    empty.fill(Qt::transparent);
    const QIcon blank(empty);
    for (int i = 0; i < list_->count(); ++i)
    {
        auto* item = list_->item(i);
        item->setIcon(badBlocks.contains(i) ? icons::get("error") : blank);
        item->setForeground(badBlocks.contains(i) ? theme::kDanger : theme::kText);
    }
    const int rules = static_cast<int>(core::ruleNames(lines).size());
    QString state;
    if (simple_)
        state = tr("%n control line(s)", nullptr,
                   static_cast<int>(std::count_if(lines.begin(), lines.end(), [](const std::string& l) {
                       return core::InpSection::isDataLine(l);
                   })));
    else
        state = tr("%n rule(s)", nullptr, rules);
    if (errors)
        state += tr(", %n error(s)", nullptr, errors);
    if (warnings)
        state += tr(", %n warning(s)", nullptr, warnings);
    if (hasPendingEdits())
        state += tr("  ·  edited; Apply writes the changes to the project");
    setState(state);
}

void ControlRulesEditor::fillTemplateMenu()
{
    auto* menu = newRule_->menu();
    menu->clear();
    const auto ctx = core::RuleContext::fromModel(doc_->model());
    const auto used = core::ruleNames(allLines());
    for (const auto& t : core::ruleTemplates(syntax_, ctx, used))
    {
        QAction* a = menu->addAction(coreText(t.title));
        a->setToolTip(coreText(t.description));
        a->setStatusTip(coreText(t.description));
        const QStringList vars = qlist(t.variables), lines = qlist(t.lines);
        connect(a, &QAction::triggered, this, [this, vars, lines] { addRule(vars, lines); });
    }
    menu->setToolTipsVisible(true);
}

void ControlRulesEditor::addRule(const QStringList& variables, const QStringList& lines)
{
    if (blocks_.isEmpty())
        blocks_ << QString();
    if (!variables.isEmpty())
    {
        QString& lead = blocks_[0];
        // Keep a blank line between the variables and the first rule.
        QStringList leadLines = lead.isEmpty() ? QStringList() : lead.split('\n');
        while (!leadLines.isEmpty() && leadLines.last().trimmed().isEmpty())
            leadLines.removeLast();
        leadLines << variables << QString();
        lead = leadLines.join('\n');
    }
    QString& last = blocks_.last();
    if (!last.isEmpty() && !last.endsWith('\n'))
        last += '\n';
    QString name;
    for (const QString& l : lines)
    {
        const QStringList t = l.split(' ', Qt::SkipEmptyParts);
        if (t.size() >= 2 && t[0] == "RULE")
        {
            name = t[1];
            break;
        }
    }
    QStringList all = blocks_;
    all << lines.join('\n') + '\n';
    std::vector<std::string> flat;
    for (const QString& b : all)
    {
        if (b.isEmpty())
            continue;
        for (const QString& l : b.split('\n'))
            flat.push_back(l.toStdString());
    }
    loading_ = true;
    setLines(flat, name);
    loading_ = false;
    setDirty(true);
    validate();
    edit_->setFocus();
}

void ControlRulesEditor::deleteRule()
{
    if (current_ <= 0 || current_ >= blocks_.size())
        return;
    blocks_.removeAt(current_);
    const int next = std::min(current_, static_cast<int>(blocks_.size()) - 1);
    const auto lines = allLines();
    const QString keep = next >= 0 ? blockLabel(next) : QString();
    loading_ = true;
    setLines(lines, keep);
    loading_ = false;
    setDirty(true);
    validate();
}

void ControlRulesEditor::openBuilder()
{
    const auto ctx = core::RuleContext::fromModel(doc_->model());
    RuleBuilderDialog dlg(syntax_, ctx, core::ruleNames(allLines()), this);
    if (dlg.exec() == QDialog::Accepted)
        addRule({}, dlg.ruleLines());
}

void ControlRulesEditor::apply()
{
    timer_->stop();
    std::vector<std::string> lines = allLines();
    while (!lines.empty() && text::trim(lines.back()).empty())
        lines.pop_back();
    const std::string section = section_.toStdString();
    const bool exists = doc_->model().document().has(section);
    if (lines.empty() && !exists)
    {
        load();
        return;
    }
    lines.emplace_back();
    QString err;
    const bool ok = doc_->edit(tr("Edit %1").arg(objectEditorTitle(doc_->model().dialect(), section_, {}).toLower()),
                               [&](core::Model& m, std::string*) {
                                   m.document().ensure(section).lines = lines;
                                   m.rebuild();
                                   return true;
                               },
                               &err);
    if (!ok)
    {
        setState(err);
        return;
    }
    applied(id_);
}

// ---------------------------------------------------------------------------
// RuleBuilderDialog

RuleBuilderDialog::RuleBuilderDialog(core::RuleSyntax syntax, const core::RuleContext& context,
                                     const std::set<std::string>& usedNames, QWidget* parent)
    : QDialog(parent), syntax_(syntax), context_(context)
{
    setWindowTitle(tr("Rule builder"));
    resize(760, 560);
    auto* layout = new QVBoxLayout(this);
    auto* top = new QFormLayout;
    name_ = new QLineEdit(qs(core::uniqueRuleName(usedNames, "Rule1")), this);
    top->addRow(tr("Rule name"), name_);
    layout->addLayout(top);

    auto* condBox = new QGroupBox(tr("Condition"), this);
    auto* cg = new QGridLayout(condBox);
    cg->addWidget(new QLabel(tr("IF"), condBox), 0, 0);
    cond_ = makeClause(condBox, false);
    QWidget* row1[] = {cond_.object, cond_.id, cond_.attribute, cond_.relation, cond_.value};
    for (int i = 0; i < 5; ++i)
        cg->addWidget(row1[i], 0, i + 1);
    useCond2_ = new QCheckBox(condBox);
    join_ = new QComboBox(condBox);
    join_->addItems({"AND", "OR"});
    auto* j = new QHBoxLayout;
    j->addWidget(useCond2_);
    j->addWidget(join_);
    cg->addLayout(j, 1, 0);
    cond2_ = makeClause(condBox, false);
    QWidget* row2[] = {cond2_.object, cond2_.id, cond2_.attribute, cond2_.relation, cond2_.value};
    for (int i = 0; i < 5; ++i)
        cg->addWidget(row2[i], 1, i + 1);
    layout->addWidget(condBox);

    auto* actBox = new QGroupBox(tr("Action"), this);
    auto* ag = new QGridLayout(actBox);
    ag->addWidget(new QLabel(tr("THEN"), actBox), 0, 0);
    action_ = makeClause(actBox, true);
    QWidget* row3[] = {action_.object, action_.id, action_.attribute, action_.relation, action_.value};
    for (int i = 0; i < 5; ++i)
        ag->addWidget(row3[i], 0, i + 1);
    useElse_ = new QCheckBox(tr("ELSE set it to"), actBox);
    elseValue_ = new QComboBox(actBox);
    elseValue_->setEditable(true);
    ag->addWidget(useElse_, 1, 0, 1, 3);
    ag->addWidget(elseValue_, 1, 5);
    priority_ = new QLineEdit(actBox);
    priority_->setPlaceholderText(tr("optional"));
    priority_->setToolTip(tr("When rules conflict, the action of the rule with the highest priority wins."));
    ag->addWidget(new QLabel(tr("Priority"), actBox), 2, 0, 1, 3);
    ag->addWidget(priority_, 2, 5);
    layout->addWidget(actBox);

    layout->addWidget(new QLabel(tr("Rule text"), this));
    preview_ = new QPlainTextEdit(this);
    preview_->setLayoutDirection(Qt::LeftToRight); // rule text
    preview_->setReadOnly(true);
    preview_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    new RuleHighlighter(preview_->document(), syntax_, false);
    layout->addWidget(preview_, 1);
    messages_ = new QListWidget(this);
    messages_->setMaximumHeight(70);
    layout->addWidget(messages_);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    ok_ = buttons->button(QDialogButtonBox::Ok);
    ok_->setText(tr("Add rule"));
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto wire = [this](Clause& c, bool action) {
        connect(c.object, &QComboBox::currentTextChanged, this, [this, &c, action] {
            objectChanged(c, action);
            updatePreview();
        });
        connect(c.attribute, &QComboBox::currentTextChanged, this, [this, &c, action] {
            attributeChanged(c, action);
            updatePreview();
        });
        for (QComboBox* b : {c.id, c.relation, c.value})
            connect(b, &QComboBox::currentTextChanged, this, &RuleBuilderDialog::updatePreview);
        objectChanged(c, action);
    };
    wire(cond_, false);
    wire(cond2_, false);
    wire(action_, true);
    connect(name_, &QLineEdit::textChanged, this, &RuleBuilderDialog::updatePreview);
    connect(priority_, &QLineEdit::textChanged, this, &RuleBuilderDialog::updatePreview);
    connect(elseValue_, &QComboBox::currentTextChanged, this, &RuleBuilderDialog::updatePreview);
    connect(useElse_, &QCheckBox::toggled, this, &RuleBuilderDialog::updatePreview);
    connect(join_, &QComboBox::currentTextChanged, this, &RuleBuilderDialog::updatePreview);
    auto enableSecond = [this](bool on) {
        const QList<QWidget*> widgets = {join_, cond2_.object, cond2_.id, cond2_.attribute, cond2_.relation,
                                         cond2_.value};
        for (QWidget* w : widgets)
            w->setEnabled(on);
        updatePreview();
    };
    connect(useCond2_, &QCheckBox::toggled, this, enableSecond);
    enableSecond(false);
    // The usual condition is on a node.
    if (!context_.nodes().empty())
        cond_.object->setCurrentText("NODE");
    updatePreview();
}

RuleBuilderDialog::Clause RuleBuilderDialog::makeClause(QWidget* parent, bool action)
{
    Clause c;
    c.object = new QComboBox(parent);
    c.object->addItems(qlist(core::ruleObjectTypes(syntax_, action)));
    c.id = new QComboBox(parent);
    c.id->setEditable(true);
    c.id->setMinimumWidth(110);
    c.attribute = new QComboBox(parent);
    c.relation = new QComboBox(parent);
    if (action)
        c.relation->addItem(syntax_ == core::RuleSyntax::Swmm ? "=" : "IS");
    else
        c.relation->addItems(qlist(core::ruleRelations(syntax_)));
    c.value = new QComboBox(parent);
    c.value->setEditable(true);
    c.value->setMinimumWidth(110);
    if (action && syntax_ == core::RuleSyntax::Swmm)
    {
        // Prefer an object type the model has.
        for (const char* o : {"PUMP", "ORIFICE", "WEIR", "OUTLET", "CONDUIT"})
        {
            if (!core::ruleObjectIds(context_, syntax_, o).empty())
            {
                c.object->setCurrentText(o);
                break;
            }
        }
    }
    return c;
}

void RuleBuilderDialog::objectChanged(Clause& c, bool action)
{
    const std::string obj = c.object->currentText().toStdString();
    const QString keepId = c.id->currentText();
    c.id->blockSignals(true);
    c.id->clear();
    c.id->addItems(qlist(core::ruleObjectIds(context_, syntax_, obj)));
    if (!keepId.isEmpty() && c.id->findText(keepId) >= 0)
        c.id->setCurrentText(keepId);
    c.id->blockSignals(false);
    c.id->setVisible(core::ruleObjectHasId(syntax_, obj));
    const QString keepAttr = c.attribute->currentText();
    c.attribute->blockSignals(true);
    c.attribute->clear();
    c.attribute->addItems(qlist(core::ruleAttributes(syntax_, obj, action)));
    if (c.attribute->findText(keepAttr) >= 0)
        c.attribute->setCurrentText(keepAttr);
    c.attribute->blockSignals(false);
    attributeChanged(c, action);
}

void RuleBuilderDialog::attributeChanged(Clause& c, bool action)
{
    const std::string obj = c.object->currentText().toStdString();
    const std::string attr = c.attribute->currentText().toStdString();
    QStringList values = qlist(core::ruleValueWords(syntax_, obj, attr));
    if (action && syntax_ == core::RuleSyntax::Swmm && attr == "SETTING")
    {
        values << "1" << "0.5" << "0";
        for (const auto& cv : context_.curves())
            values << "CURVE " + qs(cv);
        for (const auto& ts : context_.timeSeries())
            values << "TIMESERIES " + qs(ts);
        values << "PID 0.5 0.1 0";
    }
    if (values.isEmpty())
    {
        if (attr == "CLOCKTIME")
            values << "08:00";
        else if (attr == "TIME")
            values << "1:00";
        else if (attr == "DATE")
            values << "01/01/2026";
        else
            values << "0";
    }
    QList<QComboBox*> boxes = {c.value};
    if (action)
        boxes << elseValue_;
    for (QComboBox* b : boxes)
    {
        if (!b)
            continue;
        b->blockSignals(true);
        b->clear();
        b->addItems(values);
        b->blockSignals(false);
    }
    if (action && elseValue_ && values.size() > 1)
        elseValue_->setCurrentIndex(1);
}

QString RuleBuilderDialog::clauseText(const Clause& c, bool action, const QString& value) const
{
    QStringList parts = {c.object->currentText()};
    if (core::ruleObjectHasId(syntax_, c.object->currentText().toStdString()))
        parts << c.id->currentText().trimmed();
    parts << c.attribute->currentText() << c.relation->currentText()
          << (value.isEmpty() ? c.value->currentText().trimmed() : value);
    Q_UNUSED(action);
    return parts.join(' ');
}

QStringList RuleBuilderDialog::ruleLines() const
{
    QStringList lines = {"RULE " + name_->text().trimmed(), "IF " + clauseText(cond_, false)};
    if (useCond2_->isChecked())
        lines << join_->currentText() + " " + clauseText(cond2_, false);
    lines << "THEN " + clauseText(action_, true);
    if (useElse_->isChecked())
        lines << "ELSE " + clauseText(action_, true, elseValue_->currentText().trimmed());
    if (!priority_->text().trimmed().isEmpty())
        lines << "PRIORITY " + priority_->text().trimmed();
    return lines;
}

void RuleBuilderDialog::updatePreview()
{
    if (!preview_)
        return;
    const QStringList lines = ruleLines();
    preview_->setPlainText(lines.join('\n'));
    std::vector<std::string> v;
    for (const QString& l : lines)
        v.push_back(l.toStdString());
    const auto msgs = core::validateRules(v, syntax_, &context_);
    messages_->clear();
    bool errors = false;
    for (const auto& m : msgs)
    {
        errors = errors || m.error;
        new QListWidgetItem(icons::get(m.error ? "error" : "warning"), qs(m.message), messages_);
    }
    if (name_->text().trimmed().contains(' ') || name_->text().trimmed().isEmpty())
        errors = true;
    if (msgs.empty())
    {
        auto* item = new QListWidgetItem(icons::get("info"), tr("The rule is valid."), messages_);
        item->setForeground(theme::kSuccess);
    }
    ok_->setEnabled(!errors);
}

} // namespace mirab::app
