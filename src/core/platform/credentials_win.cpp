#include "credentials.h"

#include <qt_windows.h>

#include <wincred.h>

namespace animehub::platform {

bool storeCredential(const QString& target, const Credential& credential) {
    auto target_name = target.toStdWString();
    auto user_name = credential.user.toStdWString();
    CREDENTIALW entry{};
    entry.Type = CRED_TYPE_GENERIC;
    entry.TargetName = target_name.data();
    entry.UserName = user_name.data();
    entry.CredentialBlobSize = static_cast<DWORD>(credential.secret.size());
    entry.CredentialBlob =
        reinterpret_cast<LPBYTE>(const_cast<char*>(credential.secret.constData()));
    entry.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&entry, 0) != FALSE;
}

std::optional<Credential> readCredential(const QString& target) {
    PCREDENTIALW entry = nullptr;
    const auto target_name = target.toStdWString();
    if (CredReadW(target_name.c_str(), CRED_TYPE_GENERIC, 0, &entry) == FALSE) {
        return std::nullopt;
    }
    Credential credential{
        .user = entry->UserName != nullptr ? QString::fromWCharArray(entry->UserName) : QString(),
        .secret = QByteArray(reinterpret_cast<const char*>(entry->CredentialBlob),
                             static_cast<qsizetype>(entry->CredentialBlobSize))};
    CredFree(entry);
    return credential;
}

void removeCredential(const QString& target) {
    const auto target_name = target.toStdWString();
    CredDeleteW(target_name.c_str(), CRED_TYPE_GENERIC, 0);
}

} // namespace animehub::platform
