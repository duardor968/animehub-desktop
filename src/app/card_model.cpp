#include "card_model.h"

namespace animehub::app {

void CardModel::setCards(QList<Card> cards) {
    const bool same_size = cards.size() == cards_.size();
    const bool same_rows =
        same_size && std::ranges::equal(cards, cards_, {}, &Card::slug, &Card::slug);
    if (same_rows) {
        // Same posters in the same order: refresh in place so views keep scroll and focus.
        cards_ = std::move(cards);
        if (!cards_.isEmpty()) {
            emit dataChanged(index(0), index(static_cast<int>(cards_.size()) - 1));
        }
        return;
    }
    beginResetModel();
    cards_ = std::move(cards);
    endResetModel();
    emit countChanged();
}

void CardModel::append(const QList<Card>& cards) {
    if (cards.isEmpty()) {
        return;
    }
    beginInsertRows({}, static_cast<int>(cards_.size()),
                    static_cast<int>(cards_.size() + cards.size()) - 1);
    cards_.append(cards);
    endInsertRows();
    emit countChanged();
}

int CardModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(cards_.size());
}

QVariant CardModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= cards_.size()) {
        return {};
    }
    const auto& card = cards_.at(index.row());
    switch (role) {
    case SlugRole:
        return card.slug;
    case TitleRole:
    case Qt::DisplayRole:
        return card.title;
    case PosterRole:
        return card.posterUrl;
    case CaptionRole:
        return card.caption;
    case ProgressRole:
        return card.progress;
    case EpisodeRole:
        return card.episode;
    case FreshRole:
        return card.fresh;
    case ActionRole:
        return card.action;
    default:
        return {};
    }
}

QHash<int, QByteArray> CardModel::roleNames() const {
    return {{SlugRole, "slug"},       {TitleRole, "title"},       {PosterRole, "posterUrl"},
            {CaptionRole, "caption"}, {ProgressRole, "progress"}, {EpisodeRole, "episode"},
            {FreshRole, "fresh"},     {ActionRole, "action"}};
}

} // namespace animehub::app
