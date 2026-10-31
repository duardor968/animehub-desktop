#include "remote_view.h"

#include "display.h"

#include <QClipboard>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>

namespace animehub::app {
RemoteView::RemoteView(remote::RemoteService& service, QObject* parent)
    : QObject(parent), service_(service) {
    connect(&service_, &remote::RemoteService::changed, this, &RemoteView::update);
}

QSet<QString> RemoteView::linkIds() const {
    QSet<QString> ids;
    for (const auto& link : service_.links())
        ids.insert(link.toObject().value(QStringLiteral("id")).toString());
    return ids;
}

void RemoteView::update() {
    const auto ids = linkIds();
    if (service_.code() != code_seen_) {
        code_seen_ = service_.code();
        ids_before_ = ids;
    } else if (!code_seen_.isEmpty() && code_seen_ != used_code_ &&
               !(ids - ids_before_).isEmpty()) {
        used_code_ = code_seen_;
        emit linked();
    }
    emit changed();
}

void RemoteView::copyCode() const {
    if (const auto value = code(); !value.isEmpty())
        QGuiApplication::clipboard()->setText(value);
}

QString RemoteView::expiresText() const {
    return display::time(QDateTime::fromString(service_.expiresAt(), Qt::ISODateWithMs));
}

QVariantList RemoteView::links() const {
    QVariantList links;
    for (const auto& value : service_.links()) {
        const auto link = value.toObject();
        links.append(QVariantMap{
            {QStringLiteral("id"), link.value(QStringLiteral("id")).toString()},
            {QStringLiteral("name"), link.value(QStringLiteral("name")).toString()},
            {QStringLiteral("since"),
             display::date(QDateTime::fromString(link.value(QStringLiteral("createdAt")).toString(),
                                                 Qt::ISODateWithMs))}});
    }
    return links;
}
} // namespace animehub::app
