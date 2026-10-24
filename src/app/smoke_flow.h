#pragma once

#include "library_model.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>

namespace animehub::app {
// Runs only against an explicit smoke profile; events stay inside this Qt process.
inline bool runSmokeFlow(QQuickWindow* window, LibraryModel* model, const QString& flow) {
    const auto settle = [] {
        QEventLoop loop;
        QTimer::singleShot(260, &loop, &QEventLoop::quit);
        loop.exec();
    };
    const auto check = [](bool ok, const char* message) {
        if (ok)
            qInfo("Flow PASS: %s", message);
        else
            qCritical("Flow FAIL: %s", message);
        return ok;
    };
    const auto named = [window](const char* name) {
        return window->findChild<QObject*>(QString::fromLatin1(name));
    };
    const auto key = [window](int code) {
        QKeyEvent press(QEvent::KeyPress, code, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        QKeyEvent release(QEvent::KeyRelease, code, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &release);
    };
    const auto descendants = [window] {
        QList<QObject*> result{window};
        QSet<QObject*> seen{window};
        for (qsizetype i = 0; i < result.size(); ++i) {
            auto children = result[i]->children();
            if (auto* item = qobject_cast<QQuickItem*>(result[i]))
                for (auto* child : item->childItems())
                    children.append(child);
            for (auto* child : children)
                if (!seen.contains(child)) {
                    seen.insert(child);
                    result.append(child);
                }
        }
        return result;
    };
    const auto byText = [&descendants](const QString& text) -> QObject* {
        for (auto* object : descendants())
            if (object->property("text").toString() == text && object->property("visible").toBool())
                return object;
        return nullptr;
    };
    if (flow == u"create" || flow == u"add" || flow == u"ignore") {
        auto* dialog = named("franchiseReviewDialog");
        auto* destination = named("candidateDestination");
        auto* title = qobject_cast<QQuickItem*>(named("candidateNewTitle"));
        auto* confirm = named("candidateConfirm");
        if (!check(dialog && destination && title && confirm &&
                       !model->pendingCandidates().isEmpty(),
                   "candidate fixture and controls"))
            return false;
        const auto count = model->pendingCandidates().size();
        if (!QMetaObject::invokeMethod(dialog, "show"))
            return false;
        settle();
        if (!check(!confirm->property("enabled").toBool(), "unselected destination cannot submit"))
            return false;
        QMetaObject::invokeMethod(dialog, "submit");
        if (!check(model->pendingCandidates().size() == count,
                   "invalid submit preserves candidate"))
            return false;
        qobject_cast<QQuickItem*>(destination)->forceActiveFocus();
        auto* focus = window->activeFocusItem();
        qInfo() << "Flow focus before Tab" << focus;
        key(Qt::Key_Tab);
        qInfo() << "Flow focus after Tab" << window->activeFocusItem();
        if (!check(window->activeFocusItem() && window->activeFocusItem() != focus,
                   "Tab advances dialog focus"))
            return false;
        if (flow == u"ignore") {
            auto* ignore = named("candidateIgnore");
            if (!ignore || !QMetaObject::invokeMethod(ignore, "clicked"))
                return false;
        } else if (flow == u"create") {
            destination->setProperty("currentIndex", destination->property("count").toInt() - 1);
            settle();
            title->setProperty("text", QStringLiteral("   "));
            if (!check(!confirm->property("enabled").toBool(), "blank franchise cannot submit"))
                return false;
            title->setProperty("text", QStringLiteral("Smoke flow franchise"));
            title->forceActiveFocus();
            key(Qt::Key_Return);
        } else {
            destination->setProperty("currentIndex", 1);
            if (!check(confirm->property("enabled").toBool(), "existing franchise enables submit"))
                return false;
            if (!QMetaObject::invokeMethod(confirm, "clicked"))
                return false;
        }
        settle();
        if (!check(model->pendingCandidates().size() == count - 1,
                   "action resolves exactly one candidate"))
            return false;
        if (flow == u"create" &&
            !check(model->franchise().value(QStringLiteral("title")).toString() ==
                       u"Smoke flow franchise",
                   "Enter creates selected franchise"))
            return false;
        QMetaObject::invokeMethod(dialog, "close");
    } else if (flow == u"archive") {
        model->setFilter(LibraryModel::Filter::AllAnime);
        model->setArchivedOnly(false);
        settle();
        const QString slug = model->slugAt(0);
        const int count = model->archivedCount();
        QObject* list = nullptr;
        for (auto* object : descendants())
            if (object->metaObject()->indexOfSignal("removeRequested(QString,QString)") >= 0) {
                list = object;
                break;
            }
        if (!check(list && !slug.isEmpty(), "archive fixture and list"))
            return false;
        if (!QMetaObject::invokeMethod(list, "removeRequested", Q_ARG(QString, slug),
                                       Q_ARG(QString, slug)))
            return false;
        settle();
        auto* archive = byText(QStringLiteral("Archivar"));
        qInfo() << "Flow archive" << slug << count << archive;
        if (!archive || !QMetaObject::invokeMethod(archive, "clicked"))
            return false;
        qInfo() << "Flow archive result" << model->archivedCount() << model->error();
        if (!check(model->archivedCount() == count + 1, "archive dialog archives one record"))
            return false;
        model->setArchivedOnly(true);
        settle();
        QObject* restore = nullptr;
        for (auto* object : descendants()) {
            if (object->property("text").toString() != u"Restaurar a la biblioteca")
                continue;
            for (auto* context = QQmlEngine::contextForObject(object); context;
                 context = context->parentContext()) {
                auto* row = context->objectForName(QStringLiteral("row"));
                if (row && row->property("slug").toString() == slug) {
                    restore = object;
                    break;
                }
            }
            if (restore)
                break;
        }
        if (!check(restore != nullptr, "archived row exposes restore action"))
            return false;
        if (!QMetaObject::invokeMethod(restore, "triggered"))
            return false;
        if (!check(model->archivedCount() == count, "restore action restores archive count"))
            return false;
        model->setArchivedOnly(false);
        if (!check(model->indexOf(slug) >= 0, "restored work returns to library"))
            return false;
    } else if (flow == u"settings") {
        auto* dialog = named("settingsDialog");
        if (!dialog || !QMetaObject::invokeMethod(dialog, "open"))
            return false;
        settle();
        QObject* flick = nullptr;
        for (auto* object : dialog->findChildren<QObject*>())
            if (object->property("contentY").isValid() &&
                object->property("contentHeight").toDouble() >
                    object->property("height").toDouble()) {
                flick = object;
                break;
            }
        if (!check(flick != nullptr, "settings overflows at tested size"))
            return false;
        auto* item = qobject_cast<QQuickItem*>(flick);
        if (!item)
            return false;
        item->forceActiveFocus();
        key(Qt::Key_Down);
        key(Qt::Key_Down);
        settle();
        if (!check(flick->property("contentY").toDouble() > 0, "Down scrolls settings"))
            return false;
        QMetaObject::invokeMethod(dialog, "close");
    } else
        return false;
    settle();
    return check(!window->isVisible(), "native window stayed hidden");
}
} // namespace animehub::app
