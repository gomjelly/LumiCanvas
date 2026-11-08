#pragma once

#include "core/StepGenerator.h"

#include <QDialog>
#include <QImage>
#include <QList>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QSlider;
class QSpinBox;
class QTimer;

namespace lumi {

// Lets the user tune how a finished image is split into steps, with a live preview.
class GenerateStepsDialog : public QDialog {
    Q_OBJECT

public:
    GenerateStepsDialog(const QImage& source, const QString& sourcePath, QWidget* parent = nullptr);

    StepOptions options() const;
    QString outputFolder() const;

    void accept() override;

private:
    void schedulePreview();
    void runPreview();
    void showPreview(const QList<GeneratedStep>& steps);
    void chooseFolder();

    QImage m_previewSource;
    quint64 m_previewGeneration = 0; // only the newest preview gets shown
    QTimer* m_previewTimer = nullptr;

    QComboBox* m_methodCombo = nullptr;
    QSpinBox* m_levelsSpin = nullptr;
    QComboBox* m_orderCombo = nullptr;
    QCheckBox* m_outlineCheck = nullptr;
    QSlider* m_detailSlider = nullptr;
    QCheckBox* m_originalCheck = nullptr;
    QLineEdit* m_folderEdit = nullptr;
    QListWidget* m_previewList = nullptr;
    QLabel* m_previewStatus = nullptr;
};

} // namespace lumi
