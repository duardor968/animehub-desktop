#ifndef ANIMEHUB_APP_CARD_MODEL_H
#define ANIMEHUB_APP_CARD_MODEL_H

#include <QAbstractListModel>
#include <QUrl>
#include <QtQmlIntegration>

namespace animehub::app {

// A poster with a title and one line under it: shelves of the library home and the catalog.
struct Card {
    QString slug;
    QString title;
    QUrl posterUrl;
    QString caption;
    double progress = -1; // < 0 hides the bar
    double episode = -1;  // the episode a click acts on, when there is one
    bool fresh = false;   // has episodes the user has not seen yet
    // What the poster's own button does with the episode: "play", "download" or nothing.
    QString action;
};

class CardModel : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("Provided by the application")
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    enum Role {
        SlugRole = Qt::UserRole + 1,
        TitleRole,
        PosterRole,
        CaptionRole,
        ProgressRole,
        EpisodeRole,
        FreshRole,
        ActionRole,
    };

    using QAbstractListModel::QAbstractListModel;

    void setCards(QList<Card> cards);
    void append(const QList<Card>& cards);
    [[nodiscard]] const QList<Card>& cards() const {
        return cards_;
    }

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

signals:
    void countChanged();

private:
    QList<Card> cards_;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_CARD_MODEL_H
