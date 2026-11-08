#include "ui/GenerateStepsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

namespace lumi {

namespace {

constexpr int kPreviewSide = 360;

// A folder that doesn't exist yet, so generating never touches earlier results.
QString defaultFolder(const QString& sourcePath)
{
    const QFileInfo info(sourcePath);
    const QString base = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral("_steps");
    QString folder = base;
    for (int n = 2; QFileInfo::exists(folder); ++n)
        folder = base + QLatin1Char('_') + QString::number(n);
    return folder;
}

} // namespace

GenerateStepsDialog::GenerateStepsDialog(const QImage& source, const QString& sourcePath, QWidget* parent)
    : QDialog(parent)
    , m_previewSource(source.scaled(kPreviewSide, kPreviewSide, Qt::KeepAspectRatio, Qt::SmoothTransformation))
{
    setWindowTitle(tr("Generate Steps from Image"));
    resize(960, 560);

    auto* form = new QFormLayout;

    // Combo order matches StepOptions::Method.
    m_methodCombo = new QComboBox;
    m_methodCombo->addItems({tr("Colour regions"), tr("Light and dark (grey)")});
    form->addRow(tr("Split by"), m_methodCombo);

    m_levelsSpin = new QSpinBox;
    m_levelsSpin->setRange(2, 16);
    m_levelsSpin->setValue(5);
    form->addRow(tr("Regions"), m_levelsSpin);

    m_orderCombo = new QComboBox;
    m_orderCombo->addItems({tr("Dark to light"), tr("Light to dark")});
    form->addRow(tr("Painting order"), m_orderCombo);

    m_outlineCheck = new QCheckBox(tr("Start with an outline"));
    m_outlineCheck->setChecked(true);
    form->addRow(QString(), m_outlineCheck);

    m_detailSlider = new QSlider(Qt::Horizontal);
    m_detailSlider->setRange(0, 100);
    m_detailSlider->setValue(50);
    m_detailSlider->setToolTip(tr("Left: main shapes only. Right: fine detail."));
    form->addRow(tr("Line detail"), m_detailSlider);

    m_originalCheck = new QCheckBox(tr("End with the original image"));
    m_originalCheck->setChecked(true);
    form->addRow(QString(), m_originalCheck);

    m_folderEdit = new QLineEdit(QDir::toNativeSeparators(defaultFolder(sourcePath)));
    auto* browse = new QPushButton(tr("Choose..."));
    browse->setAutoDefault(false); // Enter must generate, not open the folder picker
    connect(browse, &QPushButton::clicked, this, &GenerateStepsDialog::chooseFolder);
    auto* folderRow = new QHBoxLayout;
    folderRow->addWidget(m_folderEdit, 1);
    folderRow->addWidget(browse);
    form->addRow(tr("Save to"), folderRow);

    m_previewList = new QListWidget;
    m_previewList->setViewMode(QListView::IconMode);
    m_previewList->setResizeMode(QListView::Adjust);
    m_previewList->setMovement(QListView::Static);
    m_previewList->setSelectionMode(QAbstractItemView::NoSelection);
    m_previewList->setFocusPolicy(Qt::NoFocus);
    m_previewList->setIconSize(QSize(170, 170));
    m_previewList->setSpacing(4);
    m_previewStatus = new QLabel;

    auto* previewColumn = new QVBoxLayout;
    previewColumn->addWidget(m_previewList, 1);
    previewColumn->addWidget(m_previewStatus);

    auto* columns = new QHBoxLayout;
    columns->addLayout(form);
    columns->addLayout(previewColumn, 1);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton* generate = buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &GenerateStepsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &GenerateStepsDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(columns, 1);
    layout->addWidget(buttons);
    generate->setDefault(true); // only takes effect once the button belongs to the dialog

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(200); // don't recompute for every slider tick
    connect(m_previewTimer, &QTimer::timeout, this, &GenerateStepsDialog::runPreview);

    connect(m_methodCombo, &QComboBox::currentIndexChanged, this, &GenerateStepsDialog::schedulePreview);
    connect(m_levelsSpin, &QSpinBox::valueChanged, this, &GenerateStepsDialog::schedulePreview);
    connect(m_orderCombo, &QComboBox::currentIndexChanged, this, &GenerateStepsDialog::schedulePreview);
    connect(m_outlineCheck, &QCheckBox::toggled, this, &GenerateStepsDialog::schedulePreview);
    connect(m_outlineCheck, &QCheckBox::toggled, m_detailSlider, &QSlider::setEnabled);
    connect(m_detailSlider, &QSlider::valueChanged, this, &GenerateStepsDialog::schedulePreview);
    connect(m_originalCheck, &QCheckBox::toggled, this, &GenerateStepsDialog::schedulePreview);

    runPreview();
}

StepOptions GenerateStepsDialog::options() const
{
    StepOptions options;
    options.method = StepOptions::Method(m_methodCombo->currentIndex());
    options.levels = m_levelsSpin->value();
    options.darkFirst = m_orderCombo->currentIndex() == 0;
    options.outline = m_outlineCheck->isChecked();
    options.lineDetail = m_detailSlider->value();
    options.finalOriginal = m_originalCheck->isChecked();
    return options;
}

QString GenerateStepsDialog::outputFolder() const
{
    return QDir::cleanPath(QDir::fromNativeSeparators(m_folderEdit->text().trimmed()));
}

void GenerateStepsDialog::accept()
{
    const QString folder = outputFolder();
    if (folder.isEmpty() || folder == QLatin1String(".")) {
        QMessageBox::warning(this, windowTitle(), tr("Choose a folder to save the steps in."));
        return;
    }
    // Steps from an earlier run would mix with the new ones in the sequence.
    if (!QDir(folder).isEmpty() && QFileInfo::exists(folder)) {
        QMessageBox::warning(this, windowTitle(),
                             tr("%1 is not empty. Choose a new or empty folder.").arg(QDir::toNativeSeparators(folder)));
        return;
    }
    QDialog::accept();
}

void GenerateStepsDialog::schedulePreview()
{
    m_previewStatus->setText(tr("Updating preview..."));
    m_previewTimer->start();
}

void GenerateStepsDialog::runPreview()
{
    const quint64 generation = ++m_previewGeneration;
    StepOptions previewOptions = options();
    previewOptions.maxSide = kPreviewSide;

    auto* watcher = new QFutureWatcher<QList<GeneratedStep>>(this);
    connect(watcher, &QFutureWatcher<QList<GeneratedStep>>::finished, this, [this, watcher, generation] {
        watcher->deleteLater();
        if (generation == m_previewGeneration)
            showPreview(watcher->result());
    });
    watcher->setFuture(QtConcurrent::run([source = m_previewSource, previewOptions] {
        QList<GeneratedStep> steps;
        generateSteps(source, previewOptions, [&](const GeneratedStep& step, int, int) {
            steps.append(step);
            return true;
        });
        return steps;
    }));
}

void GenerateStepsDialog::showPreview(const QList<GeneratedStep>& steps)
{
    m_previewList->clear();
    int region = 0;
    for (int i = 0; i < steps.size(); ++i) {
        QString label;
        if (steps[i].name == QLatin1String("outline"))
            label = tr("Outline");
        else if (steps[i].name == QLatin1String("original"))
            label = tr("Original");
        else
            label = tr("Region %1").arg(++region);
        m_previewList->addItem(new QListWidgetItem(QIcon(QPixmap::fromImage(steps[i].image)),
                                                   QStringLiteral("%1. %2").arg(i + 1).arg(label)));
    }
    m_previewStatus->setText(tr("%n step(s) will be created.", nullptr, int(steps.size())));
}

void GenerateStepsDialog::chooseFolder()
{
    const QString folder = QFileDialog::getExistingDirectory(this, tr("Save Steps To"),
                                                             QFileInfo(outputFolder()).absolutePath());
    if (!folder.isEmpty())
        m_folderEdit->setText(QDir::toNativeSeparators(folder));
}

} // namespace lumi
