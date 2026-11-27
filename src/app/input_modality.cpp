#include "input_modality.h"

#include <QFocusEvent>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <QTimer>
#include <QVariant>

namespace animehub::app {
namespace {

bool modifierOnly(int key) {
    switch (key) {
    case Qt::Key_Shift:
    case Qt::Key_Control:
    case Qt::Key_Meta:
    case Qt::Key_Alt:
    case Qt::Key_AltGr:
    case Qt::Key_CapsLock:
    case Qt::Key_NumLock:
    case Qt::Key_Super_L:
    case Qt::Key_Super_R:
        return true;
    default:
        return false;
    }
}

// A text field shows where the caret is however it got focus: nothing to settle.
bool takesText(const QQuickItem* item) {
    return item->inherits("QQuickTextInput") || item->inherits("QQuickTextEdit");
}

} // namespace

InputModality::InputModality(QObject* parent) : QObject(parent) {
    if (auto* app = QCoreApplication::instance()) {
        app->installEventFilter(this);
    }
}

bool InputModality::eventFilter(QObject* watched, QEvent* event) {
    // Every event passes through here once per receiver; only those meant for a window count.
    if (!watched->isWindowType()) {
        return false;
    }
    if (auto* quick = qobject_cast<QQuickWindow*>(watched)) {
        last_window_ = quick;
    } else if (auto* focus = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow())) {
        last_window_ = focus;
    }
    switch (event->type()) {
    case QEvent::KeyPress: {
        const auto* key = static_cast<QKeyEvent*>(event);
        // A shortcut (Ctrl+F) is not moving through the app; Shift+Tab is.
        const auto shortcut =
            key->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
        if (!modifierOnly(key->key()) && !shortcut) {
            setKeyboard(true);
        }
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::TouchBegin:
    case QEvent::TabletPress:
        setKeyboard(false);
        scheduleSettle();
        break;
    case QEvent::FocusIn:
        // Re-activating the window in pointer mode must not bring back a keyboard focus frame.
        if (!keyboard_ &&
            static_cast<QFocusEvent*>(event)->reason() == Qt::ActiveWindowFocusReason) {
            scheduleSettle();
        }
        break;
    case QEvent::WindowActivate:
        if (!keyboard_) {
            scheduleSettle();
        }
        break;
    default:
        break;
    }
    return false;
}

void InputModality::setKeyboard(bool keyboard) {
    if (keyboard_ != keyboard) {
        keyboard_ = keyboard;
        emit changed();
    }
}

void InputModality::scheduleSettle() {
    // Once the press or activation has done its work: it may have moved focus itself.
    if (!settling_) {
        settling_ = true;
        QTimer::singleShot(0, this, &InputModality::settle);
    }
}

void InputModality::settle() {
    settling_ = false;
    if (keyboard_) {
        return;
    }
    auto* window = qobject_cast<QQuickWindow*>(QGuiApplication::focusWindow());
    if (!window) {
        window = last_window_.data();
    }
    auto* item = window ? window->activeFocusItem() : nullptr;
    if (!item || takesText(item)) {
        return;
    }
    const bool visual_focus = item->property("visualFocus").toBool();
    const QVariant focus_reason = item->property("focusReason");
    const bool other_focus_reason =
        focus_reason.isValid() && focus_reason.value<Qt::FocusReason>() == Qt::OtherFocusReason;
    if (!visual_focus && !other_focus_reason) {
        return;
    }
    // Fluent draws its focus frame on keyboard focus and OtherFocusReason, not on
    // MouseFocusReason; giving the same focus back with the pointer's reason hides the frame
    // while keeping active focus intact.
    item->setFocus(false, Qt::OtherFocusReason);
    item->forceActiveFocus(Qt::MouseFocusReason);
}

} // namespace animehub::app
