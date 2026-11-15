#pragma once

#include "core/Collection.h"
#include "core/Track.h"
#include "db/Database.h"

#include <QDialog>
#include <QVector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QVBoxLayout;
class QLabel;

namespace honeycomb {

struct AutoDjConfig {
    enum class SourceMode {
        EntireLibrary = 0,
        ActiveCollection = 1,
        SpecificCollection = 2,
        CustomRules = 3
    };

    SourceMode sourceMode = SourceMode::ActiveCollection;
    QString specificCollectionId;
    RuleGroup customRules;

    int queueDepth = 5;
    int genreMatchPercent = 70; // 0..100
    int artistSpacing = 3;      // avoid repeating artist within N tracks
    int avoidRecentDays = 7;    // 0 = off
    bool skipOneStar = true;
    bool preferHighRated = true;

    static AutoDjConfig load();
    void save() const;
};

QVector<Track> pickAutoDjTracks(const AutoDjConfig& cfg,
                                const Database& db,
                                const QVector<Collection>& collections,
                                const QString& activeCollectionId,
                                const QString& activeSearch,
                                const RuleGroup& activeEffectiveRules,
                                const QVector<Track>& currentQueue,
                                int currentIndex,
                                int count);

class AutoDjDialog : public QDialog {
    Q_OBJECT
public:
    AutoDjDialog(const AutoDjConfig& initial,
                 const Database* db,
                 const QVector<Collection>& collections,
                 const QString& activeCollectionId,
                 const QString& activeSearch,
                 const RuleGroup& activeEffectiveRules,
                 const QVector<Track>& currentQueue,
                 int currentIndex,
                 QWidget* parent = nullptr);

    AutoDjConfig resultConfig() const;
    bool startRequested() const { return m_startRequested; }

private slots:
    void onPreview();

private:
    struct RuleRowWidget {
        QWidget* container = nullptr;
        QComboBox* fieldCombo = nullptr;
        QComboBox* opCombo = nullptr;
        QLineEdit* valueEdit = nullptr;
    };

    void addRuleRow(const RuleCondition& cond);
    void updateOperatorsForRow(RuleRowWidget& row, FilterOp preferredOp);
    void updateSourceVisibility();

    const Database* m_db = nullptr;
    QVector<Collection> m_collections;
    QString m_activeCollectionId;
    QString m_activeSearch;
    RuleGroup m_activeEffectiveRules;
    QVector<Track> m_currentQueue;
    int m_currentIndex = -1;
    bool m_startRequested = false;

    QComboBox* m_sourceCombo = nullptr;
    QComboBox* m_collectionCombo = nullptr;
    QWidget* m_rulesWidget = nullptr;
    QComboBox* m_logicCombo = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QVector<RuleRowWidget> m_rows;

    QSpinBox* m_depthSpin = nullptr;
    QSlider* m_genreSlider = nullptr;
    QLabel* m_genrePctLabel = nullptr;
    QSpinBox* m_artistSpacingSpin = nullptr;
    QSpinBox* m_avoidRecentSpin = nullptr;
    QCheckBox* m_skipOneStarChk = nullptr;
    QCheckBox* m_preferHighRatedChk = nullptr;

    QListWidget* m_previewList = nullptr;
};

} // namespace honeycomb
