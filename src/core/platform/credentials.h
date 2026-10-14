#ifndef ANIMEHUB_CORE_PLATFORM_CREDENTIALS_H
#define ANIMEHUB_CORE_PLATFORM_CREDENTIALS_H

#include <QByteArray>
#include <QString>

#include <optional>

// Secrets kept by the operating system (Windows Credential Manager), never in AnimeHub's files.
namespace animehub::platform {

struct Credential {
    QString user;
    QByteArray secret;
};

bool storeCredential(const QString& target, const Credential& credential);
[[nodiscard]] std::optional<Credential> readCredential(const QString& target);
void removeCredential(const QString& target);

} // namespace animehub::platform

#endif // ANIMEHUB_CORE_PLATFORM_CREDENTIALS_H
