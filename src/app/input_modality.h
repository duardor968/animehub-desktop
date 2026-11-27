#ifndef ANIMEHUB_APP_INPUT_MODALITY_H
#define ANIMEHUB_APP_INPUT_MODALITY_H

#include <QObject>
#include <QPointer>
#include <QQuickWindow>
#include <QtQmlIntegration>

namespace animehub::app {

// Whether the user is moving through the app with the keyboard, as a browser's :focus-visible
// decides it: a key press (not a shortcut) says yes, a press of the pointer says no. The focus
// frame shows only while it is yes, whoever drew it: ours (`FocusFrame`) or Fluent's, which stays
// around the control that last had keyboard focus until another one takes it. So after a press
// that leaves that control focused, it keeps its focus but no longer as keyboard focus.
// One per QML engine, like Theme: no global state.
// Must NOT be `final`: QML_ELEMENT / QML_SINGLETON instantiates QQmlElement<InputModality> which
// inherits from this class (MSVC C3246).
class InputModality : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool keyboard READ keyboard NOTIFY changed)
    // The focus reason for a move the app makes itself (a page opening on its title, a popup
    // focusing its first control): Tab while the keyboard is in use, so the frame follows; Mouse
    // after the pointer, so FluentWinUI3 keeps its focus frame hidden.
    Q_PROPERTY(int reason READ reason NOTIFY changed)

public:
    explicit InputModality(QObject* parent = nullptr);

    [[nodiscard]] bool keyboard() const {
        return keyboard_;
    }
    [[nodiscard]] int reason() const {
        return keyboard_ ? Qt::TabFocusReason : Qt::MouseFocusReason;
    }

signals:
    void changed();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void setKeyboard(bool keyboard);
    void scheduleSettle();
    // After a press: the focused control, if it still shows keyboard focus, keeps its focus
    // without the frame.
    void settle();

    QPointer<QQuickWindow> last_window_;
    bool keyboard_ = false;
    bool settling_ = false;
};

} // namespace animehub::app

#endif // ANIMEHUB_APP_INPUT_MODALITY_H
