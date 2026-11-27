#include "app/input_modality.h"

#include <QCoreApplication>
#include <QFocusEvent>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>

#include <memory>

using animehub::app::InputModality;

// The focus frame follows the keyboard only, as a browser's :focus-visible: a click away from a
// control the keyboard reached leaves it focused, but without the frame.
class InputModalityTest final : public QObject {
    Q_OBJECT

private slots:
    void theFrameFollowsTheKeyboardOnly() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Window {
                width: 300; height: 200; visible: true
                property alias button: button
                Button { id: button; x: 10; y: 10; text: "Descargar todo" }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* button = root->property("button").value<QQuickItem*>();
        QVERIFY(button);

        InputModality modality;
        QVERIFY(!modality.keyboard());
        QCOMPARE(modality.reason(), int(Qt::MouseFocusReason));

        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(modality.keyboard());
        QCOMPARE(modality.reason(), int(Qt::TabFocusReason));
        button->forceActiveFocus(Qt::FocusReason(modality.reason()));
        QVERIFY(button->property("visualFocus").toBool());

        QTest::mouseClick(window, Qt::LeftButton, {}, QPoint(250, 150));
        QVERIFY(!modality.keyboard());
        QTRY_VERIFY(!button->property("visualFocus").toBool());
        QVERIFY(button->hasActiveFocus());
        // A shortcut, or a modifier alone, is not moving through the app; Shift+Tab is.
        QTest::keyClick(window, Qt::Key_F, Qt::ControlModifier);
        QVERIFY(!modality.keyboard());
        QTest::keyClick(window, Qt::Key_Shift);
        QVERIFY(!modality.keyboard());
        QTest::keyClick(window, Qt::Key_Tab, Qt::ShiftModifier);
        QVERIFY(modality.keyboard());
    }

    // Re-activating the window in pointer mode settles any lingering keyboard focus frame, while
    // keyboard mode keeps it; destroying the last window before settle() runs stays safe.
    void activeWindowFocusSettlesInPointerModeAndFallbackIsSafe() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Window {
                width: 300; height: 200; visible: true
                property alias button: button
                property alias other: other
                Button { id: button; x: 10; y: 10; text: "Descargar todo" }
                Button { id: other; x: 10; y: 60; text: "Ver" }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* button = root->property("button").value<QQuickItem*>();
        auto* other = root->property("other").value<QQuickItem*>();
        QVERIFY(button);
        QVERIFY(other);

        InputModality modality;
        QVERIFY(!modality.keyboard());

        // Pointer mode: ActiveWindowFocusReason must not leave a focus frame on the control.
        button->forceActiveFocus(Qt::TabFocusReason);
        QVERIFY(button->property("visualFocus").toBool());
        QFocusEvent reactivated(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(window, &reactivated);
        QTRY_VERIFY(!button->property("visualFocus").toBool());
        QVERIFY(button->hasActiveFocus());

        // Keyboard mode: Tab moves visual focus to the next control, and ActiveWindowFocusReason
        // preserves that keyboard focus frame.
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(modality.keyboard());
        QVERIFY(other->hasActiveFocus());
        QVERIFY(other->property("visualFocus").toBool());
        QFocusEvent keyboard_reactivated(QEvent::FocusIn, Qt::ActiveWindowFocusReason);
        QCoreApplication::sendEvent(window, &keyboard_reactivated);
        QCoreApplication::processEvents();
        QVERIFY(other->property("visualFocus").toBool());

        // Trigger settle via pointer press and destroy the window before singleShot(0) fires:
        // QPointer<QQuickWindow> last_window_ safely resolves to null.
        QTest::mousePress(window, Qt::LeftButton, {}, QPoint(250, 150));
        QVERIFY(!modality.keyboard());
        root.reset();
        QCoreApplication::processEvents();
    }

    // Opening a popup or moving focus on mouse release with OtherFocusReason must settle to
    // MouseFocusReason in pointer mode so FluentWinUI3 does not draw a keyboard focus frame,
    // while subsequent Tab navigation keeps working normally.
    void otherFocusReasonSettlesToMouseOnReleaseWithoutBreakingTab() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import QtQuick.Controls
            Window {
                width: 320; height: 220; visible: true
                property alias trigger: trigger
                property alias target: target
                Button {
                    id: trigger
                    x: 16; y: 16; width: 120; height: 36
                    text: "Limitar"
                    onClicked: target.forceActiveFocus(Qt.OtherFocusReason)
                }
                Switch {
                    id: target
                    x: 16; y: 80
                    text: "Activar"
                }
            }
        )",
                          QUrl());
        std::unique_ptr<QObject> root(component.create());
        QVERIFY2(root, qPrintable(component.errorString()));
        auto* window = qobject_cast<QQuickWindow*>(root.get());
        QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->requestActivate();
        QVERIFY(QTest::qWaitForWindowActive(window));
        auto* trigger = root->property("trigger").value<QQuickItem*>();
        auto* target = root->property("target").value<QQuickItem*>();
        QVERIFY(trigger);
        QVERIFY(target);

        InputModality modality;
        QVERIFY(!modality.keyboard());
        QCOMPARE(modality.reason(), int(Qt::MouseFocusReason));

        // Simulate a real click where MouseButtonPress settles before MouseButtonRelease fires
        // onClicked and focuses `target` with OtherFocusReason.
        const QPoint click_point(40, 32);
        QTest::mousePress(window, Qt::LeftButton, {}, click_point);
        QCoreApplication::processEvents();
        QTest::mouseRelease(window, Qt::LeftButton, {}, click_point);
        QVERIFY(!modality.keyboard());
        QVERIFY(target->hasActiveFocus());
        QTRY_COMPARE(target->property("focusReason").value<Qt::FocusReason>(),
                     Qt::MouseFocusReason);
        QVERIFY(target->hasActiveFocus());
        QVERIFY(!target->property("visualFocus").toBool());

        // Switching back to the keyboard with Tab restores keyboard modality and visual focus.
        QTest::keyClick(window, Qt::Key_Tab);
        QVERIFY(modality.keyboard());
        QCOMPARE(modality.reason(), int(Qt::TabFocusReason));
        QVERIFY(trigger->hasActiveFocus());
        QVERIFY(trigger->property("visualFocus").toBool());
        QCOMPARE(trigger->property("focusReason").value<Qt::FocusReason>(), Qt::TabFocusReason);
    }
};

QTEST_MAIN(InputModalityTest)
#include "input_modality_test.moc"
