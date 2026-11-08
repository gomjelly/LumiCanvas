#include "ui/KeySettingsDialog.h"

#include <QDialogButtonBox>
#include <QGridLayout>
#include <QHash>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

namespace lumi {

namespace {

constexpr int kKeysPerBinding = 2;

} // namespace

KeySettingsDialog::KeySettingsDialog(const QList<KeyBinding>& bindings, const QList<QKeySequence>& reserved,
                                     QWidget* parent)
    : QDialog(parent)
    , m_bindings(bindings)
    , m_reserved(reserved)
{
    setWindowTitle(tr("Key Settings"));

    auto* layout = new QVBoxLayout(this);
    auto* hint = new QLabel(tr("Click a box and press a key on the keyboard, presentation remote or foot pedal."));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* grid = new QGridLayout;
    for (int row = 0; row < m_bindings.size(); ++row) {
        const KeyBinding& binding = m_bindings[row];
        grid->addWidget(new QLabel(binding.label), row, 0);
        for (int k = 0; k < kKeysPerBinding; ++k) {
            auto* edit = new QKeySequenceEdit(binding.keys.value(k));
            edit->setMaximumSequenceLength(1);
            edit->setClearButtonEnabled(true);
            grid->addWidget(edit, row, k + 1);
            m_edits.append(edit);
        }
    }
    layout->addLayout(grid);

    auto* note = new QLabel(tr("Esc and Tab cannot be assigned. Arrow keys assigned here no longer move corners."));
    note->setWordWrap(true);
    note->setForegroundRole(QPalette::PlaceholderText);
    layout->addWidget(note);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel
                                         | QDialogButtonBox::RestoreDefaults);
    connect(buttons, &QDialogButtonBox::accepted, this, &KeySettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &KeySettingsDialog::reject);
    connect(buttons->button(QDialogButtonBox::RestoreDefaults), &QPushButton::clicked, this,
            &KeySettingsDialog::restoreDefaults);
    layout->addWidget(buttons);
}

QList<KeyBinding> KeySettingsDialog::bindings() const
{
    QList<KeyBinding> result = m_bindings;
    for (int row = 0; row < result.size(); ++row) {
        result[row].keys.clear();
        for (int k = 0; k < kKeysPerBinding; ++k) {
            const QKeySequence key = m_edits[row * kKeysPerBinding + k]->keySequence();
            if (!key.isEmpty())
                result[row].keys.append(key);
        }
    }
    return result;
}

void KeySettingsDialog::accept()
{
    const QList<KeyBinding> result = bindings();
    QHash<QKeySequence, QString> used;
    for (const KeyBinding& binding : result) {
        for (const QKeySequence& key : binding.keys) {
            const QString name = key.toString(QKeySequence::NativeText);
            if (m_reserved.contains(key)) {
                QMessageBox::warning(this, windowTitle(), tr("%1 is already used elsewhere in LumiCanvas.").arg(name));
                return;
            }
            if (const auto it = used.constFind(key); it != used.constEnd() && it.value() != binding.id) {
                QMessageBox::warning(this, windowTitle(), tr("%1 is assigned to more than one operation.").arg(name));
                return;
            }
            used.insert(key, binding.id);
        }
    }
    QDialog::accept();
}

void KeySettingsDialog::restoreDefaults()
{
    for (int row = 0; row < m_bindings.size(); ++row) {
        for (int k = 0; k < kKeysPerBinding; ++k)
            m_edits[row * kKeysPerBinding + k]->setKeySequence(m_bindings[row].defaults.value(k));
    }
}

} // namespace lumi
