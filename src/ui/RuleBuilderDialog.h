#pragma once

#include "core/Collection.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QLineEdit>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVector>

namespace honeycomb {

class RuleBuilderDialog : public QDialog {
    Q_OBJECT
public:
    explicit RuleBuilderDialog(const Collection& initial, QWidget* parent = nullptr);

    Collection resultCollection() const;

private:
    struct RuleRowWidget {
        QWidget* container = nullptr;
        QComboBox* fieldCombo = nullptr;
        QComboBox* opCombo = nullptr;
        QLineEdit* valueEdit = nullptr;
    };

    struct SubGroupWidget {
        QWidget* container = nullptr;
        QComboBox* logicCombo = nullptr;
        QVBoxLayout* rowsLayout = nullptr;
        QVector<RuleRowWidget> rows;
    };

    void addRuleRow(QVBoxLayout* targetLayout, QVector<RuleRowWidget>& targetRows, const RuleCondition& cond);
    void addSubGroup(const RuleGroup& sub);
    void updateOperatorsForRow(RuleRowWidget& row, FilterOp preferredOp);

    Collection m_base;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_typeCombo = nullptr;
    QComboBox* m_logicCombo = nullptr;
    QComboBox* m_viewModeCombo = nullptr;
    QComboBox* m_sortCombo = nullptr;
    QSpinBox* m_artSizeSpin = nullptr;
    QCheckBox* m_colBrowserCheck = nullptr;
    QSpinBox* m_limitSpin = nullptr;
    QComboBox* m_limitOrderCombo = nullptr;
    QVBoxLayout* m_rulesLayout = nullptr;
    QVector<RuleRowWidget> m_rows;
    QVector<SubGroupWidget> m_subGroups;
};

} // namespace honeycomb
