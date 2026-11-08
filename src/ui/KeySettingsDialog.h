#pragma once

#include <QDialog>
#include <QKeySequence>
#include <QList>
#include <QString>

class QKeySequenceEdit;

namespace lumi {

// One rebindable operation, with up to two keys. A presentation remote or a
// foot pedal is just another key, so this is how those devices get set up.
struct KeyBinding {
    QString id;
    QString label;
    QList<QKeySequence> defaults;
    QList<QKeySequence> keys;
};

class KeySettingsDialog : public QDialog {
    Q_OBJECT

public:
    // Keys in reserved are refused because the app already uses them elsewhere.
    KeySettingsDialog(const QList<KeyBinding>& bindings, const QList<QKeySequence>& reserved,
                      QWidget* parent = nullptr);

    QList<KeyBinding> bindings() const;

    void accept() override;

private:
    void restoreDefaults();

    QList<KeyBinding> m_bindings;
    QList<QKeySequence> m_reserved;
    QList<QKeySequenceEdit*> m_edits; // two per binding
};

} // namespace lumi
