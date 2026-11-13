#pragma once

#include <QJsonObject>
#include <QString>
#include <QVariantList>
#include <QVector>

namespace honeycomb {

enum class FilterField {
    AnyField,
    Title,
    Artist,
    AlbumArtist,
    Album,
    Genre,
    Year,
    Format,
    Bitrate,
    Path,
    Rating,
    PlayCount,
    IsCompilation,
    DateAddedDaysAgo
};

enum class FilterOp {
    Is,
    IsNot,
    Contains,
    DoesNotContain,
    StartsWith,
    IsAnyOf,       // semicolon-separated list
    IsNotAnyOf,    // semicolon-separated list
    GreaterThan,
    LessThan,
    GreaterOrEqual,
    LessOrEqual,
    IsTrue,
    IsFalse
};

struct RuleCondition {
    FilterField field = FilterField::Genre;
    FilterOp op = FilterOp::Is;
    QString value;

    QJsonObject toJson() const;
    static RuleCondition fromJson(const QJsonObject& obj);
};

struct RuleGroup {
    enum class Logic { All, Any };

    Logic logic = Logic::All;
    QVector<RuleCondition> conditions;
    QVector<RuleGroup> subGroups;

    bool isEmpty() const {
        return conditions.isEmpty() && subGroups.isEmpty();
    }

    QJsonObject toJson() const;
    static RuleGroup fromJson(const QJsonObject& obj);

    // Compiles this rule tree into a parameterized SQL WHERE clause (without leading "WHERE").
    // Returns "1=1" if empty.
    QString toSql(QVariantList& bindValues) const;
};

QString filterFieldToString(FilterField f);
FilterField filterFieldFromString(const QString& s);
QString filterFieldDisplayName(FilterField f);

QString filterOpToString(FilterOp op);
FilterOp filterOpFromString(const QString& s);
QString filterOpDisplayName(FilterOp op);

QVector<FilterField> allFilterFields();
QVector<FilterOp> operatorsForField(FilterField f);

} // namespace honeycomb
