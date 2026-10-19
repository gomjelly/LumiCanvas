#pragma once

#include <QWindow>

#include <windows.h>

class QScreen;

namespace lumi {

// Native window that a projector output renders into. The render engine draws
// into it through a DXGI swap chain; Qt only manages placement and input.
class OutputWindow : public QWindow {
    Q_OBJECT

public:
    OutputWindow(QScreen* screen, bool fullscreen);

    HWND hwnd() { return reinterpret_cast<HWND>(winId()); }

signals:
    // The owner must detach the window from the render engine before deleting it.
    void closeRequested(lumi::OutputWindow* window);
    // Keys the window does not handle itself, so the operator can step from the projector.
    void keyPressed(QKeyCombination key);

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
};

} // namespace lumi
