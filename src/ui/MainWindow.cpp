#include "ui/MainWindow.h"

#include "render/OutputWindow.h"
#include "ui/WarpEditor.h"

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QImageReader>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScreen>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

namespace lumi {

namespace {

const char* const kProjectFilter = QT_TRANSLATE_NOOP("lumi::MainWindow", "LumiCanvas Project (*.lumi)");

QSize physicalSize(const QScreen* screen)
{
    return (QSizeF(screen->size()) * screen->devicePixelRatio()).toSize();
}

QString imageFilter()
{
    QStringList patterns;
    for (const QByteArray& format : QImageReader::supportedImageFormats())
        patterns << QStringLiteral("*.") + QString::fromLatin1(format);
    return MainWindow::tr("Images (%1)").arg(patterns.join(' '));
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_editor = new WarpEditor(&m_project, this);
    setCentralWidget(m_editor);

    createActions();
    createDocks();

    connect(&m_project, &Project::structureChanged, this, &MainWindow::onStructureChanged);
    connect(&m_project, &Project::surfaceChanged, this, &MainWindow::onSurfaceChanged);
    connect(m_editor, &WarpEditor::selectionChanged, this, &MainWindow::selectSurface);
    connect(qApp, &QGuiApplication::screenAdded, this, &MainWindow::refreshScreens);
    connect(qApp, &QGuiApplication::screenRemoved, this, &MainWindow::refreshScreens);

    m_lastImageDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    refreshScreens();
    newProject();
    updateOutputStatus();
    resize(1400, 860);
}

MainWindow::~MainWindow()
{
    // Child widgets outlive m_project; keep them from calling back during teardown.
    m_surfaceList->blockSignals(true);
    closeAllOutputs();
    m_engine.stop();
}

bool MainWindow::initialize()
{
    QString error;
    if (!m_engine.start(&error)) {
        QMessageBox::critical(this, tr("LumiCanvas"),
                              tr("Could not initialise Direct3D 11.\n\n%1").arg(error));
        return false;
    }
    statusBar()->showMessage(tr("GPU: %1").arg(m_engine.adapterName()));
    syncEngine();
    return true;
}

void MainWindow::createActions()
{
    auto addAction = [this](QMenu* menu, const QString& text, const QKeySequence& shortcut,
                            auto slot) {
        QAction* action = menu->addAction(text);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    addAction(fileMenu, tr("&New"), QKeySequence::New, &MainWindow::newProject);
    addAction(fileMenu, tr("&Open..."), QKeySequence::Open, &MainWindow::openProject);
    addAction(fileMenu, tr("&Save"), QKeySequence::Save, &MainWindow::saveProject);
    addAction(fileMenu, tr("Save &As..."), QKeySequence::SaveAs, &MainWindow::saveProjectAs);
    fileMenu->addSeparator();
    addAction(fileMenu, tr("E&xit"), QKeySequence::Quit, &QWidget::close);

    QMenu* surfaceMenu = menuBar()->addMenu(tr("&Surface"));
    QAction* addSurfaceAction =
        addAction(surfaceMenu, tr("&Add Surface"), QKeySequence(Qt::CTRL | Qt::Key_T), &MainWindow::addSurface);
    QAction* removeSurfaceAction =
        addAction(surfaceMenu, tr("&Remove Surface"), QKeySequence::Delete, &MainWindow::removeSurface);
    surfaceMenu->addSeparator();
    addAction(surfaceMenu, tr("Set &Image..."), QKeySequence(Qt::CTRL | Qt::Key_I), &MainWindow::chooseImage);

    QMenu* outputMenu = menuBar()->addMenu(tr("&Output"));
    QAction* openOutputAction = addAction(outputMenu, tr("&Open Output"),
                                          QKeySequence(Qt::CTRL | Qt::Key_Return), &MainWindow::openSelectedOutput);
    addAction(outputMenu, tr("&Close All Outputs"), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return),
              &MainWindow::closeAllOutputs);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About LumiCanvas"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About LumiCanvas"),
                           tr("<h3>LumiCanvas %1</h3><p>Projection mapping for Windows.</p>"
                              "<p>GPU: %2</p>").arg(QStringLiteral(LUMI_VERSION), m_engine.adapterName()));
    });

    QToolBar* toolBar = addToolBar(tr("Main"));
    toolBar->setObjectName("mainToolBar");
    toolBar->setMovable(false);
    toolBar->addAction(addSurfaceAction);
    toolBar->addAction(removeSurfaceAction);
    toolBar->addSeparator();
    toolBar->addAction(openOutputAction);
}

void MainWindow::createDocks()
{
    // Surfaces dock: list + properties.
    auto* surfacesPanel = new QWidget;
    auto* surfacesLayout = new QVBoxLayout(surfacesPanel);

    m_surfaceList = new QListWidget;
    connect(m_surfaceList, &QListWidget::currentRowChanged, this, &MainWindow::selectSurface);
    surfacesLayout->addWidget(m_surfaceList, 1);

    auto* listButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add"));
    auto* removeButton = new QPushButton(tr("Remove"));
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addSurface);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSurface);
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    surfacesLayout->addLayout(listButtons);

    m_propertiesBox = new QGroupBox(tr("Properties"));
    auto* form = new QFormLayout(m_propertiesBox);

    m_nameEdit = new QLineEdit;
    connect(m_nameEdit, &QLineEdit::textEdited, this,
            [this](const QString& text) { m_project.setName(m_selected, text); });
    form->addRow(tr("Name"), m_nameEdit);

    m_contentCombo = new QComboBox;
    m_contentCombo->addItems({tr("Test Pattern"), tr("Image")});
    connect(m_contentCombo, &QComboBox::activated, this, &MainWindow::onContentActivated);
    m_imageButton = new QPushButton(tr("Choose..."));
    connect(m_imageButton, &QPushButton::clicked, this, &MainWindow::chooseImage);
    auto* contentRow = new QHBoxLayout;
    contentRow->addWidget(m_contentCombo, 1);
    contentRow->addWidget(m_imageButton);
    form->addRow(tr("Content"), contentRow);

    m_imageLabel = new QLabel;
    m_imageLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_imageLabel->setWordWrap(true);
    form->addRow(QString(), m_imageLabel);

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacityLabel = new QLabel;
    m_opacityLabel->setMinimumWidth(40);
    connect(m_opacitySlider, &QSlider::valueChanged, this,
            [this](int value) { m_project.setOpacity(m_selected, value / 100.0f); });
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityLabel);
    form->addRow(tr("Opacity"), opacityRow);

    m_visibleCheck = new QCheckBox(tr("Visible"));
    connect(m_visibleCheck, &QCheckBox::toggled, this,
            [this](bool on) { m_project.setVisible(m_selected, on); });
    form->addRow(QString(), m_visibleCheck);

    surfacesLayout->addWidget(m_propertiesBox);

    auto* surfacesDock = new QDockWidget(tr("Surfaces"), this);
    surfacesDock->setObjectName("surfacesDock");
    surfacesDock->setFeatures(QDockWidget::DockWidgetMovable);
    surfacesDock->setWidget(surfacesPanel);
    addDockWidget(Qt::LeftDockWidgetArea, surfacesDock);

    // Outputs dock.
    auto* outputsPanel = new QWidget;
    auto* outputsLayout = new QVBoxLayout(outputsPanel);

    m_screenCombo = new QComboBox;
    connect(m_screenCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onScreenSelected);
    outputsLayout->addWidget(new QLabel(tr("Display")));
    outputsLayout->addWidget(m_screenCombo);

    m_windowedCheck = new QCheckBox(tr("Windowed (for testing)"));
    outputsLayout->addWidget(m_windowedCheck);

    auto* outputButtons = new QHBoxLayout;
    auto* openButton = new QPushButton(tr("Open Output"));
    auto* closeButton = new QPushButton(tr("Close All"));
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openSelectedOutput);
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::closeAllOutputs);
    outputButtons->addWidget(openButton);
    outputButtons->addWidget(closeButton);
    outputsLayout->addLayout(outputButtons);

    m_outputStatus = new QLabel;
    outputsLayout->addWidget(m_outputStatus);
    outputsLayout->addStretch();

    auto* outputsDock = new QDockWidget(tr("Outputs"), this);
    outputsDock->setObjectName("outputsDock");
    outputsDock->setFeatures(QDockWidget::DockWidgetMovable);
    outputsDock->setWidget(outputsPanel);
    addDockWidget(Qt::LeftDockWidgetArea, outputsDock);
}

// ---------------------------------------------------------------------------
// Project file handling

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_project.clear();
    m_project.addSurface();
    m_projectPath.clear();
    selectSurface(0);
    setWindowModified(false);
    updateTitle();
}

void MainWindow::openProject()
{
    if (!maybeSave())
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Open Project"),
                                                      QFileInfo(m_projectPath).absolutePath(),
                                                      tr(kProjectFilter));
    if (path.isEmpty())
        return;

    QString error;
    QStringList missing;
    if (!m_project.load(path, &error, &missing)) {
        QMessageBox::critical(this, tr("Open Project"), error);
        return;
    }
    m_projectPath = path;
    selectSurface(m_project.count() > 0 ? 0 : -1);
    setWindowModified(false);
    updateTitle();
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, tr("Open Project"),
                             tr("These images could not be loaded:\n\n%1").arg(missing.join('\n')));
    }
}

bool MainWindow::saveProject()
{
    if (m_projectPath.isEmpty())
        return saveProjectAs();
    QString error;
    if (!m_project.save(m_projectPath, &error)) {
        QMessageBox::critical(this, tr("Save Project"), error);
        return false;
    }
    setWindowModified(false);
    statusBar()->showMessage(tr("Saved %1").arg(QDir::toNativeSeparators(m_projectPath)), 3000);
    return true;
}

bool MainWindow::saveProjectAs()
{
    QString path = QFileDialog::getSaveFileName(this, tr("Save Project"),
                                                m_projectPath.isEmpty() ? tr("Untitled.lumi") : m_projectPath,
                                                tr(kProjectFilter));
    if (path.isEmpty())
        return false;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".lumi");
    m_projectPath = path;
    updateTitle();
    return saveProject();
}

bool MainWindow::maybeSave()
{
    if (!isWindowModified())
        return true;
    const auto answer = QMessageBox::warning(
        this, tr("LumiCanvas"), tr("The project has unsaved changes. Save them?"),
        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save)
        return saveProject();
    return answer == QMessageBox::Discard;
}

void MainWindow::updateTitle()
{
    const QString name = m_projectPath.isEmpty() ? tr("Untitled") : QFileInfo(m_projectPath).fileName();
    setWindowTitle(QStringLiteral("%1[*] - LumiCanvas").arg(name));
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave()) {
        event->ignore();
        return;
    }
    closeAllOutputs();
    event->accept();
}

// ---------------------------------------------------------------------------
// Surfaces

void MainWindow::addSurface()
{
    selectSurface(m_project.addSurface());
}

void MainWindow::removeSurface()
{
    if (!m_project.isValidIndex(m_selected))
        return;
    const int index = m_selected;
    m_project.removeSurface(index);
    selectSurface(std::min(index, m_project.count() - 1));
}

void MainWindow::selectSurface(int index)
{
    if (!m_project.isValidIndex(index))
        index = -1;
    m_selected = index;
    m_editor->setSelectedSurface(index);
    {
        const QSignalBlocker blocker(m_surfaceList);
        m_surfaceList->setCurrentRow(index);
    }
    updatePropertyPanel();
}

void MainWindow::chooseImage()
{
    if (!m_project.isValidIndex(m_selected))
        return;
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose Image"), m_lastImageDir, imageFilter());
    if (!path.isEmpty()) {
        m_lastImageDir = QFileInfo(path).absolutePath();
        if (!m_project.setImage(m_selected, path)) {
            QMessageBox::warning(this, tr("Choose Image"),
                                 tr("Could not load %1").arg(QDir::toNativeSeparators(path)));
        }
    }
    updatePropertyPanel(); // reverts the content combo if the dialog was cancelled
}

void MainWindow::onContentActivated(int index)
{
    if (!m_project.isValidIndex(m_selected))
        return;
    if (index == 0) {
        m_project.setContent(m_selected, ContentType::TestPattern);
    } else if (!m_project.setContent(m_selected, ContentType::Image)) {
        chooseImage(); // no image yet
    }
}

void MainWindow::onStructureChanged()
{
    {
        const QSignalBlocker blocker(m_surfaceList);
        m_surfaceList->clear();
        for (int i = 0; i < m_project.count(); ++i) {
            auto* item = new QListWidgetItem(m_project.surface(i).name, m_surfaceList);
            item->setForeground(surfaceColor(i));
        }
    }
    selectSurface(std::min(m_selected, m_project.count() - 1));
    setWindowModified(true);
    syncEngine();
}

void MainWindow::onSurfaceChanged(int index)
{
    if (QListWidgetItem* item = m_surfaceList->item(index))
        item->setText(m_project.surface(index).name);
    if (index == m_selected)
        updatePropertyPanel();
    setWindowModified(true);
    syncEngine();
}

void MainWindow::updatePropertyPanel()
{
    const bool valid = m_project.isValidIndex(m_selected);
    m_propertiesBox->setEnabled(valid);

    const QSignalBlocker b1(m_nameEdit);
    const QSignalBlocker b2(m_contentCombo);
    const QSignalBlocker b3(m_opacitySlider);
    const QSignalBlocker b4(m_visibleCheck);

    if (!valid) {
        m_nameEdit->clear();
        m_imageLabel->clear();
        m_opacityLabel->clear();
        return;
    }

    const Surface& s = m_project.surface(m_selected);
    if (m_nameEdit->text() != s.name) // don't disturb the cursor while typing
        m_nameEdit->setText(s.name);
    const bool isImage = s.content == ContentType::Image;
    m_contentCombo->setCurrentIndex(isImage ? 1 : 0);
    if (isImage) {
        const QString file = QFileInfo(s.imagePath).fileName();
        m_imageLabel->setText(s.image.isNull()
                                  ? tr("%1 (missing)").arg(file)
                                  : QStringLiteral("%1  (%2 × %3)").arg(file).arg(s.image.width()).arg(s.image.height()));
    } else {
        m_imageLabel->clear();
    }
    m_opacitySlider->setValue(qRound(s.opacity * 100));
    m_opacityLabel->setText(QStringLiteral("%1%").arg(qRound(s.opacity * 100)));
    m_visibleCheck->setChecked(s.visible);
}

void MainWindow::syncEngine()
{
    auto scene = std::make_shared<Scene>();
    for (int i = 0; i < m_project.count(); ++i) {
        const Surface& s = m_project.surface(i);
        if (!s.visible)
            continue;
        SceneSurface out;
        out.corners = s.corners;
        if (s.content == ContentType::Image)
            out.image = s.image;
        const QColor color = surfaceColor(i);
        out.tint = {color.redF(), color.greenF(), color.blueF(), 1.0f};
        out.opacity = s.opacity;
        scene->surfaces.push_back(std::move(out));
    }
    m_engine.setScene(std::move(scene));
}

// ---------------------------------------------------------------------------
// Outputs

void MainWindow::refreshScreens()
{
    const QString current = m_screenCombo->currentData().toString();
    const QList<QScreen*> screens = QGuiApplication::screens();
    {
        const QSignalBlocker blocker(m_screenCombo);
        m_screenCombo->clear();
        int preferred = -1;
        for (int i = 0; i < screens.size(); ++i) {
            QScreen* screen = screens[i];
            const QSize size = physicalSize(screen);
            QString label = QStringLiteral("%1  (%2 × %3)").arg(screen->name()).arg(size.width()).arg(size.height());
            if (screen == QGuiApplication::primaryScreen())
                label += tr("  - primary");
            m_screenCombo->addItem(label, screen->name());
            if (screen->name() == current)
                preferred = i;
        }
        // Projectors are usually secondary displays.
        for (int i = 0; preferred < 0 && i < screens.size(); ++i) {
            if (screens[i] != QGuiApplication::primaryScreen())
                preferred = i;
        }
        m_screenCombo->setCurrentIndex(std::max(preferred, 0));
    }
    onScreenSelected();
}

QScreen* MainWindow::selectedScreen() const
{
    const QString name = m_screenCombo->currentData().toString();
    for (QScreen* screen : QGuiApplication::screens()) {
        if (screen->name() == name)
            return screen;
    }
    return QGuiApplication::primaryScreen();
}

void MainWindow::onScreenSelected()
{
    QScreen* screen = selectedScreen();
    if (!screen)
        return;
    m_editor->setCanvasSize(physicalSize(screen));
    // Fullscreen on the primary display would cover the editor.
    m_windowedCheck->setChecked(screen == QGuiApplication::primaryScreen());
}

void MainWindow::openSelectedOutput()
{
    if (QScreen* screen = selectedScreen())
        openOutput(screen, !m_windowedCheck->isChecked());
}

void MainWindow::openOutput(QScreen* screen, bool fullscreen)
{
    auto* window = new OutputWindow(screen, fullscreen);
    connect(window, &OutputWindow::closeRequested, this, &MainWindow::closeOutput);
    m_outputs.insert(window, m_engine.addOutput(window->hwnd()));
    updateOutputStatus();
}

void MainWindow::closeOutput(OutputWindow* window)
{
    const auto it = m_outputs.find(window);
    if (it == m_outputs.end())
        return;
    m_engine.removeOutput(it.value());
    m_outputs.erase(it);
    window->destroy();
    window->deleteLater();
    updateOutputStatus();
}

void MainWindow::closeAllOutputs()
{
    const QList<OutputWindow*> windows = m_outputs.keys();
    for (OutputWindow* window : windows)
        closeOutput(window);
}

void MainWindow::updateOutputStatus()
{
    m_outputStatus->setText(m_outputs.isEmpty() ? tr("No outputs open")
                                                : tr("%n output(s) open  ·  Esc on an output closes it",
                                                     nullptr, int(m_outputs.size())));
}

} // namespace lumi
