#include "remote_view.h"
#include <QJsonArray>
namespace animehub::app {
RemoteView::RemoteView(remote::RemoteService& service, QObject* parent)
    : QObject(parent), service_(service) {
    connect(&service_, &remote::RemoteService::changed, this, &RemoteView::changed);
}
QVariantList RemoteView::links() const {
    return service_.links().toVariantList();
}
} // namespace animehub::app
