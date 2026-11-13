#include "RuleBuilderDialog.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QUuid>

namespace honeycomb {

RuleBuilderDialog::RuleBuilderDialog(const Collection& initial, QWidget* parent)
    : QDialog(parent), m_base(initial) {
    setWindowTitle(initial.id.isEmpty() ? "New Collection / Smart Playlist" : "Edit Collection");
    resize(740, 560);

    auto* root = new QVBoxLayout(this);

    auto* form = new QFormLayout();
    m_nameEdit = new QLineEdit(initial.name, this);
    m_nameEdit->setPlaceholderText("e.g. Christmas (Various Artists)");
    form->addRow("Name:", m_nameEdit);

    m_typeCombo = new QComboBox(this);
    m_typeCombo->addItem("Collection (Filtered Library)", false);
    m_typeCombo->addItem("Smart Playlist", true);
    m_typeCombo->setCurrentIndex(initial.isSmartPlaylist ? 1 : 0);
    form->addRow("Type:", m_typeCombo);

    m_logicCombo = new QComboBox(this);
    m_logicCombo->addItem("Match ALL rules (AND)", static_cast<int>(RuleGroup::Logic::All));
    m_logicCombo->addItem("Match ANY rule (OR)", static_cast<int>(RuleGroup::Logic::Any));
    m_logicCombo->setCurrentIndex(initial.rules.logic == RuleGroup::Logic::Any ? 1 : 0);
    form->addRow("Rule Logic:", m_logicCombo);
    root->addLayout(form);

    // Rules box
    auto* rulesGroup = new QGroupBox("Filter Rules", this);
    auto* rulesBoxLayout = new QVBoxLayout(rulesGroup);

    auto* scroll = new QScrollArea(rulesGroup);
    scroll->setWidgetResizable(true);
    auto* scrollInner = new QWidget(scroll);
    m_rulesLayout = new QVBoxLayout(scrollInner);
    m_rulesLayout->setAlignment(Qt::AlignTop);
    scroll->setWidget(scrollInner);
    rulesBoxLayout->addWidget(scroll, 1);

    auto* btnRow = new QHBoxLayout();
    auto* addBtn = new QPushButton("+ Add Rule", rulesGroup);
    auto* addSubBtn = new QPushButton("+ Add Sub-Group", rulesGroup);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(addSubBtn);
    btnRow->addStretch();
    rulesBoxLayout->addLayout(btnRow);
    root->addWidget(rulesGroup, 1);

    connect(addBtn, &QPushButton::clicked, this, [this]() {
        addRuleRow(m_rulesLayout, m_rows, {FilterField::Genre, FilterOp::Is, ""});
    });
    connect(addSubBtn, &QPushButton::clicked, this, [this]() {
        RuleGroup sub;
        sub.logic = RuleGroup::Logic::Any;
        sub.conditions.append({FilterField::Genre, FilterOp::Is, ""});
        addSubGroup(sub);
    });

    // Per-Collection View Settings
    auto* viewGroup = new QGroupBox("Default View for This Collection", this);
    auto* viewLayout = new QHBoxLayout(viewGroup);

    m_viewModeCombo = new QComboBox(viewGroup);
    m_viewModeCombo->addItem("Album Covers Grid", static_cast<int>(ViewMode::AlbumCovers));
    m_viewModeCombo->addItem("Album & Tracks", static_cast<int>(ViewMode::AlbumAndTracks));
    m_viewModeCombo->addItem("Tracks Table", static_cast<int>(ViewMode::TracksTable));
    m_viewModeCombo->setCurrentIndex(static_cast<int>(initial.viewMode));

    m_sortCombo = new QComboBox(viewGroup);
    m_sortCombo->addItem("Album Artist / Year", static_cast<int>(SortOrder::AlbumArtistYear));
    m_sortCombo->addItem("Album Title", static_cast<int>(SortOrder::AlbumTitle));
    m_sortCombo->addItem("Year (Newest First)", static_cast<int>(SortOrder::YearDesc));
    m_sortCombo->addItem("Recently Added", static_cast<int>(SortOrder::RecentlyAdded));
    m_sortCombo->addItem("Track Title", static_cast<int>(SortOrder::TrackTitle));
    m_sortCombo->setCurrentIndex(static_cast<int>(initial.sortOrder));

    m_artSizeSpin = new QSpinBox(viewGroup);
    m_artSizeSpin->setRange(120, 360);
    m_artSizeSpin->setSingleStep(20);
    m_artSizeSpin->setSuffix(" px");
    m_artSizeSpin->setValue(initial.artworkSize);

    m_colBrowserCheck = new QCheckBox("Column Browser", viewGroup);
    m_colBrowserCheck->setChecked(initial.showColumnBrowser);

    viewLayout->addWidget(new QLabel("View:", viewGroup));
    viewLayout->addWidget(m_viewModeCombo);
    viewLayout->addSpacing(8);
    viewLayout->addWidget(new QLabel("Sort:", viewGroup));
    viewLayout->addWidget(m_sortCombo);
    viewLayout->addSpacing(8);
    viewLayout->addWidget(m_artSizeSpin);
    viewLayout->addSpacing(8);
    viewLayout->addWidget(m_colBrowserCheck);
    viewLayout->addStretch();
    root->addWidget(viewGroup);

    auto* limitGroup = new QGroupBox("Track Limit (Smart Playlists)", this);
    auto* limitLayout = new QHBoxLayout(limitGroup);
    m_limitSpin = new QSpinBox(limitGroup);
    m_limitSpin->setRange(0, 10000);
    m_limitSpin->setSpecialValueText("Unlimited");
    m_limitSpin->setSuffix(" tracks");
    m_limitSpin->setValue(initial.limitCount);

    m_limitOrderCombo = new QComboBox(limitGroup);
    m_limitOrderCombo->addItem("Default Sort", static_cast<int>(LimitOrder::Default));
    m_limitOrderCombo->addItem("Random", static_cast<int>(LimitOrder::Random));
    m_limitOrderCombo->addItem("Highest Rated", static_cast<int>(LimitOrder::HighestRated));
    m_limitOrderCombo->addItem("Most Played", static_cast<int>(LimitOrder::MostPlayed));
    m_limitOrderCombo->addItem("Least Played", static_cast<int>(LimitOrder::LeastPlayed));
    m_limitOrderCombo->addItem("Recently Added", static_cast<int>(LimitOrder::RecentlyAdded));
    m_limitOrderCombo->setCurrentIndex(static_cast<int>(initial.limitOrder));

    limitLayout->addWidget(new QLabel("Limit to:", limitGroup));
    limitLayout->addWidget(m_limitSpin);
    limitLayout->addSpacing(12);
    limitLayout->addWidget(new QLabel("Pick by:", limitGroup));
    limitLayout->addWidget(m_limitOrderCombo);
    limitLayout->addStretch();
    root->addWidget(limitGroup);

    if (initial.rules.conditions.isEmpty() && initial.rules.subGroups.isEmpty()) {
        addRuleRow(m_rulesLayout, m_rows, {FilterField::Genre, FilterOp::Is, ""});
    } else {
        for (const auto& c : initial.rules.conditions) {
            addRuleRow(m_rulesLayout, m_rows, c);
        }
        for (const auto& sub : initial.rules.subGroups) {
            addSubGroup(sub);
        }
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    root->addWidget(buttons);
}

void RuleBuilderDialog::addRuleRow(QVBoxLayout* targetLayout, QVector<RuleRowWidget>& targetRows, const RuleCondition& cond) {
    RuleRowWidget row;
    row.container = new QWidget(this);
    auto* h = new QHBoxLayout(row.container);
    h->setContentsMargins(0, 2, 0, 2);

    row.fieldCombo = new QComboBox(row.container);
    for (FilterField f : allFilterFields()) {
        row.fieldCombo->addItem(filterFieldDisplayName(f), static_cast<int>(f));
    }
    const int fIdx = row.fieldCombo->findData(static_cast<int>(cond.field));
    if (fIdx >= 0) row.fieldCombo->setCurrentIndex(fIdx);

    row.opCombo = new QComboBox(row.container);
    row.valueEdit = new QLineEdit(cond.value, row.container);
    row.valueEdit->setPlaceholderText("Value...");

    auto* removeBtn = new QPushButton("×", row.container);
    removeBtn->setFixedWidth(28);

    h->addWidget(row.fieldCombo, 2);
    h->addWidget(row.opCombo, 2);
    h->addWidget(row.valueEdit, 3);
    h->addWidget(removeBtn);

    targetLayout->addWidget(row.container);
    targetRows.append(row);

    const int rowIndex = targetRows.size() - 1;
    updateOperatorsForRow(targetRows[rowIndex], cond.op);

    connect(row.fieldCombo, &QComboBox::currentIndexChanged, this, [this, &targetRows, container = row.container](int) {
        for (auto& r : targetRows) {
            if (r.container == container) {
                updateOperatorsForRow(r, static_cast<FilterOp>(r.opCombo->currentData().toInt()));
                break;
            }
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [targetLayout, &targetRows, container = row.container]() {
        for (int i = 0; i < targetRows.size(); ++i) {
            if (targetRows[i].container == container) {
                targetLayout->removeWidget(container);
                container->deleteLater();
                targetRows.removeAt(i);
                break;
            }
        }
    });
}

void RuleBuilderDialog::addSubGroup(const RuleGroup& sub) {
    SubGroupWidget sg;
    sg.container = new QGroupBox("Sub-Group", this);
    sg.container->setStyleSheet("QGroupBox { border: 1px solid #343746; border-radius: 6px; margin-top: 8px; padding-top: 10px; }");
    auto* v = new QVBoxLayout(sg.container);

    auto* top = new QHBoxLayout();
    sg.logicCombo = new QComboBox(sg.container);
    sg.logicCombo->addItem("Match ALL in sub-group (AND)", static_cast<int>(RuleGroup::Logic::All));
    sg.logicCombo->addItem("Match ANY in sub-group (OR)", static_cast<int>(RuleGroup::Logic::Any));
    sg.logicCombo->setCurrentIndex(sub.logic == RuleGroup::Logic::Any ? 1 : 0);

    auto* addSubRuleBtn = new QPushButton("+ Rule", sg.container);
    auto* removeSubBtn = new QPushButton("Remove Group", sg.container);

    top->addWidget(sg.logicCombo);
    top->addStretch();
    top->addWidget(addSubRuleBtn);
    top->addWidget(removeSubBtn);
    v->addLayout(top);

    sg.rowsLayout = new QVBoxLayout();
    v->addLayout(sg.rowsLayout);

    m_rulesLayout->addWidget(sg.container);
    m_subGroups.append(sg);
    const int sgIdx = m_subGroups.size() - 1;

    for (const auto& c : sub.conditions) {
        addRuleRow(m_subGroups[sgIdx].rowsLayout, m_subGroups[sgIdx].rows, c);
    }

    connect(addSubRuleBtn, &QPushButton::clicked, this, [this, container = sg.container]() {
        for (auto& g : m_subGroups) {
            if (g.container == container) {
                addRuleRow(g.rowsLayout, g.rows, {FilterField::Genre, FilterOp::Is, ""});
                break;
            }
        }
    });

    connect(removeSubBtn, &QPushButton::clicked, this, [this, container = sg.container]() {
        for (int i = 0; i < m_subGroups.size(); ++i) {
            if (m_subGroups[i].container == container) {
                m_rulesLayout->removeWidget(container);
                container->deleteLater();
                m_subGroups.removeAt(i);
                break;
            }
        }
    });
}

void RuleBuilderDialog::updateOperatorsForRow(RuleRowWidget& row, FilterOp preferredOp) {
    const auto field = static_cast<FilterField>(row.fieldCombo->currentData().toInt());
    const auto ops = operatorsForField(field);
    row.opCombo->clear();
    for (FilterOp op : ops) {
        row.opCombo->addItem(filterOpDisplayName(op), static_cast<int>(op));
    }
    const int idx = row.opCombo->findData(static_cast<int>(preferredOp));
    if (idx >= 0) row.opCombo->setCurrentIndex(idx);

    const bool hideValue = (field == FilterField::IsCompilation);
    row.valueEdit->setVisible(!hideValue);
}

Collection RuleBuilderDialog::resultCollection() const {
    Collection c = m_base;
    if (c.id.isEmpty()) {
        c.id = "col_" + QUuid::createUuid().toString(QUuid::Id128);
    }
    c.name = m_nameEdit->text().trimmed().isEmpty() ? "Untitled Collection" : m_nameEdit->text().trimmed();
    c.isSmartPlaylist = m_typeCombo->currentData().toBool();
    c.viewMode = static_cast<ViewMode>(m_viewModeCombo->currentData().toInt());
    c.sortOrder = static_cast<SortOrder>(m_sortCombo->currentData().toInt());
    c.artworkSize = m_artSizeSpin->value();
    c.showColumnBrowser = m_colBrowserCheck->isChecked();
    c.limitCount = m_limitSpin->value();
    c.limitOrder = static_cast<LimitOrder>(m_limitOrderCombo->currentData().toInt());

    c.rules = RuleGroup{};
    c.rules.logic = static_cast<RuleGroup::Logic>(m_logicCombo->currentData().toInt());
    for (const auto& r : m_rows) {
        RuleCondition cond;
        cond.field = static_cast<FilterField>(r.fieldCombo->currentData().toInt());
        cond.op = static_cast<FilterOp>(r.opCombo->currentData().toInt());
        cond.value = r.valueEdit->text().trimmed();
        c.rules.conditions.append(cond);
    }
    for (const auto& sg : m_subGroups) {
        RuleGroup sub;
        sub.logic = static_cast<RuleGroup::Logic>(sg.logicCombo->currentData().toInt());
        for (const auto& r : sg.rows) {
            RuleCondition cond;
            cond.field = static_cast<FilterField>(r.fieldCombo->currentData().toInt());
            cond.op = static_cast<FilterOp>(r.opCombo->currentData().toInt());
            cond.value = r.valueEdit->text().trimmed();
            sub.conditions.append(cond);
        }
        if (!sub.isEmpty()) {
            c.rules.subGroups.append(sub);
        }
    }
    return c;
}

} // namespace honeycomb
