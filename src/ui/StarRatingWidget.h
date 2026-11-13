#pragma once

#include <QHBoxLayout>
#include <QPushButton>
#include <QWidget>

namespace honeycomb {

class StarRatingWidget : public QWidget {
    Q_OBJECT
public:
    explicit StarRatingWidget(QWidget* parent = nullptr) : QWidget(parent) {
        auto* h = new QHBoxLayout(this);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(2);
        for (int i = 1; i <= 5; ++i) {
            auto* btn = new QPushButton("☆", this);
            btn->setFixedSize(22, 22);
            btn->setFlat(true);
            btn->setStyleSheet("QPushButton { border: none; background: transparent; font-size: 16px; padding: 0; color: #f5a623; }");
            connect(btn, &QPushButton::clicked, this, [this, i]() {
                const int next = (m_rating == i) ? 0 : i; // clicking active star clears to 0
                setRating(next);
                emit ratingChanged(next);
            });
            h->addWidget(btn);
            m_stars[i - 1] = btn;
        }
        h->addStretch();
    }

    void setRating(int rating) {
        m_rating = qBound(0, rating, 5);
        for (int i = 0; i < 5; ++i) {
            m_stars[i]->setText(i < m_rating ? "★" : "☆");
        }
    }

    int rating() const { return m_rating; }

signals:
    void ratingChanged(int newRating);

private:
    QPushButton* m_stars[5]{};
    int m_rating = 0;
};

} // namespace honeycomb
