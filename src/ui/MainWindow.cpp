#include "ui/MainWindow.h"

#include "core/ImageLoader.h"
#include "render/OutputWindow.h"
#include "ui/GenerateStepsDialog.h"
#include "ui/WarpEditor.h"

#include <QAbstractSpinBox>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QProgressDialog>
#include <QPromise>
#include <QPushButton>
#include <QScreen>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QStandardPaths>
#include <QStatusBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace lumi {

namespace {

const char* const kProjectFilter = QT_TRANSLATE_NOOP("lumi::MainWindow", "LumiCanvas Project (*.lumi)");

QSize physicalSize(const QScreen* screen)
{
    return (QSizeF(screen->size()) * screen->devicePixelRatio()).toSize();
}

QString imageFilter()
{
    return MainWindow::tr("Images (%1)").arg(imageNameFilters().join(' '));
}

QToolButton* actionButton(QAction* action)
{
    auto* button = new QToolButton;
    button->setDefaultAction(action);
    button->setToolButtonStyle(Qt::ToolButtonTextOnly);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return button;
}

} // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    m_editor = new WarpEditor(&m_project, &m_history, this);
    setCentralWidget(m_editor);
    setAcceptDrops(true);
    // Clear edges between the menu bar, the side panels and the editor; the plain
    // Fusion frame is nearly invisible in the dark theme.
    setStyleSheet(QStringLiteral(
        "QMenuBar { border-bottom: 1px solid #4a4a52; }"
        "QDockWidget::title { background: #2d2d33; padding: 5px 8px; border-bottom: 1px solid #4a4a52; }"
        "QMainWindow::separator { background: #44444c; width: 4px; height: 4px; }"
        "QMainWindow::separator:hover { background: #5b82b8; }"));

    createActions();
    loadKeyBindings();
    createDocks();
    updateKeyHint();
    qApp->installEventFilter(this); // the peek key needs both press and release, in any window

    m_statusLabel = new QLabel;
    statusBar()->addPermanentWidget(m_statusLabel);

    connect(&m_project, &Project::structureChanged, this, &MainWindow::onStructureChanged);
    connect(&m_project, &Project::surfaceChanged, this, &MainWindow::onSurfaceChanged);
    connect(&m_project, &Project::guideChanged, this, [this] {
        setWindowModified(true);
        updateGuidePanel();
        syncEngine();
    });
    updateGuidePanel();
    Sequence* sequence = &m_project.sequence();
    connect(sequence, &Sequence::loaded, this, &MainWindow::onSequenceLoaded);
    connect(sequence, &Sequence::currentChanged, this, [this] {
        setWindowModified(true); // the current step is saved with the project
        updateSequencePanel();
    });
    connect(sequence, &Sequence::displayImageChanged, this, &MainWindow::syncEngine);
    connect(sequence, &Sequence::loadFailed, this, [this](int index) {
        statusBar()->showMessage(tr("Could not load %1").arg(m_project.sequence().fileName(index)), 5000);
        updateSequencePanel();
    });
    connect(sequence, &Sequence::thumbnailReady, this, [this](int index, const QImage& thumbnail) {
        if (QListWidgetItem* item = m_stepList->item(index))
            item->setIcon(QIcon(QPixmap::fromImage(thumbnail)));
    });
    connect(m_editor, &WarpEditor::selectionChanged, this, &MainWindow::selectSurface);
    connect(m_editor, &WarpEditor::cornerSelectionChanged, this, [this] {
        updatePropertyPanel();
        syncEngine(); // the output marks the corner being aligned
    });
    connect(qApp, &QGuiApplication::screenAdded, this, &MainWindow::refreshScreens);
    connect(qApp, &QGuiApplication::screenRemoved, this, &MainWindow::refreshScreens);

    m_lastImageDir = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    refreshScreens();
    newProject();
    updateOutputStatus();
    resize(1500, 900);
}

MainWindow::~MainWindow()
{
    qApp->removeEventFilter(this);
    // Child widgets outlive m_project; keep them from calling back during teardown.
    m_editor->disconnect(this);
    m_surfaceList->blockSignals(true);
    m_stepList->blockSignals(true);
    m_project.sequence().disconnect(this);
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
    statusBar()->showMessage(tr("GPU: %1").arg(m_engine.adapterName()), 5000);
    syncEngine();
    return true;
}

void MainWindow::createActions()
{
    auto addAction = [this](QMenu* menu, const QString& text, const QList<QKeySequence>& shortcuts,
                            auto slot) {
        QAction* action = menu->addAction(text);
        action->setShortcuts(shortcuts);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };

    QMenu* fileMenu = menuBar()->addMenu(tr("&File"));
    addAction(fileMenu, tr("&New"), {QKeySequence::New}, &MainWindow::newProject);
    addAction(fileMenu, tr("&Open..."), {QKeySequence::Open}, &MainWindow::openProject);
    addAction(fileMenu, tr("&Save"), {QKeySequence::Save}, &MainWindow::saveProject);
    addAction(fileMenu, tr("Save &As..."), {QKeySequence::SaveAs}, &MainWindow::saveProjectAs);
    fileMenu->addSeparator();
    addAction(fileMenu, tr("E&xit"), {QKeySequence::Quit}, &QWidget::close);

    QMenu* editMenu = menuBar()->addMenu(tr("&Edit"));
    QAction* undoAction = m_history.stack()->createUndoAction(this, tr("&Undo"));
    undoAction->setShortcuts(QKeySequence::Undo);
    QAction* redoAction = m_history.stack()->createRedoAction(this, tr("&Redo"));
    redoAction->setShortcuts({QKeySequence(Qt::CTRL | Qt::Key_Y), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Z)});
    editMenu->addAction(undoAction);
    editMenu->addAction(redoAction);

    // Menus follow the order of a session: set up surfaces, open the output, then
    // step through the sequence while drawing.
    QMenu* surfaceMenu = menuBar()->addMenu(tr("S&urface"));
    addAction(surfaceMenu, tr("&Add Surface"), {QKeySequence(Qt::CTRL | Qt::Key_T)}, &MainWindow::addSurface);
    addAction(surfaceMenu, tr("&Remove Surface"), {QKeySequence::Delete}, &MainWindow::removeSurface);
    surfaceMenu->addSeparator();
    addAction(surfaceMenu, tr("Set &Image..."), {QKeySequence(Qt::CTRL | Qt::Key_I)}, &MainWindow::chooseImage);

    QMenu* outputMenu = menuBar()->addMenu(tr("&Output"));
    addAction(outputMenu, tr("&Open Output"), {QKeySequence(Qt::CTRL | Qt::Key_Return)},
              &MainWindow::openSelectedOutput);
    addAction(outputMenu, tr("&Close All Outputs"), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Return)},
              &MainWindow::closeAllOutputs);

    QMenu* sequenceMenu = menuBar()->addMenu(tr("Se&quence"));
    addAction(sequenceMenu, tr("&Open Folder..."), {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O)},
              &MainWindow::openSequenceFolder);
    addAction(sequenceMenu, tr("&Generate Steps from Image..."), {QKeySequence(Qt::CTRL | Qt::Key_G)},
              &MainWindow::generateStepsFromImage);
    sequenceMenu->addSeparator();
    Sequence* sequence = &m_project.sequence();
    m_nextAction = addAction(sequenceMenu, tr("&Next Step"),
                             {QKeySequence(Qt::Key_Space), QKeySequence(Qt::Key_PageDown)},
                             [sequence] { sequence->next(); });
    m_previousAction = addAction(sequenceMenu, tr("&Previous Step"),
                                 {QKeySequence(Qt::Key_Backspace), QKeySequence(Qt::Key_PageUp)},
                                 [sequence] { sequence->previous(); });
    m_firstAction = addAction(sequenceMenu, tr("&First Step"), {QKeySequence(Qt::Key_Home)},
                              [sequence] { sequence->first(); });
    m_lastAction = addAction(sequenceMenu, tr("&Last Step"), {QKeySequence(Qt::Key_End)},
                             [sequence] { sequence->last(); });
    sequenceMenu->addSeparator();
    m_guideAction = sequenceMenu->addAction(tr("Show &Guide"));
    m_guideAction->setCheckable(true);
    m_guideAction->setChecked(m_guideVisible);
    m_guideAction->setShortcut(QKeySequence(Qt::Key_H));
    connect(m_guideAction, &QAction::toggled, this, &MainWindow::setGuideVisible);
    m_alignAction = sequenceMenu->addAction(tr("&Alignment Pattern"));
    m_alignAction->setCheckable(true);
    m_alignAction->setShortcut(QKeySequence(Qt::Key_T));
    connect(m_alignAction, &QAction::toggled, this, &MainWindow::setAlignmentMode);
    m_guideModeAction = addAction(sequenceMenu, tr("&Cycle Guide Display"), {QKeySequence(Qt::Key_G)},
                                  &MainWindow::cycleGuideMode);

    // Stepping must work while the projector output window has focus, too.
    m_showActions = {m_nextAction, m_previousAction, m_firstAction, m_lastAction,
                     m_guideAction, m_alignAction, m_guideModeAction};
    for (QAction* action : std::as_const(m_showActions))
        action->setShortcutContext(Qt::ApplicationShortcut);

    QMenu* viewMenu = menuBar()->addMenu(tr("&View"));
    createLanguageMenu(viewMenu->addMenu(tr("&Language")));
    addAction(viewMenu, tr("&Key Settings..."), {}, &MainWindow::openKeySettings);

    QMenu* helpMenu = menuBar()->addMenu(tr("&Help"));
    QAction* aboutAction = helpMenu->addAction(tr("&About LumiCanvas"));
    connect(aboutAction, &QAction::triggered, this, [this] {
        QMessageBox::about(this, tr("About LumiCanvas"),
                           tr("<h3>LumiCanvas %1</h3><p>Projection guide for drawing.</p>"
                              "<p>GPU: %2</p>").arg(QStringLiteral(LUMI_VERSION), m_engine.adapterName()));
    });
}

void MainWindow::createLanguageMenu(QMenu* menu)
{
    // Language names are shown in their own language so they can be found in either UI.
    const QList<std::pair<QString, QString>> languages{
        {QStringLiteral("system"), tr("System Default")},
        {QStringLiteral("ko"), QStringLiteral("한국어")},
        {QStringLiteral("en"), QStringLiteral("English")},
    };
    const QString current = QSettings().value(QStringLiteral("language"), QStringLiteral("system")).toString();
    auto* group = new QActionGroup(menu);
    for (const auto& [code, name] : languages) {
        QAction* action = menu->addAction(name);
        action->setCheckable(true);
        action->setChecked(code == current);
        group->addAction(action);
        connect(action, &QAction::triggered, this, [this, code] {
            QSettings().setValue(QStringLiteral("language"), code);
            QMessageBox::information(this, tr("Language"),
                                     tr("The new language is used after LumiCanvas restarts."));
        });
    }
}

template <class Change>
void MainWindow::recordEdit(const QString& text, Change&& change, int mergeId)
{
    m_history.begin(text, mergeId, m_selected);
    change();
    m_history.end();
}

void MainWindow::createDocks()
{
    auto addDock = [this](const QString& title, const char* name, QWidget* panel, Qt::DockWidgetArea area) {
        auto* dock = new QDockWidget(title, this);
        dock->setObjectName(QLatin1String(name));
        dock->setFeatures(QDockWidget::DockWidgetMovable);
        dock->setWidget(panel);
        addDockWidget(area, dock);
        return dock;
    };
    m_sequenceDock = addDock(tr("Sequence"), "sequenceDock", createSequencePanel(), Qt::RightDockWidgetArea);
    addDock(tr("Surfaces"), "surfacesDock", createSurfacesPanel(), Qt::LeftDockWidgetArea);
    addDock(tr("Outputs"), "outputsDock", createOutputsPanel(), Qt::LeftDockWidgetArea);
}

QWidget* MainWindow::createSequencePanel()
{
    auto* panel = new QWidget;
    panel->setMinimumWidth(260);
    auto* layout = new QVBoxLayout(panel);

    auto* openButton = new QPushButton(tr("Open Folder..."));
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openSequenceFolder);
    auto* generateButton = new QPushButton(tr("From Image..."));
    generateButton->setToolTip(tr("Generate steps from one finished image"));
    connect(generateButton, &QPushButton::clicked, this, &MainWindow::generateStepsFromImage);
    auto* sourceRow = new QHBoxLayout;
    sourceRow->addWidget(openButton);
    sourceRow->addWidget(generateButton);
    layout->addLayout(sourceRow);

    m_sequenceFolderLabel = new QLabel;
    m_sequenceFolderLabel->setWordWrap(true);
    m_sequenceFolderLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_sequenceFolderLabel);

    m_stepLabel = new QLabel;
    QFont stepFont = m_stepLabel->font();
    stepFont.setPointSizeF(stepFont.pointSizeF() * 2.0);
    stepFont.setBold(true);
    m_stepLabel->setFont(stepFont);
    m_stepLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_stepLabel);

    m_stepFileLabel = new QLabel;
    m_stepFileLabel->setAlignment(Qt::AlignCenter);
    m_stepFileLabel->setWordWrap(true);
    layout->addWidget(m_stepFileLabel);

    auto* navRow = new QHBoxLayout;
    navRow->addWidget(actionButton(m_firstAction));
    navRow->addWidget(actionButton(m_previousAction));
    navRow->addWidget(actionButton(m_nextAction));
    navRow->addWidget(actionButton(m_lastAction));
    layout->addLayout(navRow);

    m_stepList = new QListWidget;
    m_stepList->setIconSize(QSize(48, 48));
    connect(m_stepList, &QListWidget::currentRowChanged, this,
            [this](int row) { m_project.sequence().setCurrentIndex(row); });
    layout->addWidget(m_stepList, 1);

    auto* toggleRow = new QHBoxLayout;
    toggleRow->addWidget(actionButton(m_guideAction));
    toggleRow->addWidget(actionButton(m_alignAction));
    layout->addLayout(toggleRow);

    layout->addWidget(createGuidePanel());

    m_keyHint = new QLabel;
    m_keyHint->setWordWrap(true);
    m_keyHint->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(m_keyHint);
    return panel;
}

QWidget* MainWindow::createGuidePanel()
{
    auto* box = new QGroupBox(tr("Guide Display"));
    m_guideForm = new QFormLayout(box);

    auto updateGuide = [this](auto change) {
        GuideSettings guide = m_project.guide();
        change(guide);
        m_project.setGuide(guide);
    };

    // Combo order matches GuideMode.
    m_guideModeCombo = new QComboBox;
    m_guideModeCombo->addItems({tr("Normal"), tr("Highlight new parts"), tr("Onion skin")});
    connect(m_guideModeCombo, &QComboBox::activated, this, [updateGuide](int index) {
        updateGuide([index](GuideSettings& g) { g.mode = GuideMode(index); });
    });
    m_guideForm->addRow(tr("Mode"), m_guideModeCombo);

    auto makePercentSlider = [] {
        auto* slider = new QSlider(Qt::Horizontal);
        slider->setRange(0, 100);
        return slider;
    };

    m_dimSlider = makePercentSlider();
    m_dimSlider->setToolTip(tr("Brightness of parts that were already in the previous step"));
    connect(m_dimSlider, &QSlider::valueChanged, this, [updateGuide](int value) {
        updateGuide([value](GuideSettings& g) { g.dimUnchanged = value / 100.0f; });
    });
    m_guideForm->addRow(tr("Drawn parts"), m_dimSlider);

    m_tintCheck = new QCheckBox(tr("Tint new parts"));
    connect(m_tintCheck, &QCheckBox::toggled, this, [updateGuide](bool on) {
        updateGuide([on](GuideSettings& g) { g.tintChanges = on; });
    });
    m_tintColorButton = new QToolButton;
    m_tintColorButton->setToolTip(tr("Choose the highlight colour"));
    connect(m_tintColorButton, &QToolButton::clicked, this, [this] {
        chooseGuideColor(&GuideSettings::highlightColor, &GuideSettings::tintChanges, tr("Highlight Colour"));
    });
    auto* tintRow = new QHBoxLayout;
    tintRow->addWidget(m_tintCheck, 1);
    tintRow->addWidget(m_tintColorButton);
    m_guideForm->addRow(QString(), tintRow);

    m_previousSlider = makePercentSlider();
    m_previousSlider->setToolTip(tr("How strongly the previous step shows through"));
    connect(m_previousSlider, &QSlider::valueChanged, this, [updateGuide](int value) {
        updateGuide([value](GuideSettings& g) { g.previousOpacity = value / 100.0f; });
    });
    m_guideForm->addRow(tr("Previous step"), m_previousSlider);

    m_brightnessSlider = makePercentSlider();
    m_brightnessSlider->setMinimum(5);
    m_brightnessSlider->setToolTip(tr("Overall projector light on the canvas"));
    connect(m_brightnessSlider, &QSlider::valueChanged, this, [updateGuide](int value) {
        updateGuide([value](GuideSettings& g) { g.brightness = value / 100.0f; });
    });
    m_guideForm->addRow(tr("Brightness"), m_brightnessSlider);

    m_invertCheck = new QCheckBox(tr("Invert (dark lines become light)"));
    connect(m_invertCheck, &QCheckBox::toggled, this, [updateGuide](bool on) {
        updateGuide([on](GuideSettings& g) { g.invert = on; });
    });
    m_guideForm->addRow(QString(), m_invertCheck);

    m_recolorCheck = new QCheckBox(tr("Line colour"));
    connect(m_recolorCheck, &QCheckBox::toggled, this, [updateGuide](bool on) {
        updateGuide([on](GuideSettings& g) { g.recolor = on; });
    });
    m_lineColorButton = new QToolButton;
    m_lineColorButton->setToolTip(tr("Choose the line colour"));
    connect(m_lineColorButton, &QToolButton::clicked, this, [this] {
        chooseGuideColor(&GuideSettings::lineColor, &GuideSettings::recolor, tr("Line Colour"));
    });
    auto* lineRow = new QHBoxLayout;
    lineRow->addWidget(m_recolorCheck, 1);
    lineRow->addWidget(m_lineColorButton);
    m_guideForm->addRow(QString(), lineRow);

    m_gridCheck = new QCheckBox(tr("Grid"));
    connect(m_gridCheck, &QCheckBox::toggled, this, [updateGuide](bool on) {
        updateGuide([on](GuideSettings& g) { g.grid = on; });
    });
    m_gridCellSpin = new QDoubleSpinBox;
    m_gridCellSpin->setRange(1.0, 10000.0);
    m_gridCellSpin->setDecimals(0);
    m_gridCellSpin->setSuffix(tr(" mm"));
    m_gridCellSpin->setKeyboardTracking(false);
    m_gridCellSpin->setToolTip(tr("Grid cell size on the real canvas"));
    connect(m_gridCellSpin, &QDoubleSpinBox::valueChanged, this, [updateGuide](double value) {
        updateGuide([value](GuideSettings& g) { g.gridCellMm = value; });
    });
    auto* gridRow = new QHBoxLayout;
    gridRow->addWidget(m_gridCheck, 1);
    gridRow->addWidget(m_gridCellSpin);
    m_guideForm->addRow(QString(), gridRow);

    m_gridStrengthSlider = makePercentSlider();
    connect(m_gridStrengthSlider, &QSlider::valueChanged, this, [updateGuide](int value) {
        updateGuide([value](GuideSettings& g) { g.gridStrength = value / 100.0f; });
    });
    m_guideForm->addRow(tr("Grid strength"), m_gridStrengthSlider);

    return box;
}

void MainWindow::updateGuidePanel()
{
    const GuideSettings& g = m_project.guide();
    const QSignalBlocker b1(m_guideModeCombo);
    const QSignalBlocker b2(m_dimSlider);
    const QSignalBlocker b3(m_tintCheck);
    const QSignalBlocker b4(m_previousSlider);
    const QSignalBlocker b5(m_brightnessSlider);
    const QSignalBlocker b6(m_invertCheck);
    const QSignalBlocker b7(m_recolorCheck);
    const QSignalBlocker b8(m_gridCheck);
    const QSignalBlocker b9(m_gridCellSpin);
    const QSignalBlocker b10(m_gridStrengthSlider);

    m_guideModeCombo->setCurrentIndex(int(g.mode));
    m_dimSlider->setValue(qRound(g.dimUnchanged * 100));
    m_tintCheck->setChecked(g.tintChanges);
    m_previousSlider->setValue(qRound(g.previousOpacity * 100));
    m_brightnessSlider->setValue(qRound(g.brightness * 100));
    m_invertCheck->setChecked(g.invert);
    m_recolorCheck->setChecked(g.recolor);
    m_gridCheck->setChecked(g.grid);
    m_gridCellSpin->setValue(g.gridCellMm);
    m_gridCellSpin->setEnabled(g.grid);
    m_gridStrengthSlider->setValue(qRound(g.gridStrength * 100));

    auto setSwatch = [](QToolButton* button, const QColor& color) {
        QPixmap swatch(16, 16);
        swatch.fill(color);
        button->setIcon(QIcon(swatch));
    };
    setSwatch(m_tintColorButton, g.highlightColor);
    setSwatch(m_lineColorButton, g.lineColor);
    m_guideForm->setRowVisible(m_gridStrengthSlider, g.grid);

    const bool highlight = g.mode == GuideMode::Highlight;
    m_guideForm->setRowVisible(m_dimSlider, highlight);
    m_guideForm->setRowVisible(2, highlight); // tint row
    m_guideForm->setRowVisible(m_previousSlider, g.mode == GuideMode::Onion);
}

void MainWindow::cycleGuideMode()
{
    GuideSettings guide = m_project.guide();
    guide.mode = GuideMode((int(guide.mode) + 1) % 3);
    m_project.setGuide(guide);
    statusBar()->showMessage(tr("Guide display: %1").arg(m_guideModeCombo->itemText(int(guide.mode))), 2000);
}

void MainWindow::chooseGuideColor(QColor GuideSettings::*color, bool GuideSettings::*enable, const QString& title)
{
    GuideSettings guide = m_project.guide();
    const QColor chosen = QColorDialog::getColor(guide.*color, this, title);
    if (!chosen.isValid())
        return;
    guide.*color = chosen;
    guide.*enable = true; // picking a colour implies wanting it
    m_project.setGuide(guide);
}

void MainWindow::setPeeking(bool peeking)
{
    if (peeking == m_peeking)
        return;
    m_peeking = peeking;
    syncEngine();
    updateSequencePanel();
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        const QKeySequence pressed(QKeyCombination(key->modifiers() & ~Qt::KeypadModifier, Qt::Key(key->key())));
        const QWidget* focus = QApplication::focusWidget();
        const bool typing = qobject_cast<const QLineEdit*>(focus) || qobject_cast<const QAbstractSpinBox*>(focus);
        if (!key->isAutoRepeat() && !typing && !QApplication::activeModalWidget() && m_peekKeys.contains(pressed)) {
            m_peekHeldKey = key->key();
            setPeeking(true);
        }
    } else if (event->type() == QEvent::KeyRelease) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (m_peeking && !key->isAutoRepeat() && key->key() == m_peekHeldKey)
            setPeeking(false);
    } else if (event->type() == QEvent::ApplicationStateChange
               && QGuiApplication::applicationState() != Qt::ApplicationActive) {
        setPeeking(false); // the release would never arrive
    }
    return QMainWindow::eventFilter(watched, event);
}

QList<KeyBinding> MainWindow::keyBindings() const
{
    const auto key = [](auto... keys) { return QList<QKeySequence>{QKeySequence(keys)...}; };
    return {
        {QStringLiteral("next"), tr("Next step"), key(Qt::Key_Space, Qt::Key_PageDown), m_nextAction->shortcuts()},
        {QStringLiteral("previous"), tr("Previous step"), key(Qt::Key_Backspace, Qt::Key_PageUp), m_previousAction->shortcuts()},
        {QStringLiteral("first"), tr("First step"), key(Qt::Key_Home), m_firstAction->shortcuts()},
        {QStringLiteral("last"), tr("Last step"), key(Qt::Key_End), m_lastAction->shortcuts()},
        {QStringLiteral("guide"), tr("Guide on/off"), key(Qt::Key_H), m_guideAction->shortcuts()},
        {QStringLiteral("peek"), tr("Hide guide while held"), key(Qt::Key_B), m_peekKeys},
        {QStringLiteral("align"), tr("Alignment pattern"), key(Qt::Key_T), m_alignAction->shortcuts()},
        {QStringLiteral("mode"), tr("Cycle guide display"), key(Qt::Key_G), m_guideModeAction->shortcuts()},
    };
}

void MainWindow::applyKeyBindings(const QList<KeyBinding>& bindings)
{
    const QHash<QString, QAction*> actions{
        {QStringLiteral("next"), m_nextAction},   {QStringLiteral("previous"), m_previousAction},
        {QStringLiteral("first"), m_firstAction}, {QStringLiteral("last"), m_lastAction},
        {QStringLiteral("guide"), m_guideAction}, {QStringLiteral("align"), m_alignAction},
        {QStringLiteral("mode"), m_guideModeAction},
    };
    for (const KeyBinding& binding : bindings) {
        if (QAction* action = actions.value(binding.id))
            action->setShortcuts(binding.keys);
        else
            m_peekKeys = binding.keys;
    }
}

void MainWindow::loadKeyBindings()
{
    QSettings settings;
    QList<KeyBinding> bindings = keyBindings();
    for (KeyBinding& binding : bindings) {
        const QString name = QStringLiteral("keys/") + binding.id;
        binding.keys = settings.contains(name)
                           ? QKeySequence::listFromString(settings.value(name).toString(), QKeySequence::PortableText)
                           : binding.defaults;
    }
    applyKeyBindings(bindings);
}

void MainWindow::openKeySettings()
{
    QList<QKeySequence> reserved{QKeySequence(Qt::Key_Escape), QKeySequence(Qt::Key_Tab),
                                 QKeySequence(Qt::Key_Backtab), QKeySequence(Qt::SHIFT | Qt::Key_Backtab)};
    for (const QAction* action : findChildren<QAction*>()) {
        if (!m_showActions.contains(action))
            reserved += action->shortcuts();
    }

    KeySettingsDialog dialog(keyBindings(), reserved, this);
    if (dialog.exec() != QDialog::Accepted)
        return;
    QSettings settings;
    const QList<KeyBinding> bindings = dialog.bindings();
    for (const KeyBinding& binding : bindings) {
        settings.setValue(QStringLiteral("keys/") + binding.id,
                          QKeySequence::listToString(binding.keys, QKeySequence::PortableText));
    }
    applyKeyBindings(bindings);
    updateKeyHint();
}

void MainWindow::updateKeyHint()
{
    QHash<QString, QString> names;
    const QList<KeyBinding> bindings = keyBindings();
    for (const KeyBinding& binding : bindings) {
        QStringList keys;
        for (const QKeySequence& key : binding.keys)
            keys << key.toString(QKeySequence::NativeText);
        names[binding.id] = keys.isEmpty() ? QStringLiteral("-") : keys.join(QStringLiteral(" / "));
    }
    m_keyHint->setText(tr("%1: next  ·  %2: previous\n"
                          "%3 / %4: first / last  ·  %5: guide on/off\n"
                          "%6: hide while held  ·  %7: alignment  ·  %8: guide display")
                           .arg(names[QStringLiteral("next")], names[QStringLiteral("previous")],
                                names[QStringLiteral("first")], names[QStringLiteral("last")],
                                names[QStringLiteral("guide")], names[QStringLiteral("peek")],
                                names[QStringLiteral("align")], names[QStringLiteral("mode")]));
}

QWidget* MainWindow::createSurfacesPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    m_surfaceList = new QListWidget;
    connect(m_surfaceList, &QListWidget::currentRowChanged, this, &MainWindow::selectSurface);
    layout->addWidget(m_surfaceList, 1);

    auto* listButtons = new QHBoxLayout;
    auto* addButton = new QPushButton(tr("Add"));
    auto* removeButton = new QPushButton(tr("Remove"));
    connect(addButton, &QPushButton::clicked, this, &MainWindow::addSurface);
    connect(removeButton, &QPushButton::clicked, this, &MainWindow::removeSurface);
    listButtons->addWidget(addButton);
    listButtons->addWidget(removeButton);
    layout->addLayout(listButtons);

    m_propertiesBox = new QGroupBox(tr("Properties"));
    auto* form = new QFormLayout(m_propertiesBox);

    m_nameEdit = new QLineEdit;
    connect(m_nameEdit, &QLineEdit::textEdited, this,
            [this](const QString& text) {
                recordEdit(tr("Rename Surface"), [&] { m_project.setName(m_selected, text); },
                           History::MergeRename);
            });
    form->addRow(tr("Name"), m_nameEdit);

    // Combo order matches ContentType.
    m_contentCombo = new QComboBox;
    m_contentCombo->addItems({tr("Test Pattern"), tr("Image"), tr("Sequence")});
    connect(m_contentCombo, &QComboBox::activated, this, &MainWindow::onContentActivated);
    m_contentButton = new QPushButton;
    connect(m_contentButton, &QPushButton::clicked, this, [this] {
        if (!m_project.isValidIndex(m_selected))
            return;
        if (m_project.surface(m_selected).content == ContentType::Sequence)
            openSequenceFolder();
        else
            chooseImage();
    });
    auto* contentRow = new QHBoxLayout;
    contentRow->addWidget(m_contentCombo, 1);
    contentRow->addWidget(m_contentButton);
    form->addRow(tr("Content"), contentRow);

    m_contentLabel = new QLabel;
    m_contentLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_contentLabel->setWordWrap(true);
    form->addRow(QString(), m_contentLabel);

    auto makeSizeSpin = [this] {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(10.0, 100000.0);
        spin->setDecimals(0);
        spin->setSuffix(tr(" mm"));
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, this, &MainWindow::onCanvasSizeEdited);
        return spin;
    };
    m_canvasWidth = makeSizeSpin();
    m_canvasHeight = makeSizeSpin();
    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(m_canvasWidth, 1);
    sizeRow->addWidget(new QLabel(QStringLiteral("×")));
    sizeRow->addWidget(m_canvasHeight, 1);
    form->addRow(tr("Canvas"), sizeRow);

    // Combo order matches FitMode.
    m_fitCombo = new QComboBox;
    m_fitCombo->addItems({tr("Fit (keep aspect)"), tr("Fill (crop)"), tr("Stretch")});
    connect(m_fitCombo, &QComboBox::activated, this,
            [this](int index) {
                recordEdit(tr("Change Placement"), [&] { m_project.setFitMode(m_selected, FitMode(index)); });
            });
    form->addRow(tr("Placement"), m_fitCombo);

    // Combo order matches the corner order: TL, TR, BR, BL.
    m_cornerCombo = new QComboBox;
    m_cornerCombo->addItems({tr("Top left"), tr("Top right"), tr("Bottom right"), tr("Bottom left")});
    m_cornerCombo->setPlaceholderText(tr("Select a corner"));
    connect(m_cornerCombo, &QComboBox::activated, m_editor, &WarpEditor::setSelectedCorner);
    form->addRow(tr("Corner"), m_cornerCombo);

    auto makeCornerSpin = [this](Qt::Orientation axis) {
        auto* spin = new QDoubleSpinBox;
        spin->setRange(-100000.0, 100000.0);
        spin->setDecimals(1);
        spin->setSuffix(tr(" px"));
        spin->setKeyboardTracking(false);
        connect(spin, &QDoubleSpinBox::valueChanged, this,
                [this, axis](double value) { onCornerEdited(axis, value); });
        return spin;
    };
    m_cornerX = makeCornerSpin(Qt::Horizontal);
    m_cornerY = makeCornerSpin(Qt::Vertical);
    auto* cornerRow = new QHBoxLayout;
    cornerRow->addWidget(m_cornerX, 1);
    cornerRow->addWidget(m_cornerY, 1);
    form->addRow(QString(), cornerRow);

    m_opacitySlider = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacityLabel = new QLabel;
    m_opacityLabel->setMinimumWidth(40);
    connect(m_opacitySlider, &QSlider::valueChanged, this,
            [this](int value) {
                recordEdit(tr("Change Opacity"), [&] { m_project.setOpacity(m_selected, value / 100.0f); },
                           History::MergeOpacity);
            });
    auto* opacityRow = new QHBoxLayout;
    opacityRow->addWidget(m_opacitySlider, 1);
    opacityRow->addWidget(m_opacityLabel);
    form->addRow(tr("Opacity"), opacityRow);

    m_visibleCheck = new QCheckBox(tr("Visible"));
    connect(m_visibleCheck, &QCheckBox::toggled, this,
            [this](bool on) {
                recordEdit(tr("Toggle Visibility"), [&] { m_project.setVisible(m_selected, on); });
            });
    form->addRow(QString(), m_visibleCheck);

    layout->addWidget(m_propertiesBox);
    return panel;
}

QWidget* MainWindow::createOutputsPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);

    m_screenCombo = new QComboBox;
    connect(m_screenCombo, &QComboBox::currentIndexChanged, this, &MainWindow::onScreenSelected);
    layout->addWidget(new QLabel(tr("Display")));
    layout->addWidget(m_screenCombo);

    m_windowedCheck = new QCheckBox(tr("Windowed (for testing)"));
    layout->addWidget(m_windowedCheck);

    auto* buttons = new QHBoxLayout;
    auto* openButton = new QPushButton(tr("Open Output"));
    auto* closeButton = new QPushButton(tr("Close All"));
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openSelectedOutput);
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::closeAllOutputs);
    buttons->addWidget(openButton);
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    m_outputStatus = new QLabel;
    m_outputStatus->setWordWrap(true);
    layout->addWidget(m_outputStatus);
    layout->addStretch();
    return panel;
}

// ---------------------------------------------------------------------------
// Project file handling

void MainWindow::newProject()
{
    if (!maybeSave())
        return;
    m_project.clear();
    const int canvas = m_project.addSurface();
    m_project.setName(canvas, tr("Canvas"));
    m_project.setContent(canvas, ContentType::Sequence);
    m_projectPath.clear();
    m_history.clear();
    selectSurface(canvas);
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
    if (!path.isEmpty())
        openProjectFile(path);
}

void MainWindow::openProjectFile(const QString& path)
{
    QString error;
    QStringList missing;
    if (!m_project.load(path, &error, &missing)) {
        QMessageBox::critical(this, tr("Open Project"), error);
        return;
    }
    m_projectPath = path;
    m_history.clear();
    selectSurface(m_project.count() > 0 ? 0 : -1);
    setWindowModified(false);
    updateTitle();
    if (!missing.isEmpty()) {
        QMessageBox::warning(this, tr("Open Project"),
                             tr("These files or folders could not be found:\n\n%1").arg(missing.join('\n')));
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

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (!urls.isEmpty() && urls.first().isLocalFile())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    event->acceptProposedAction();
    openPath(event->mimeData()->urls().first().toLocalFile());
}

void MainWindow::openPath(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) {
        QMessageBox::warning(this, tr("LumiCanvas"),
                             tr("%1 does not exist.").arg(QDir::toNativeSeparators(path)));
        return;
    }
    if (info.isDir()) {
        loadSequence(info.absoluteFilePath());
    } else if (info.suffix().compare(QLatin1String("lumi"), Qt::CaseInsensitive) == 0) {
        if (maybeSave())
            openProjectFile(info.absoluteFilePath());
    } else {
        // A single image: load its whole folder and jump to it.
        loadSequence(info.absolutePath());
        Sequence& sequence = m_project.sequence();
        for (int i = 0; i < sequence.count(); ++i) {
            if (sequence.fileName(i) == info.fileName())
                sequence.setCurrentIndex(i);
        }
    }
}

// ---------------------------------------------------------------------------
// Sequence

void MainWindow::openSequenceFolder()
{
    const QString start = m_project.sequence().isEmpty() ? m_lastImageDir : m_project.sequence().folder();
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Open Image Sequence Folder"), start);
    if (!folder.isEmpty())
        loadSequence(folder);
}

void MainWindow::generateStepsFromImage()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Choose a Finished Image"), m_lastImageDir, imageFilter());
    if (path.isEmpty())
        return;
    m_lastImageDir = QFileInfo(path).absolutePath();
    const QImage source = loadImage(path);
    if (source.isNull()) {
        QMessageBox::warning(this, tr("Choose Image"), tr("Could not load %1").arg(QDir::toNativeSeparators(path)));
        return;
    }
    GenerateStepsDialog dialog(source, path, this);
    if (dialog.exec() == QDialog::Accepted)
        runStepGeneration(source, dialog.options(), dialog.outputFolder());
}

void MainWindow::runStepGeneration(const QImage& source, const StepOptions& options, const QString& folder)
{
    auto* progress = new QProgressDialog(tr("Generating steps..."), tr("Cancel"), 0, 0, this);
    progress->setWindowTitle(tr("Generate Steps from Image"));
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);
    progress->setAutoClose(false);
    progress->setAutoReset(false);

    auto* watcher = new QFutureWatcher<QString>(this);
    connect(watcher, &QFutureWatcher<QString>::progressRangeChanged, progress, &QProgressDialog::setRange);
    connect(watcher, &QFutureWatcher<QString>::progressValueChanged, progress, &QProgressDialog::setValue);
    connect(progress, &QProgressDialog::canceled, watcher, &QFutureWatcher<QString>::cancel);
    connect(watcher, &QFutureWatcher<QString>::finished, this, [this, watcher, progress, folder] {
        const bool canceled = watcher->isCanceled();
        const QString error = !canceled && watcher->future().resultCount() > 0 ? watcher->result() : QString();
        watcher->deleteLater();
        progress->deleteLater();
        if (canceled)
            statusBar()->showMessage(tr("Step generation cancelled"), 5000);
        else if (!error.isEmpty())
            QMessageBox::warning(this, tr("Generate Steps from Image"), error);
        else
            loadSequence(folder);
    });
    watcher->setFuture(QtConcurrent::run([source, options, folder](QPromise<QString>& promise) {
        promise.addResult(saveSteps(source, options, folder, [&](int done, int total) {
            promise.setProgressRange(0, total);
            promise.setProgressValue(done);
            return !promise.isCanceled();
        }));
    }));
}

void MainWindow::loadSequence(const QString& folder)
{
    if (!m_project.sequence().load(folder)) {
        QMessageBox::warning(this, tr("Open Folder"),
                             tr("No images found in %1").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    m_lastImageDir = folder;

    // Make sure something actually shows the sequence.
    for (const Surface& s : m_project.surfaces()) {
        if (s.content == ContentType::Sequence)
            return;
    }
    int target = m_selected;
    recordEdit(tr("Show Sequence"), [&] {
        if (!m_project.isValidIndex(target))
            target = m_project.count() > 0 ? 0 : m_project.addSurface();
        m_project.setContent(target, ContentType::Sequence);
    });
    selectSurface(target);
}

void MainWindow::onSequenceLoaded()
{
    const Sequence& sequence = m_project.sequence();
    {
        const QSignalBlocker blocker(m_stepList);
        m_stepList->clear();
        for (int i = 0; i < sequence.count(); ++i)
            m_stepList->addItem(QStringLiteral("%1.  %2").arg(i + 1).arg(sequence.fileName(i)));
    }
    setWindowModified(true);
    updateSequencePanel();
}

void MainWindow::updateSequencePanel()
{
    updateContentStatus();

    const Sequence& sequence = m_project.sequence();
    const int current = sequence.currentIndex();
    const bool empty = sequence.isEmpty();

    QString status;
    if (empty) {
        m_sequenceFolderLabel->setText(tr("No folder loaded. Drop a folder of step images here."));
        m_stepLabel->setText(QStringLiteral("-"));
        m_stepFileLabel->clear();
    } else {
        m_sequenceFolderLabel->setText(QDir::toNativeSeparators(sequence.folder()));
        m_stepLabel->setText(QStringLiteral("%1 / %2").arg(current + 1).arg(sequence.count()));
        QString file = sequence.fileName(current);
        if (sequence.hasFailed(current))
            file = tr("%1 (could not load)").arg(file);
        m_stepFileLabel->setText(file);
        status = tr("Step %1 / %2").arg(current + 1).arg(sequence.count());
    }
    {
        const QSignalBlocker blocker(m_stepList);
        m_stepList->setCurrentRow(current);
    }

    if (m_alignmentMode)
        status += (status.isEmpty() ? QString() : QStringLiteral("  ·  ")) + tr("Alignment pattern");
    else if (!m_guideVisible || m_peeking)
        status += (status.isEmpty() ? QString() : QStringLiteral("  ·  ")) + tr("Guide hidden");
    m_statusLabel->setText(status);
}

void MainWindow::setGuideVisible(bool visible)
{
    m_guideVisible = visible;
    {
        const QSignalBlocker blocker(m_guideAction);
        m_guideAction->setChecked(visible);
    }
    syncEngine();
    updateSequencePanel();
}

void MainWindow::setAlignmentMode(bool on)
{
    m_alignmentMode = on;
    {
        const QSignalBlocker blocker(m_alignAction);
        m_alignAction->setChecked(on);
    }
    if (on) {
        // Start on a corner right away: Tab and arrows then work from the editor or the output.
        if (m_editor->selectedCorner() < 0)
            m_editor->setSelectedCorner(0);
        statusBar()->showMessage(tr("Alignment: Tab picks a corner, arrows move it (Shift ×10)"), 5000);
    }
    syncEngine();
    updateSequencePanel();
}

// ---------------------------------------------------------------------------
// Surfaces

void MainWindow::addSurface()
{
    int index = -1;
    recordEdit(tr("Add Surface"), [&] { index = m_project.addSurface(); });
    selectSurface(index);
}

void MainWindow::removeSurface()
{
    if (!m_project.isValidIndex(m_selected))
        return;
    const int index = m_selected;
    recordEdit(tr("Remove Surface"), [&] { m_project.removeSurface(index); });
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
        bool loaded = false;
        recordEdit(tr("Set Image"), [&] { loaded = m_project.setImage(m_selected, path); });
        if (!loaded) {
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
    const ContentType content = ContentType(index);
    bool applied = false;
    recordEdit(tr("Change Content"), [&] { applied = m_project.setContent(m_selected, content); });
    if (content == ContentType::Image && !applied)
        chooseImage(); // no image yet
    else if (content == ContentType::Sequence && m_project.sequence().isEmpty())
        openSequenceFolder();
}

void MainWindow::onCanvasSizeEdited()
{
    const QSizeF size(m_canvasWidth->value(), m_canvasHeight->value());
    recordEdit(tr("Change Canvas Size"), [&] { m_project.setCanvasSize(m_selected, size); },
               History::MergeCanvasSize);
}

void MainWindow::onCornerEdited(Qt::Orientation axis, double pixels)
{
    const int corner = m_editor->selectedCorner();
    if (!m_project.isValidIndex(m_selected) || corner < 0)
        return;
    // Only the edited axis changes, so rounding in the other box can't creep in.
    QPointF position = m_project.surface(m_selected).corners[corner];
    const QSize canvas = m_editor->canvasSize();
    if (axis == Qt::Horizontal)
        position.setX(pixels / canvas.width());
    else
        position.setY(pixels / canvas.height());
    m_history.begin(tr("Move Corner"), History::MergeCornerInput, m_selected * 5 + corner);
    m_project.setCorner(m_selected, corner, position);
    m_history.end();
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

void MainWindow::updateContentStatus()
{
    const bool valid = m_project.isValidIndex(m_selected);
    const ContentType content = valid ? m_project.surface(m_selected).content : ContentType::TestPattern;

    m_contentButton->setVisible(content != ContentType::TestPattern);
    // The sequence panel only applies to a surface that shows the sequence. It stays
    // in place when disabled, so the layout doesn't jump while switching surfaces.
    m_sequenceDock->widget()->setEnabled(content == ContentType::Sequence);

    if (content == ContentType::Image) {
        const Surface& s = m_project.surface(m_selected);
        const QString file = QFileInfo(s.imagePath).fileName();
        m_contentButton->setText(tr("Choose..."));
        m_contentLabel->setText(s.image.isNull()
                                    ? tr("%1 (missing)").arg(file)
                                    : QStringLiteral("%1  (%2 × %3)").arg(file).arg(s.image.width()).arg(s.image.height()));
    } else if (content == ContentType::Sequence) {
        const Sequence& sequence = m_project.sequence();
        m_contentButton->setText(tr("Choose Folder..."));
        m_contentLabel->setText(sequence.isEmpty()
                                    ? tr("No folder selected")
                                    : tr("%1  (step %2 / %3)\n%4")
                                          .arg(QFileInfo(sequence.folder()).fileName())
                                          .arg(sequence.currentIndex() + 1)
                                          .arg(sequence.count())
                                          .arg(sequence.fileName(sequence.currentIndex())));
    } else {
        m_contentLabel->clear();
    }
}

void MainWindow::updatePropertyPanel()
{
    const bool valid = m_project.isValidIndex(m_selected);
    m_propertiesBox->setEnabled(valid);

    const QSignalBlocker b1(m_nameEdit);
    const QSignalBlocker b2(m_contentCombo);
    const QSignalBlocker b3(m_opacitySlider);
    const QSignalBlocker b4(m_visibleCheck);
    const QSignalBlocker b5(m_canvasWidth);
    const QSignalBlocker b6(m_canvasHeight);
    const QSignalBlocker b7(m_fitCombo);
    const QSignalBlocker b8(m_cornerCombo);
    const QSignalBlocker b9(m_cornerX);
    const QSignalBlocker b10(m_cornerY);

    updateContentStatus();
    if (!valid) {
        m_nameEdit->clear();
        m_opacityLabel->clear();
        return;
    }

    const Surface& s = m_project.surface(m_selected);
    if (m_nameEdit->text() != s.name) // don't disturb the cursor while typing
        m_nameEdit->setText(s.name);
    m_contentCombo->setCurrentIndex(int(s.content));
    m_canvasWidth->setValue(s.canvasSizeMm.width());
    m_canvasHeight->setValue(s.canvasSizeMm.height());
    m_fitCombo->setCurrentIndex(int(s.fit));

    const int corner = m_editor->selectedCorner();
    m_cornerCombo->setCurrentIndex(corner);
    m_cornerX->setEnabled(corner >= 0);
    m_cornerY->setEnabled(corner >= 0);
    if (corner >= 0) {
        const QSize canvas = m_editor->canvasSize();
        m_cornerX->setValue(s.corners[corner].x() * canvas.width());
        m_cornerY->setValue(s.corners[corner].y() * canvas.height());
    }

    m_opacitySlider->setValue(qRound(s.opacity * 100));
    m_opacityLabel->setText(QStringLiteral("%1%").arg(qRound(s.opacity * 100)));
    m_visibleCheck->setChecked(s.visible);
}

void MainWindow::applyGuide(SceneSurface& out, const Surface& surface) const
{
    const GuideSettings& g = m_project.guide();
    const Sequence& sequence = m_project.sequence();
    const QImage previous = sequence.previousImage();
    // While the previous step is still loading, show the plain image rather than
    // highlighting everything as new.
    if (g.mode == GuideMode::Normal || (sequence.currentIndex() > 0 && previous.isNull()))
        return;

    out.guide = g.mode == GuideMode::Highlight ? SceneSurface::Guide::Highlight : SceneSurface::Guide::Onion;
    out.previous = previous;
    if (!previous.isNull())
        out.previousRect = contentRect(surface.fit, surface.canvasSizeMm, previous.size());
    out.dimUnchanged = g.dimUnchanged;
    out.previousOpacity = g.previousOpacity;
    out.tintAmount = g.tintChanges ? 1.0f : 0.0f;
    out.highlightColor = {g.highlightColor.redF(), g.highlightColor.greenF(), g.highlightColor.blueF(), 1.0f};
}

void MainWindow::applyAdjustments(SceneSurface& out, const Surface& surface) const
{
    const GuideSettings& g = m_project.guide();
    out.invert = g.invert;
    out.recolor = g.recolor;
    out.lineColor = {g.lineColor.redF(), g.lineColor.greenF(), g.lineColor.blueF(), 1.0f};
    out.brightness = g.brightness;
    if (g.grid) {
        out.gridColumns = float(surface.canvasSizeMm.width() / g.gridCellMm);
        out.gridRows = float(surface.canvasSizeMm.height() / g.gridCellMm);
        out.gridStrength = g.gridStrength;
    }
}

void MainWindow::syncEngine()
{
    auto scene = std::make_shared<Scene>();
    if ((m_guideVisible && !m_peeking) || m_alignmentMode) {
        for (int i = 0; i < m_project.count(); ++i) {
            const Surface& s = m_project.surface(i);
            if (!s.visible)
                continue;
            SceneSurface out;
            out.corners = s.corners;
            const QColor color = surfaceColor(i);
            out.tint = {color.redF(), color.greenF(), color.blueF(), 1.0f};
            out.opacity = m_alignmentMode ? 1.0f : s.opacity;

            if (!m_alignmentMode) { // the alignment pattern replaces all content
                QImage image;
                if (s.content == ContentType::Image) {
                    image = s.image; // a missing image falls back to the test pattern
                } else if (s.content == ContentType::Sequence) {
                    image = m_project.sequence().displayImage();
                    if (image.isNull())
                        continue; // nothing loaded yet: keep the canvas dark
                }
                if (!image.isNull()) {
                    out.image = image;
                    out.contentRect = contentRect(s.fit, s.canvasSizeMm, image.size());
                    applyAdjustments(out, s);
                }
                if (s.content == ContentType::Sequence)
                    applyGuide(out, s);
            } else if (i == m_selected) {
                out.activeCorner = m_editor->selectedCorner();
            }
            scene->surfaces.push_back(std::move(out));
        }
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
    updatePropertyPanel(); // corner positions are shown in output pixels
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
    connect(window, &OutputWindow::keyPressed, this, &MainWindow::handleOutputKey);
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

void MainWindow::handleOutputKey(QKeyCombination key)
{
    // Fallback for keys the shortcut system did not pick up on the output window.
    // Ignoring the keypad flag lets numpad Home/End/PageUp/PageDown step as well.
    const QKeySequence pressed(QKeyCombination(key.keyboardModifiers() & ~Qt::KeypadModifier, key.key()));
    for (QAction* action : std::as_const(m_showActions)) {
        if (action->isEnabled() && action->shortcuts().contains(pressed)) {
            action->trigger();
            return;
        }
    }
    // Arrows only move corners while aligning, so a stray key can't shift a drawing in progress.
    if (m_alignmentMode)
        m_editor->handleAlignmentKey(key);
}

} // namespace lumi
