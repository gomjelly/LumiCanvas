#pragma once

#include "core/Project.h"
#include "render/RenderEngine.h"

#include <QHash>
#include <QKeyCombination>
#include <QList>
#include <QMainWindow>

class QAction;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QScreen;
class QSlider;

namespace lumi {

class OutputWindow;
class WarpEditor;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    // Starts the render engine; shows an error and returns false on failure.
    bool initialize();
    void openOutput(QScreen* screen, bool fullscreen);
    void loadSequence(const QString& folder);
    // Opens a .lumi project, a folder of step images, or the folder of one image.
    void openPath(const QString& path);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private:
    void createActions();
    void createDocks();
    QWidget* createSequencePanel();
    QWidget* createSurfacesPanel();
    QWidget* createOutputsPanel();

    void newProject();
    void openProject();
    void openProjectFile(const QString& path);
    bool saveProject();
    bool saveProjectAs();
    bool maybeSave();
    void updateTitle();

    void openSequenceFolder();
    void onSequenceLoaded();
    void updateSequencePanel();
    void setGuideVisible(bool visible);
    void setAlignmentMode(bool on);

    void addSurface();
    void removeSurface();
    void selectSurface(int index);
    void chooseImage();
    void onContentActivated(int index);
    void onCanvasSizeEdited();
    void onStructureChanged();
    void onSurfaceChanged(int index);
    void updatePropertyPanel();
    void syncEngine();

    void refreshScreens();
    QScreen* selectedScreen() const;
    void onScreenSelected();
    void openSelectedOutput();
    void closeOutput(OutputWindow* window);
    void closeAllOutputs();
    void updateOutputStatus();
    void handleOutputKey(QKeyCombination key);

    Project m_project;
    RenderEngine m_engine;
    WarpEditor* m_editor = nullptr;
    int m_selected = -1;
    bool m_guideVisible = true;
    bool m_alignmentMode = false;
    QString m_projectPath;
    QString m_lastImageDir;

    // Actions that also work while a projector output has focus.
    QList<QAction*> m_showActions;
    QAction* m_nextAction = nullptr;
    QAction* m_previousAction = nullptr;
    QAction* m_firstAction = nullptr;
    QAction* m_lastAction = nullptr;
    QAction* m_guideAction = nullptr;
    QAction* m_alignAction = nullptr;

    QLabel* m_sequenceFolderLabel = nullptr;
    QLabel* m_stepLabel = nullptr;
    QLabel* m_stepFileLabel = nullptr;
    QListWidget* m_stepList = nullptr;
    QLabel* m_statusLabel = nullptr;

    QListWidget* m_surfaceList = nullptr;
    QGroupBox* m_propertiesBox = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_contentCombo = nullptr;
    QPushButton* m_imageButton = nullptr;
    QLabel* m_imageLabel = nullptr;
    QDoubleSpinBox* m_canvasWidth = nullptr;
    QDoubleSpinBox* m_canvasHeight = nullptr;
    QComboBox* m_fitCombo = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QCheckBox* m_visibleCheck = nullptr;

    QComboBox* m_screenCombo = nullptr;
    QCheckBox* m_windowedCheck = nullptr;
    QLabel* m_outputStatus = nullptr;
    QHash<OutputWindow*, int> m_outputs;
};

} // namespace lumi
