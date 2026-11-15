#include "AutoDjDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QRandomGenerator>
#include <QScrollArea>
#include <QSet>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace honeycomb {

AutoDjConfig AutoDjConfig::load() {
    QSettings s("Honeycomb", "Honeycomb");
    AutoDjConfig cfg;
    cfg.sourceMode = static_cast<SourceMode>(s.value("autodj/sourceMode", static_cast<int>(SourceMode::ActiveCollection)).toInt());
    cfg.specificCollectionId = s.value("autodj/specificCollectionId").toString();
    const QByteArray rulesJson = s.value("autodj/customRules").toByteArray();
    if (!rulesJson.isEmpty()) {
        cfg.customRules = RuleGroup::fromJson(QJsonDocument::fromJson(rulesJson).object());
    }
    cfg.queueDepth = qBound(1, s.value("autodj/queueDepth", 5).toInt(), 25);
    cfg.genreMatchPercent = qBound(0, s.value("autodj/genreMatchPercent", 70).toInt(), 100);
    cfg.artistSpacing = qBound(0, s.value("autodj/artistSpacing", 3).toInt(), 20);
    cfg.avoidRecentDays = qBound(0, s.value("autodj/avoidRecentDays", 7).toInt(), 365);
    cfg.skipOneStar = s.value("autodj/skipOneStar", true).toBool();
    cfg.preferHighRated = s.value("autodj/preferHighRated", true).toBool();
    return cfg;
}

void AutoDjConfig::save() const {
    QSettings s("Honeycomb", "Honeycomb");
    s.setValue("autodj/sourceMode", static_cast<int>(sourceMode));
    s.setValue("autodj/specificCollectionId", specificCollectionId);
    s.setValue("autodj/customRules", QJsonDocument(customRules.toJson()).toJson(QJsonDocument::Compact));
    s.setValue("autodj/queueDepth", queueDepth);
    s.setValue("autodj/genreMatchPercent", genreMatchPercent);
    s.setValue("autodj/artistSpacing", artistSpacing);
    s.setValue("autodj/avoidRecentDays", avoidRecentDays);
    s.setValue("autodj/skipOneStar", skipOneStar);
    s.setValue("autodj/preferHighRated", preferHighRated);
}

QVector<Track> pickAutoDjTracks(const AutoDjConfig& cfg,
                                const Database& db,
                                const QVector<Collection>& collections,
                                const QString& activeCollectionId,
                                const QString& activeSearch,
                                const RuleGroup& activeEffectiveRules,
                                const QVector<Track>& currentQueue,
                                int currentIndex,
                                int count) {
    QVector<Track> picked;
    if (count <= 0) return picked;

    QVector<Track> pool;
    switch (cfg.sourceMode) {
    case AutoDjConfig::SourceMode::EntireLibrary:
        pool = db.queryTracks(RuleGroup{}, QString(), SortOrder::TrackTitle);
        break;
    case AutoDjConfig::SourceMode::ActiveCollection:
        pool = db.queryTracks(activeEffectiveRules, activeSearch, SortOrder::TrackTitle);
        break;
    case AutoDjConfig::SourceMode::SpecificCollection: {
        bool found = false;
        for (const auto& c : collections) {
            if (c.id == cfg.specificCollectionId) {
                pool = db.queryTracks(c.rules, QString(), SortOrder::TrackTitle, QString(), QString(), c.limitCount, c.limitOrder);
                found = true;
                break;
            }
        }
        if (!found) {
            pool = db.queryTracks(RuleGroup{}, QString(), SortOrder::TrackTitle);
        }
        break;
    }
    case AutoDjConfig::SourceMode::CustomRules:
        pool = db.queryTracks(cfg.customRules, QString(), SortOrder::TrackTitle);
        break;
    }

    if (pool.isEmpty() && cfg.sourceMode == AutoDjConfig::SourceMode::ActiveCollection && !activeCollectionId.isEmpty()) {
        pool = db.queryTracks(RuleGroup{}, QString(), SortOrder::TrackTitle);
    }
    if (pool.isEmpty()) return picked;

    QString seedGenre;
    if (currentIndex >= 0 && currentIndex < currentQueue.size()) {
        seedGenre = currentQueue[currentIndex].genre;
    } else if (!currentQueue.isEmpty()) {
        seedGenre = currentQueue.last().genre;
    }

    QSet<QString> usedPaths;
    QStringList recentArtists;
    for (const Track& q : currentQueue) {
        usedPaths.insert(q.path);
        if (!q.artist.isEmpty()) {
            recentArtists.append(q.artist.toLower());
        }
    }

    const int64_t nowSecs = QDateTime::currentSecsSinceEpoch();
    const int64_t recentCutoff = cfg.avoidRecentDays > 0
                                     ? nowSecs - static_cast<int64_t>(cfg.avoidRecentDays) * 86400LL
                                     : 0;

    auto isRecentArtist = [&](const QString& artist) -> bool {
        if (cfg.artistSpacing <= 0 || artist.isEmpty()) return false;
        const QString low = artist.toLower();
        const int start = qMax(0, recentArtists.size() - cfg.artistSpacing);
        for (int i = start; i < recentArtists.size(); ++i) {
            if (recentArtists[i] == low) return true;
        }
        return false;
    };

    auto* rng = QRandomGenerator::global();

    for (int step = 0; step < count; ++step) {
        QVector<int> genreSpaced;
        QVector<int> otherSpaced;
        QVector<int> spacedAny;
        QVector<int> fallbackUnused;

        for (int i = 0; i < pool.size(); ++i) {
            const Track& cand = pool[i];
            if (usedPaths.contains(cand.path)) continue;
            if (cfg.skipOneStar && cand.rating == 1) continue;
            if (recentCutoff > 0 && cand.lastPlayed >= recentCutoff) continue;

            const bool artistBlocked = isRecentArtist(cand.artist);
            const bool sameGenre = !seedGenre.isEmpty() && cand.genre.compare(seedGenre, Qt::CaseInsensitive) == 0;

            if (!artistBlocked && sameGenre) {
                genreSpaced.append(i);
            } else if (!artistBlocked) {
                otherSpaced.append(i);
            } else {
                spacedAny.append(i);
            }
        }

        if (genreSpaced.isEmpty() && otherSpaced.isEmpty() && spacedAny.isEmpty()) {
            // Relax recent-played / 1-star constraints before repeating
            for (int i = 0; i < pool.size(); ++i) {
                if (!usedPaths.contains(pool[i].path)) {
                    fallbackUnused.append(i);
                }
            }
            if (fallbackUnused.isEmpty()) {
                for (int i = 0; i < pool.size(); ++i) fallbackUnused.append(i);
            }
        }

        const QVector<int>* chosenBucket = nullptr;
        if (!genreSpaced.isEmpty() && (otherSpaced.isEmpty() || rng->bounded(100) < cfg.genreMatchPercent)) {
            chosenBucket = &genreSpaced;
        } else if (!otherSpaced.isEmpty()) {
            chosenBucket = &otherSpaced;
        } else if (!spacedAny.isEmpty()) {
            chosenBucket = &spacedAny;
        } else {
            chosenBucket = &fallbackUnused;
        }

        QVector<int> weighted;
        weighted.reserve(chosenBucket->size() * 2);
        for (int idx : *chosenBucket) {
            weighted.append(idx);
            if (cfg.preferHighRated && pool[idx].rating >= 4) {
                weighted.append(idx);
            }
        }

        const Track& choice = pool[weighted[rng->bounded(weighted.size())]];
        picked.append(choice);
        usedPaths.insert(choice.path);
        if (!choice.artist.isEmpty()) {
            recentArtists.append(choice.artist.toLower());
        }
        if (seedGenre.isEmpty() && !choice.genre.isEmpty()) {
            seedGenre = choice.genre;
        }
    }

    return picked;
}

AutoDjDialog::AutoDjDialog(const AutoDjConfig& initial,
                           const Database* db,
                           const QVector<Collection>& collections,
                           const QString& activeCollectionId,
                           const QString& activeSearch,
                           const RuleGroup& activeEffectiveRules,
                           const QVector<Track>& currentQueue,
                           int currentIndex,
                           QWidget* parent)
    : QDialog(parent),
      m_db(db),
      m_collections(collections),
      m_activeCollectionId(activeCollectionId),
      m_activeSearch(activeSearch),
      m_activeEffectiveRules(activeEffectiveRules),
      m_currentQueue(currentQueue),
      m_currentIndex(currentIndex) {

    setWindowTitle("Auto-DJ Settings");
    resize(700, 660);

    auto* root = new QVBoxLayout(this);
    root->setSpacing(10);

    // 1. Source Group
    auto* sourceGroup = new QGroupBox("Track Source", this);
    auto* sourceLayout = new QVBoxLayout(sourceGroup);

    auto* sourceForm = new QFormLayout();
    m_sourceCombo = new QComboBox(sourceGroup);
    m_sourceCombo->addItem("Entire Library", static_cast<int>(AutoDjConfig::SourceMode::EntireLibrary));
    m_sourceCombo->addItem("Active Collection / Current View", static_cast<int>(AutoDjConfig::SourceMode::ActiveCollection));
    m_sourceCombo->addItem("Specific Collection / Smart Playlist", static_cast<int>(AutoDjConfig::SourceMode::SpecificCollection));
    m_sourceCombo->addItem("Custom Filter Rules (Genre, Rating, Year, etc.)", static_cast<int>(AutoDjConfig::SourceMode::CustomRules));
    m_sourceCombo->setCurrentIndex(static_cast<int>(initial.sourceMode));
    sourceForm->addRow("Pull tracks from:", m_sourceCombo);

    m_collectionCombo = new QComboBox(sourceGroup);
    for (const auto& c : m_collections) {
        m_collectionCombo->addItem(c.name, c.id);
    }
    const int colIdx = m_collectionCombo->findData(initial.specificCollectionId);
    if (colIdx >= 0) m_collectionCombo->setCurrentIndex(colIdx);
    sourceForm->addRow("Playlist / Collection:", m_collectionCombo);
    sourceLayout->addLayout(sourceForm);

    // Custom Rules Area
    m_rulesWidget = new QWidget(sourceGroup);
    auto* rulesOuter = new QVBoxLayout(m_rulesWidget);
    rulesOuter->setContentsMargins(0, 4, 0, 0);

    auto* logicRow = new QHBoxLayout();
    logicRow->addWidget(new QLabel("Match", m_rulesWidget));
    m_logicCombo = new QComboBox(m_rulesWidget);
    m_logicCombo->addItem("ALL of these rules (AND)", static_cast<int>(RuleGroup::Logic::All));
    m_logicCombo->addItem("ANY of these rules (OR)", static_cast<int>(RuleGroup::Logic::Any));
    m_logicCombo->setCurrentIndex(initial.customRules.logic == RuleGroup::Logic::Any ? 1 : 0);
    logicRow->addWidget(m_logicCombo);
    logicRow->addStretch();
    auto* addRuleBtn = new QPushButton("+ Add Rule", m_rulesWidget);
    logicRow->addWidget(addRuleBtn);
    rulesOuter->addLayout(logicRow);

    auto* scroll = new QScrollArea(m_rulesWidget);
    scroll->setWidgetResizable(true);
    scroll->setMinimumHeight(130);
    auto* rulesContainer = new QWidget(scroll);
    m_rulesLayout = new QVBoxLayout(rulesContainer);
    m_rulesLayout->setSpacing(6);
    m_rulesLayout->addStretch();
    scroll->setWidget(rulesContainer);
    rulesOuter->addWidget(scroll);

    sourceLayout->addWidget(m_rulesWidget);
    root->addWidget(sourceGroup);

    if (initial.customRules.conditions.isEmpty()) {
        addRuleRow(RuleCondition{FilterField::Genre, FilterOp::Contains, ""});
    } else {
        for (const auto& c : initial.customRules.conditions) {
            addRuleRow(c);
        }
    }

    connect(addRuleBtn, &QPushButton::clicked, this, [this]() {
        addRuleRow(RuleCondition{FilterField::Genre, FilterOp::Contains, ""});
    });
    connect(m_sourceCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, &AutoDjDialog::updateSourceVisibility);
    updateSourceVisibility();

    // 2. Selection Rules Group
    auto* rulesGroup = new QGroupBox("Selection & Similarity Rules", this);
    auto* selForm = new QFormLayout(rulesGroup);

    m_depthSpin = new QSpinBox(rulesGroup);
    m_depthSpin->setRange(1, 25);
    m_depthSpin->setValue(initial.queueDepth);
    m_depthSpin->setSuffix(" upcoming tracks");
    selForm->addRow("Maintain queue depth:", m_depthSpin);

    auto* genreRow = new QHBoxLayout();
    m_genreSlider = new QSlider(Qt::Horizontal, rulesGroup);
    m_genreSlider->setRange(0, 100);
    m_genreSlider->setValue(initial.genreMatchPercent);
    m_genrePctLabel = new QLabel(QString("%1%").arg(initial.genreMatchPercent), rulesGroup);
    m_genrePctLabel->setFixedWidth(40);
    genreRow->addWidget(m_genreSlider, 1);
    genreRow->addWidget(m_genrePctLabel);
    connect(m_genreSlider, &QSlider::valueChanged, this, [this](int val) {
        m_genrePctLabel->setText(QString("%1%").arg(val));
    });
    selForm->addRow("Match current Genre:", genreRow);

    m_artistSpacingSpin = new QSpinBox(rulesGroup);
    m_artistSpacingSpin->setRange(0, 20);
    m_artistSpacingSpin->setValue(initial.artistSpacing);
    m_artistSpacingSpin->setSpecialValueText("No limit");
    m_artistSpacingSpin->setSuffix(" tracks");
    selForm->addRow("Don't repeat same artist within:", m_artistSpacingSpin);

    m_avoidRecentSpin = new QSpinBox(rulesGroup);
    m_avoidRecentSpin->setRange(0, 365);
    m_avoidRecentSpin->setValue(initial.avoidRecentDays);
    m_avoidRecentSpin->setSpecialValueText("Off");
    m_avoidRecentSpin->setSuffix(" days");
    selForm->addRow("Avoid tracks played within:", m_avoidRecentSpin);

    m_skipOneStarChk = new QCheckBox("Skip 1-star tracks", rulesGroup);
    m_skipOneStarChk->setChecked(initial.skipOneStar);
    selForm->addRow("", m_skipOneStarChk);

    m_preferHighRatedChk = new QCheckBox("Prefer higher-rated tracks (4–5 stars)", rulesGroup);
    m_preferHighRatedChk->setChecked(initial.preferHighRated);
    selForm->addRow("", m_preferHighRatedChk);

    root->addWidget(rulesGroup);

    // 3. Preview Group
    auto* previewGroup = new QGroupBox("Preview", this);
    auto* previewLayout = new QVBoxLayout(previewGroup);
    auto* previewTop = new QHBoxLayout();
    auto* previewBtn = new QPushButton("Preview Next Tracks", previewGroup);
    previewTop->addWidget(previewBtn);
    previewTop->addStretch();
    previewLayout->addLayout(previewTop);

    m_previewList = new QListWidget(previewGroup);
    m_previewList->setMaximumHeight(115);
    previewLayout->addWidget(m_previewList);
    root->addWidget(previewGroup);

    connect(previewBtn, &QPushButton::clicked, this, &AutoDjDialog::onPreview);

    // Bottom Buttons
    auto* btnRow = new QHBoxLayout();
    auto* startBtn = new QPushButton("Save && Start Auto-DJ", this);
    startBtn->setStyleSheet("background: #f5a623; color: #16171d; font-weight: bold; padding: 6px 14px; border-radius: 4px;");
    btnRow->addWidget(startBtn);
    btnRow->addStretch();

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    btnRow->addWidget(buttons);
    root->addLayout(btnRow);

    connect(startBtn, &QPushButton::clicked, this, [this]() {
        m_startRequested = true;
        accept();
    });
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void AutoDjDialog::updateSourceVisibility() {
    const auto mode = static_cast<AutoDjConfig::SourceMode>(m_sourceCombo->currentData().toInt());
    m_collectionCombo->setEnabled(mode == AutoDjConfig::SourceMode::SpecificCollection);
    m_rulesWidget->setVisible(mode == AutoDjConfig::SourceMode::CustomRules);
}

void AutoDjDialog::addRuleRow(const RuleCondition& cond) {
    RuleRowWidget row;
    row.container = new QWidget(this);
    auto* hl = new QHBoxLayout(row.container);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(6);

    row.fieldCombo = new QComboBox(row.container);
    for (FilterField f : allFilterFields()) {
        row.fieldCombo->addItem(filterFieldDisplayName(f), static_cast<int>(f));
    }
    const int fIdx = row.fieldCombo->findData(static_cast<int>(cond.field));
    if (fIdx >= 0) row.fieldCombo->setCurrentIndex(fIdx);

    row.opCombo = new QComboBox(row.container);
    row.valueEdit = new QLineEdit(cond.value, row.container);
    row.valueEdit->setPlaceholderText("Value...");

    auto* removeBtn = new QPushButton("✕", row.container);
    removeBtn->setFixedWidth(28);

    hl->addWidget(row.fieldCombo, 2);
    hl->addWidget(row.opCombo, 2);
    hl->addWidget(row.valueEdit, 3);
    hl->addWidget(removeBtn);

    const int insertPos = qMax(0, m_rulesLayout->count() - 1);
    m_rulesLayout->insertWidget(insertPos, row.container);
    m_rows.append(row);

    updateOperatorsForRow(m_rows.last(), cond.op);

    QWidget* containerPtr = row.container;
    connect(row.fieldCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this, containerPtr](int) {
        for (auto& r : m_rows) {
            if (r.container == containerPtr) {
                updateOperatorsForRow(r, static_cast<FilterOp>(r.opCombo->currentData().toInt()));
                break;
            }
        }
    });

    connect(removeBtn, &QPushButton::clicked, this, [this, containerPtr]() {
        for (int i = 0; i < m_rows.size(); ++i) {
            if (m_rows[i].container == containerPtr) {
                m_rows[i].container->deleteLater();
                m_rows.removeAt(i);
                break;
            }
        }
    });
}

void AutoDjDialog::updateOperatorsForRow(RuleRowWidget& row, FilterOp preferredOp) {
    const auto f = static_cast<FilterField>(row.fieldCombo->currentData().toInt());
    row.opCombo->clear();
    for (FilterOp op : operatorsForField(f)) {
        row.opCombo->addItem(filterOpDisplayName(op), static_cast<int>(op));
    }
    const int idx = row.opCombo->findData(static_cast<int>(preferredOp));
    if (idx >= 0) row.opCombo->setCurrentIndex(idx);

    const bool needsValue = (f != FilterField::IsCompilation);
    row.valueEdit->setVisible(needsValue);
}

AutoDjConfig AutoDjDialog::resultConfig() const {
    AutoDjConfig cfg;
    cfg.sourceMode = static_cast<AutoDjConfig::SourceMode>(m_sourceCombo->currentData().toInt());
    cfg.specificCollectionId = m_collectionCombo->currentData().toString();
    cfg.customRules.logic = static_cast<RuleGroup::Logic>(m_logicCombo->currentData().toInt());
    for (const auto& r : m_rows) {
        RuleCondition cond;
        cond.field = static_cast<FilterField>(r.fieldCombo->currentData().toInt());
        cond.op = static_cast<FilterOp>(r.opCombo->currentData().toInt());
        cond.value = r.valueEdit->text().trimmed();
        if (cond.field == FilterField::IsCompilation || !cond.value.isEmpty()) {
            cfg.customRules.conditions.append(cond);
        }
    }
    cfg.queueDepth = m_depthSpin->value();
    cfg.genreMatchPercent = m_genreSlider->value();
    cfg.artistSpacing = m_artistSpacingSpin->value();
    cfg.avoidRecentDays = m_avoidRecentSpin->value();
    cfg.skipOneStar = m_skipOneStarChk->isChecked();
    cfg.preferHighRated = m_preferHighRatedChk->isChecked();
    return cfg;
}

void AutoDjDialog::onPreview() {
    m_previewList->clear();
    if (!m_db) return;

    const AutoDjConfig cfg = resultConfig();
    const auto tracks = pickAutoDjTracks(
        cfg, *m_db, m_collections, m_activeCollectionId, m_activeSearch,
        m_activeEffectiveRules, m_currentQueue, m_currentIndex, cfg.queueDepth);

    if (tracks.isEmpty()) {
        m_previewList->addItem("No matching tracks found with the current Auto-DJ rules.");
        return;
    }
    for (int i = 0; i < tracks.size(); ++i) {
        const Track& t = tracks[i];
        const QString g = t.genre.isEmpty() ? "" : QString(" [%1]").arg(t.genre);
        m_previewList->addItem(QString("%1. %2 — %3%4").arg(i + 1).arg(t.title, t.artist, g));
    }
}

} // namespace honeycomb
