#include "api_types.h"

namespace animehub::api {

QUrl AnimeSummary::backdropFor(int width) const {
    const ImageCandidate* best = nullptr;
    for (const auto& candidate : backdropCandidates) {
        if (candidate.width >= width && (best == nullptr || candidate.width < best->width)) {
            best = &candidate;
        }
    }
    if (best == nullptr) {
        for (const auto& candidate : backdropCandidates) {
            if (best == nullptr || candidate.width > best->width) {
                best = &candidate;
            }
        }
    }
    return best != nullptr ? best->url : backdropUrl;
}

QString providerKey(Provider provider) {
    switch (provider) {
    case Provider::Mega:
        return QStringLiteral("MEGA");
    case Provider::Pixeldrain:
        return QStringLiteral("PIXELDRAIN");
    case Provider::Mp4Upload:
        return QStringLiteral("MP4UPLOAD");
    case Provider::OneFichier:
        return QStringLiteral("ONE_FICHIER");
    }
    return {};
}

std::optional<Provider> providerFromKey(QStringView key) {
    for (const auto provider :
         {Provider::Mega, Provider::Pixeldrain, Provider::Mp4Upload, Provider::OneFichier}) {
        if (key == providerKey(provider)) {
            return provider;
        }
    }
    return std::nullopt;
}

QString providerLabel(Provider provider) {
    switch (provider) {
    case Provider::Mega:
        return QStringLiteral("MEGA");
    case Provider::Pixeldrain:
        return QStringLiteral("Pixeldrain");
    case Provider::Mp4Upload:
        return QStringLiteral("MP4Upload");
    case Provider::OneFichier:
        return QStringLiteral("1Fichier");
    }
    return {};
}

QString statusKey(AnimeStatus status) {
    switch (status) {
    case AnimeStatus::Airing:
        return QStringLiteral("AIRING");
    case AnimeStatus::Finished:
        return QStringLiteral("FINISHED");
    case AnimeStatus::Upcoming:
        return QStringLiteral("UPCOMING");
    case AnimeStatus::Unknown:
        break;
    }
    return QStringLiteral("UNKNOWN");
}

AnimeStatus statusFromKey(QStringView key) {
    for (const auto status : {AnimeStatus::Airing, AnimeStatus::Finished, AnimeStatus::Upcoming}) {
        if (key == statusKey(status)) {
            return status;
        }
    }
    return AnimeStatus::Unknown;
}

} // namespace animehub::api
