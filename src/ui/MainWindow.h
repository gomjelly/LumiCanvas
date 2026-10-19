#pragma once

#include "core/Project.h"
#include "render/RenderEngine.h"

#include <QHash>
#include <QMainWindow>

class QCheckBox;
class QComboBox;
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

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createActions();
    void createDocks();

    void newProject();
    void openProject();
    bool saveProject();
    bool saveProjectAs();
    bool maybeSave();
    void updateTitle();

    void addSurface();
    void removeSurface();
    void selectSurface(int index);
    void chooseImage();
    void onContentActivated(int index);
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

    Project m_project;
    RenderEngine m_engine;
    WarpEditor* m_editor = nullptr;
    int m_selected = -1;
    QString m_projectPath;
    QString m_lastImageDir;

    QListWidget* m_surfaceList = nullptr;
    QGroupBox* m_propertiesBox = nullptr;
    QLineEdit* m_nameEdit = nullptr;
    QComboBox* m_contentCombo = nullptr;
    QPushButton* m_imageButton = nullptr;
    QLabel* m_imageLabel = nullptr;
    QSlider* m_opacitySlider = nullptr;
    QLabel* m_opacityLabel = nullptr;
    QCheckBox* m_visibleCheck = nullptr;

    QComboBox* m_screenCombo = nullptr;
    QCheckBox* m_windowedCheck = nullptr;
    QLabel* m_outputStatus = nullptr;
    QHash<OutputWindow*, int> m_outputs;
};

} // namespace lumi
