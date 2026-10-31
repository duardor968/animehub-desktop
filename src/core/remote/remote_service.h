#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace animehub::api {
class ApiClient;
}
namespace animehub::library {
class LibraryService;
}

namespace animehub::remote {

// Whether this PC can record what a request asks for, known before accepting it: `Later` keeps
// the request held until the next look at the inbox, `Never` sends it back for attention.
enum class Readiness { Ready, Later, Never };

// The queue and the library record need the anime's row: one this PC has never seen is fetched
// from the API and remembered, so its title names the files and their folder.
void rememberAnime(api::ApiClient& api, library::LibraryService& library, const QString& slug,
                   QObject* context, std::function<void(Readiness)> done);

// Local only. Never serialize destination or engine connection to the API.
struct Profile {
    QString id;
    QString label;
    QString engine; // direct | jdownloader
    QString destination;
    QString connection;
    QString device;
    bool isDefault = false;
    QString audio = QStringLiteral("sub"); // local snapshot: sub | dub; never published
};
struct Submission {
    QString requestId;
    QString animeSlug;
    QList<double> episodes;
    Profile profile;
    int revision = 0;
};
struct Credentials {
    QString id;
    QByteArray token;
};
class RemoteService final : public QObject {
    Q_OBJECT
public:
    // Answers for the anime of a request that is about to be accepted (rememberAnime).
    using Prepare = std::function<void(const QString& slug, std::function<void(Readiness)> done)>;
    // enqueue must persist requestId atomically with the batch and succeed on a replay.
    using Enqueue = std::function<bool(const Submission&)>;
    using ReadCredentials = std::function<std::optional<Credentials>()>;
    using SaveCredentials = std::function<bool(const Credentials&)>;
    RemoteService(QNetworkAccessManager& network, QUrl apiBase, QString dataDirectory,
                  Prepare prepare, Enqueue enqueue, QObject* parent = nullptr);
    ~RemoteService() override;
    void setCredentialStore(ReadCredentials read, SaveCredentials save);
    // For a test profile (--data-dir): the device credential stays in the profile, never in
    // the system's store.
    void keepCredentialsInProfile();
    // Call before start, and whenever the local configuration changes. revision must increase
    // when the profiles change; only a new revision is published.
    void setProfiles(int revision, QList<Profile> profiles);
    // Only loads an existing device; never registers without user action. A registered device
    // keeps the API's live channel open: a request or a browser linked shows at once, and the
    // inbox is read every minute only while the channel is down.
    void start();
    void registerDevice(const QString& name);
    void generatePairingCode();
    // `quiet`, as a page watching the list does or the live channel asks: no busy state, no
    // error, and a change only when the list is another.
    void refreshLinks(bool quiet = false);
    void revokeLink(const QString& id);
    void poll();
    [[nodiscard]] bool registered() const {
        return !credentials_.id.isEmpty() && !credentials_.token.isEmpty();
    }
    [[nodiscard]] QString deviceName() const {
        return deviceName_;
    }
    [[nodiscard]] QString code() const {
        return code_;
    }
    [[nodiscard]] QString expiresAt() const {
        return expiresAt_;
    }
    [[nodiscard]] QString error() const {
        return error_;
    }
    // An action of the user is under way. Reading the inbox in the background is not one: it
    // never holds an action back.
    [[nodiscard]] bool busy() const {
        return operations_ > 0;
    }
    // No request to the API is under way, of any kind.
    [[nodiscard]] bool idle() const {
        return inFlight_ == 0;
    }
    // The live channel is open.
    [[nodiscard]] bool live() const {
        return live_;
    }
    [[nodiscard]] QJsonArray links() const;
    [[nodiscard]] static QString credentialTarget(const QUrl& apiBase,
                                                  const QString& dataDirectory);
signals:
    void changed();

private:
    using Response = std::function<void(int, const QJsonObject&)>;
    // User: busy while it runs, and its failure is the page's error. Background: only the error.
    // Quiet: neither.
    enum class Mode { User, Background, Quiet };
    void request(const QByteArray& method, const QString& path, const QJsonObject& body,
                 Response done, Mode mode = Mode::User);
    void publish(Mode mode, std::function<void(bool)> done);
    void begin();
    void openChannel();
    void readChannel();
    void channelClosed();
    void process(const QJsonObject& request);
    void reconcile(const QString& id);
    // Accepts the request, or with a `problem` sends it back for attention.
    void acknowledge(const QString& id, const QString& problem = {});
    void accepted(const QString& id);
    bool saveJournal();
    bool loadJournal();
    void fail(const QString& message);
    QNetworkAccessManager& network_;
    QUrl base_;
    QString journalPath_;
    Prepare prepare_;
    Enqueue enqueue_;
    ReadCredentials readCredentials_;
    SaveCredentials saveCredentials_;
    Credentials credentials_;
    QList<Profile> profiles_;
    int revision_ = 0;
    int publishedRevision_ = 0; // the last one the API took in this session
    QJsonObject journal_;
    QJsonObject links_;
    QTimer timer_;
    QNetworkReply* channel_ = nullptr;
    QByteArray channelBuffer_;
    QTimer reconnect_;
    int channelFailures_ = 0;
    bool live_ = false;
    bool started_ = false;
    QString deviceName_, code_, expiresAt_, error_;
    int operations_ = 0;
    int inFlight_ = 0;
    bool polling_ = false;
    bool pollAgain_ = false;
    bool journalValid_ = false;
    bool localAttention_ = false;
};
} // namespace animehub::remote
