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
    // The library list, found by one of its signals.
    const auto listWith = [&descendants](const char* signal) -> QObject* {
        for (auto* object : descendants())
            if (object->metaObject()->indexOfSignal(signal) >= 0)
                return object;
        return nullptr;
    };
    if (flow == u"create") {
        // "Crear franquicia…" from a work's menu in the list.
        model->setFilter(LibraryModel::Filter::All);
        model->setGrouped(false);
        model->setArchivedOnly(false);
        settle();
        const QString slug = model->slugAt(0);
        auto* list = listWith("createFranchiseRequested(QString,QString)");
        auto* dialog = named("createFranchiseDialog");
        auto* title = qobject_cast<QQuickItem*>(named("createFranchiseTitle"));
        auto* confirm = named("createFranchiseConfirm");
        if (!check(list && dialog && title && confirm && !slug.isEmpty(),
                   "create fixture and controls"))
            return false;
        if (!QMetaObject::invokeMethod(list, "createFranchiseRequested", Q_ARG(QString, slug),
                                       Q_ARG(QString, slug)))
            return false;
        settle();
        if (!check(dialog->property("visible").toBool(), "menu entry opens the dialog"))
            return false;
        title->setProperty("text", QStringLiteral("   "));
        if (!check(!confirm->property("enabled").toBool(), "blank franchise cannot submit"))
            return false;
        title->forceActiveFocus();
        key(Qt::Key_Return);
        settle();
        if (!check(dialog->property("visible").toBool(), "Enter on a blank name keeps the dialog"))
            return false;
        auto* focus = window->activeFocusItem();
        key(Qt::Key_Tab);
        if (!check(window->activeFocusItem() && window->activeFocusItem() != focus,
                   "Tab advances dialog focus"))
            return false;
        title->setProperty("text", QStringLiteral("Smoke flow franchise"));
        title->forceActiveFocus();
        key(Qt::Key_Return);
        settle();
        if (!check(model->franchise().value(QStringLiteral("title")).toString() ==
                       u"Smoke flow franchise",
                   "Enter creates the franchise"))
            return false;
        // Closing, not closed: a hidden window may never run the exit transition to its end.
        if (!check(!dialog->property("opened").toBool(), "dialog closes once created"))
            return false;
    } else if (flow == u"archive") {
        model->setFilter(LibraryModel::Filter::All);
        model->setGrouped(false);
        model->setArchivedOnly(false);
        settle();
        const QString slug = model->slugAt(0);
        const int count = model->archivedCount();
        auto* list = listWith("removeRequested(QString,QString)");
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
        // Ajustes opens on its categories, Down walks them, and at the tested size (1100x720 is
        // the reference) each page fits without scrolling.
        auto* dialog = named("settingsDialog");
        auto* categories = qobject_cast<QQuickItem*>(named("settingsCategories"));
        auto* pages = qobject_cast<QQuickItem*>(named("settingsPages"));
        if (!dialog || !categories || !pages || !QMetaObject::invokeMethod(dialog, "open"))
            return false;
        // Its opening transition runs at the hidden window's own pace.
        const auto opened = [dialog, categories] {
            return dialog->property("opened").toBool() && categories->hasActiveFocus();
        };
        for (int wait = 0; wait < 10 && !opened(); ++wait)
            settle();
        if (!check(opened(), "categories hold the focus on opening"))
            return false;
        const int count = categories->property("count").toInt();
        if (!check(count == pages->childItems().size(), "one page per category"))
            return false;
        key(Qt::Key_Tab);
        if (!check(pages->isAncestorOf(window->activeFocusItem()), "Tab goes on to the page"))
            return false;
        categories->forceActiveFocus();
        for (int index = 0; index < count; ++index) {
            settle();
            auto* page = pages->childItems().value(index);
            if (!check(pages->property("currentIndex").toInt() == index && page->isVisible(),
                       "the arrow keys open each category"))
                return false;
            const double content = page->property("contentHeight").toDouble();
            const double room = page->property("availableHeight").toDouble();
            qInfo("Flow settings page %d: %.0f of %.0f px", index, content, room);
            if (!check(content <= room, "the page fits without scrolling"))
                return false;
            key(Qt::Key_Down);
        }
        QMetaObject::invokeMethod(dialog, "close");
    } else
        return false;
    settle();
    return check(!window->isVisible(), "native window stayed hidden");
}
} // namespace animehub::app
