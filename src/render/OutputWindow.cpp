#include "render/OutputWindow.h"

#include <QCloseEvent>
#include <QKeyEvent>
#include <QScreen>

#include <windows.h>

namespace lumi {

OutputWindow::OutputWindow(QScreen* screen, bool fullscreen)
{
    setSurfaceType(QSurface::Direct3DSurface);
    setTitle(tr("LumiCanvas Output - %1").arg(screen->name()));
    setScreen(screen);

    if (fullscreen) {
        setFlags(Qt::Window | Qt::FramelessWindowHint);
        setGeometry(screen->geometry());
        setCursor(Qt::BlankCursor);
        showFullScreen();
    } else {
        const QRect available = screen->availableGeometry();
        const QSize size = QSize(1280, 720).boundedTo(available.size() * 0.8);
        setGeometry(QRect(QPoint(), size).translated(available.center() - QPoint(size.width() / 2, size.height() / 2)));
        show();
    }
}

void OutputWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        emit closeRequested(this);
        return;
    }
    emit keyPressed(event->keyCombination());
}

void OutputWindow::closeEvent(QCloseEvent* event)
{
    // Never close directly: the swap chain must be released before the HWND goes away.
    event->ignore();
    emit closeRequested(this);
}

} // namespace lumi
