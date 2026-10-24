#include "credentials.h"

#include <QStringList>

// GLib's headers name a member "signals", a keyword macro of Qt; nothing here declares signals.
#undef signals
#include <libsecret/secret.h>

#include <memory>

// Items of the desktop's keyring (Secret Service: GNOME Keyring, KWallet): the target and the
// user are attributes, the secret is the item's value. A locked keyring asks to be unlocked.
namespace animehub::platform {
namespace {

const SecretSchema* schema() {
    static const SecretSchema kSchema = [] {
        SecretSchema value{};
        value.name = ANIMEHUB_APP_ID ".Credential";
        value.flags = SECRET_SCHEMA_NONE;
        value.attributes[0] = {"target", SECRET_SCHEMA_ATTRIBUTE_STRING};
        value.attributes[1] = {"user", SECRET_SCHEMA_ATTRIBUTE_STRING};
        return value;
    }();
    return &kSchema;
}

struct GErrorFree {
    void operator()(GError* error) const {
        g_error_free(error);
    }
};
struct SecretValueUnref {
    void operator()(SecretValue* value) const {
        secret_value_unref(value);
    }
};
struct HashTableUnref {
    void operator()(GHashTable* table) const {
        g_hash_table_unref(table);
    }
};
struct ItemsFree {
    void operator()(GList* items) const {
        g_list_free_full(items, g_object_unref);
    }
};
using Error = std::unique_ptr<GError, GErrorFree>;
using Items = std::unique_ptr<GList, ItemsFree>;

// The users saved for `target`, to drop those a new store replaces.
QStringList users(const QByteArray& target) {
    GError* raw = nullptr;
    const Items items(secret_password_search_sync(schema(), SECRET_SEARCH_ALL, nullptr, &raw,
                                                  "target", target.constData(), nullptr));
    const Error error(raw);
    QStringList found;
    for (auto* item = items.get(); item != nullptr; item = item->next) {
        const std::unique_ptr<GHashTable, HashTableUnref> attributes(
            secret_retrievable_get_attributes(static_cast<SecretRetrievable*>(item->data)));
        found.append(QString::fromUtf8(
            static_cast<const char*>(g_hash_table_lookup(attributes.get(), "user"))));
    }
    return found;
}

} // namespace

bool storeCredential(const QString& target, const Credential& credential) {
    const auto target_name = target.toUtf8();
    const auto user_name = credential.user.toUtf8();
    const auto earlier = users(target_name);
    const std::unique_ptr<SecretValue, SecretValueUnref> value(secret_value_new(
        credential.secret.constData(), credential.secret.size(), "application/octet-stream"));
    const auto label = (QStringLiteral("AnimeHub: ") + target).toUtf8();
    GError* raw = nullptr;
    const bool stored = secret_password_store_binary_sync(
        schema(), SECRET_COLLECTION_DEFAULT, label.constData(), value.get(), nullptr, &raw,
        "target", target_name.constData(), "user", user_name.constData(), nullptr);
    const Error error(raw);
    if (!stored) {
        return false;
    }
    // One item per target, as one Windows credential: another user's would stay beside it.
    for (const auto& user : earlier) {
        if (user != credential.user) {
            const auto other = user.toUtf8();
            GError* ignored = nullptr;
            secret_password_clear_sync(schema(), nullptr, &ignored, "target",
                                       target_name.constData(), "user", other.constData(), nullptr);
            const Error cleared(ignored);
        }
    }
    return true;
}

std::optional<Credential> readCredential(const QString& target) {
    const auto target_name = target.toUtf8();
    GError* raw = nullptr;
    const Items items(secret_password_search_sync(
        schema(), static_cast<SecretSearchFlags>(SECRET_SEARCH_LOAD_SECRETS | SECRET_SEARCH_UNLOCK),
        nullptr, &raw, "target", target_name.constData(), nullptr));
    const Error error(raw);
    if (!items) {
        return std::nullopt;
    }
    auto* item = static_cast<SecretRetrievable*>(items->data);
    GError* retrieve_raw = nullptr;
    const std::unique_ptr<SecretValue, SecretValueUnref> value(
        secret_retrievable_retrieve_secret_sync(item, nullptr, &retrieve_raw));
    const Error retrieve_error(retrieve_raw);
    if (!value) {
        return std::nullopt;
    }
    gsize length = 0;
    const auto* bytes = secret_value_get(value.get(), &length);
    const std::unique_ptr<GHashTable, HashTableUnref> attributes(
        secret_retrievable_get_attributes(item));
    return Credential{.user = QString::fromUtf8(
                          static_cast<const char*>(g_hash_table_lookup(attributes.get(), "user"))),
                      .secret = QByteArray(bytes, static_cast<qsizetype>(length))};
}

void removeCredential(const QString& target) {
    const auto target_name = target.toUtf8();
    GError* raw = nullptr;
    secret_password_clear_sync(schema(), nullptr, &raw, "target", target_name.constData(), nullptr);
    const Error error(raw);
}

} // namespace animehub::platform
