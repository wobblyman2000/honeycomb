#include "FilterRule.h"

#include <QDateTime>
#include <QJsonArray>
#include <QStringList>

namespace honeycomb {

namespace {

bool isNumericField(FilterField f) {
    return f == FilterField::Year || f == FilterField::Bitrate ||
           f == FilterField::Rating || f == FilterField::PlayCount ||
           f == FilterField::DateAddedDaysAgo;
}

QString columnForField(FilterField f) {
    switch (f) {
    case FilterField::Title: return "title";
    case FilterField::Artist: return "artist";
    case FilterField::AlbumArtist: return "album_artist";
    case FilterField::Album: return "album";
    case FilterField::Genre: return "genre";
    case FilterField::Year: return "year";
    case FilterField::Format: return "format";
    case FilterField::Bitrate: return "bitrate";
    case FilterField::Path: return "path";
    case FilterField::Rating: return "rating";
    case FilterField::PlayCount: return "play_count";
    case FilterField::IsCompilation: return "is_compilation";
    case FilterField::DateAddedDaysAgo: return "date_added";
    case FilterField::AnyField: return "";
    }
    return "title";
}

QString escapeLike(QString s) {
    s.replace('\\', "\\\\");
    s.replace('%', "\\%");
    s.replace('_', "\\_");
    return s;
}

QString compileCondition(const RuleCondition& c, QVariantList& bindValues) {
    if (c.field == FilterField::IsCompilation) {
        if (c.op == FilterOp::IsTrue) {
            return "(is_compilation = 1 OR LOWER(album_artist) = 'various artists')";
        }
        return "(is_compilation = 0 AND LOWER(album_artist) != 'various artists')";
    }

    if (c.field == FilterField::DateAddedDaysAgo) {
        bool ok = false;
        const int64_t days = c.value.toLongLong(&ok);
        if (!ok) return "1=1";
        const int64_t cutoff = QDateTime::currentSecsSinceEpoch() - (days * 86400LL);
        bindValues.append(static_cast<qlonglong>(cutoff));
        if (c.op == FilterOp::LessOrEqual || c.op == FilterOp::LessThan) {
            return "date_added >= ?";
        }
        return "date_added < ?";
    }

    if (c.field == FilterField::AnyField) {
        const QStringList cols = {"title", "artist", "album_artist", "album", "genre", "path"};
        QStringList parts;
        const QString pattern = QString("%%1%").arg(escapeLike(c.value));
        for (const auto& col : cols) {
            parts.append(QString("%1 LIKE ? ESCAPE '\\'").arg(col));
            bindValues.append(pattern);
        }
        const QString joined = "(" + parts.join(" OR ") + ")";
        if (c.op == FilterOp::DoesNotContain || c.op == FilterOp::IsNot) {
            return "(NOT " + joined + ")";
        }
        return joined;
    }

    const QString col = columnForField(c.field);

    if (isNumericField(c.field)) {
        const qlonglong num = c.value.toLongLong();
        bindValues.append(num);
        switch (c.op) {
        case FilterOp::Is: return col + " = ?";
        case FilterOp::IsNot: return col + " != ?";
        case FilterOp::GreaterThan: return col + " > ?";
        case FilterOp::LessThan: return col + " < ?";
        case FilterOp::GreaterOrEqual: return col + " >= ?";
        case FilterOp::LessOrEqual: return col + " <= ?";
        default: return col + " = ?";
        }
    }

    // Text fields
    switch (c.op) {
    case FilterOp::Is:
        bindValues.append(c.value);
        return col + " = ? COLLATE NOCASE";
    case FilterOp::IsNot:
        bindValues.append(c.value);
        return col + " != ? COLLATE NOCASE";
    case FilterOp::Contains:
        bindValues.append(QString("%%1%").arg(escapeLike(c.value)));
        return col + " LIKE ? ESCAPE '\\'";
    case FilterOp::DoesNotContain:
        bindValues.append(QString("%%1%").arg(escapeLike(c.value)));
        return col + " NOT LIKE ? ESCAPE '\\'";
    case FilterOp::StartsWith:
        bindValues.append(QString("%1%").arg(escapeLike(c.value)));
        return col + " LIKE ? ESCAPE '\\'";
    case FilterOp::IsAnyOf:
    case FilterOp::IsNotAnyOf: {
        const QStringList items = c.value.split(';', Qt::SkipEmptyParts);
        if (items.isEmpty()) return "1=1";
        QStringList placeholders;
        for (const QString& item : items) {
            placeholders.append("?");
            bindValues.append(item.trimmed());
        }
        const QString opStr = (c.op == FilterOp::IsAnyOf) ? "IN" : "NOT IN";
        return QString("%1 COLLATE NOCASE %2 (%3)").arg(col, opStr, placeholders.join(", "));
    }
    default:
        bindValues.append(c.value);
        return col + " = ? COLLATE NOCASE";
    }
}

} // namespace

QJsonObject RuleCondition::toJson() const {
    QJsonObject obj;
    obj["field"] = filterFieldToString(field);
    obj["op"] = filterOpToString(op);
    obj["value"] = value;
    return obj;
}

RuleCondition RuleCondition::fromJson(const QJsonObject& obj) {
    RuleCondition c;
    c.field = filterFieldFromString(obj["field"].toString());
    c.op = filterOpFromString(obj["op"].toString());
    c.value = obj["value"].toString();
    return c;
}

QJsonObject RuleGroup::toJson() const {
    QJsonObject obj;
    obj["logic"] = (logic == Logic::All) ? "all" : "any";
    QJsonArray condArr;
    for (const auto& c : conditions) condArr.append(c.toJson());
    obj["conditions"] = condArr;

    QJsonArray subArr;
    for (const auto& g : subGroups) subArr.append(g.toJson());
    obj["subGroups"] = subArr;
    return obj;
}

RuleGroup RuleGroup::fromJson(const QJsonObject& obj) {
    RuleGroup g;
    g.logic = (obj["logic"].toString("all") == "any") ? Logic::Any : Logic::All;
    for (const auto& v : obj["conditions"].toArray()) {
        g.conditions.append(RuleCondition::fromJson(v.toObject()));
    }
    for (const auto& v : obj["subGroups"].toArray()) {
        g.subGroups.append(RuleGroup::fromJson(v.toObject()));
    }
    return g;
}

QString RuleGroup::toSql(QVariantList& bindValues) const {
    QStringList clauses;
    for (const auto& c : conditions) {
        clauses.append(compileCondition(c, bindValues));
    }
    for (const auto& sub : subGroups) {
        if (!sub.isEmpty()) {
            clauses.append(sub.toSql(bindValues));
        }
    }
    if (clauses.isEmpty()) return "1=1";
    const QString joiner = (logic == Logic::All) ? " AND " : " OR ";
    return "(" + clauses.join(joiner) + ")";
}

QString filterFieldToString(FilterField f) {
    switch (f) {
    case FilterField::AnyField: return "any_field";
    case FilterField::Title: return "title";
    case FilterField::Artist: return "artist";
    case FilterField::AlbumArtist: return "album_artist";
    case FilterField::Album: return "album";
    case FilterField::Genre: return "genre";
    case FilterField::Year: return "year";
    case FilterField::Format: return "format";
    case FilterField::Bitrate: return "bitrate";
    case FilterField::Path: return "path";
    case FilterField::Rating: return "rating";
    case FilterField::PlayCount: return "play_count";
    case FilterField::IsCompilation: return "is_compilation";
    case FilterField::DateAddedDaysAgo: return "date_added_days";
    }
    return "genre";
}

FilterField filterFieldFromString(const QString& s) {
    if (s == "any_field") return FilterField::AnyField;
    if (s == "title") return FilterField::Title;
    if (s == "artist") return FilterField::Artist;
    if (s == "album_artist") return FilterField::AlbumArtist;
    if (s == "album") return FilterField::Album;
    if (s == "genre") return FilterField::Genre;
    if (s == "year") return FilterField::Year;
    if (s == "format") return FilterField::Format;
    if (s == "bitrate") return FilterField::Bitrate;
    if (s == "path") return FilterField::Path;
    if (s == "rating") return FilterField::Rating;
    if (s == "play_count") return FilterField::PlayCount;
    if (s == "is_compilation") return FilterField::IsCompilation;
    if (s == "date_added_days") return FilterField::DateAddedDaysAgo;
    return FilterField::Genre;
}

QString filterFieldDisplayName(FilterField f) {
    switch (f) {
    case FilterField::AnyField: return "Any Field";
    case FilterField::Title: return "Title";
    case FilterField::Artist: return "Artist";
    case FilterField::AlbumArtist: return "Album Artist";
    case FilterField::Album: return "Album";
    case FilterField::Genre: return "Genre";
    case FilterField::Year: return "Year";
    case FilterField::Format: return "Format";
    case FilterField::Bitrate: return "Bitrate (kbps)";
    case FilterField::Path: return "File Path";
    case FilterField::Rating: return "Rating (0-5)";
    case FilterField::PlayCount: return "Play Count";
    case FilterField::IsCompilation: return "Various Artists / Compilation";
    case FilterField::DateAddedDaysAgo: return "Date Added (Days Ago)";
    }
    return "Genre";
}

QString filterOpToString(FilterOp op) {
    switch (op) {
    case FilterOp::Is: return "is";
    case FilterOp::IsNot: return "is_not";
    case FilterOp::Contains: return "contains";
    case FilterOp::DoesNotContain: return "not_contains";
    case FilterOp::StartsWith: return "starts_with";
    case FilterOp::IsAnyOf: return "is_any_of";
    case FilterOp::IsNotAnyOf: return "is_not_any_of";
    case FilterOp::GreaterThan: return "gt";
    case FilterOp::LessThan: return "lt";
    case FilterOp::GreaterOrEqual: return "gte";
    case FilterOp::LessOrEqual: return "lte";
    case FilterOp::IsTrue: return "is_true";
    case FilterOp::IsFalse: return "is_false";
    }
    return "is";
}

FilterOp filterOpFromString(const QString& s) {
    if (s == "is") return FilterOp::Is;
    if (s == "is_not") return FilterOp::IsNot;
    if (s == "contains") return FilterOp::Contains;
    if (s == "not_contains") return FilterOp::DoesNotContain;
    if (s == "starts_with") return FilterOp::StartsWith;
    if (s == "is_any_of") return FilterOp::IsAnyOf;
    if (s == "is_not_any_of") return FilterOp::IsNotAnyOf;
    if (s == "gt") return FilterOp::GreaterThan;
    if (s == "lt") return FilterOp::LessThan;
    if (s == "gte") return FilterOp::GreaterOrEqual;
    if (s == "lte") return FilterOp::LessOrEqual;
    if (s == "is_true") return FilterOp::IsTrue;
    if (s == "is_false") return FilterOp::IsFalse;
    return FilterOp::Is;
}

QString filterOpDisplayName(FilterOp op) {
    switch (op) {
    case FilterOp::Is: return "is";
    case FilterOp::IsNot: return "is not";
    case FilterOp::Contains: return "contains";
    case FilterOp::DoesNotContain: return "does not contain";
    case FilterOp::StartsWith: return "starts with";
    case FilterOp::IsAnyOf: return "is any of (;)";
    case FilterOp::IsNotAnyOf: return "is not any of (;)";
    case FilterOp::GreaterThan: return "is greater than";
    case FilterOp::LessThan: return "is less than";
    case FilterOp::GreaterOrEqual: return "is at least";
    case FilterOp::LessOrEqual: return "is at most";
    case FilterOp::IsTrue: return "is Yes";
    case FilterOp::IsFalse: return "is No";
    }
    return "is";
}

QVector<FilterField> allFilterFields() {
    return {
        FilterField::Genre,
        FilterField::AlbumArtist,
        FilterField::Artist,
        FilterField::Album,
        FilterField::Title,
        FilterField::IsCompilation,
        FilterField::Year,
        FilterField::Rating,
        FilterField::PlayCount,
        FilterField::DateAddedDaysAgo,
        FilterField::Format,
        FilterField::Bitrate,
        FilterField::Path,
        FilterField::AnyField
    };
}

QVector<FilterOp> operatorsForField(FilterField f) {
    if (f == FilterField::IsCompilation) {
        return {FilterOp::IsTrue, FilterOp::IsFalse};
    }
    if (f == FilterField::DateAddedDaysAgo) {
        return {FilterOp::LessOrEqual, FilterOp::GreaterThan};
    }
    if (isNumericField(f)) {
        return {FilterOp::Is, FilterOp::IsNot, FilterOp::GreaterOrEqual,
                FilterOp::LessOrEqual, FilterOp::GreaterThan, FilterOp::LessThan};
    }
    if (f == FilterField::AnyField) {
        return {FilterOp::Contains, FilterOp::DoesNotContain};
    }
    return {FilterOp::Is, FilterOp::IsNot, FilterOp::Contains,
            FilterOp::DoesNotContain, FilterOp::StartsWith,
            FilterOp::IsAnyOf, FilterOp::IsNotAnyOf};
}

} // namespace honeycomb
