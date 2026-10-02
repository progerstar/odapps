#ifndef MIFARECLASSICTRAILEREDITOR_H
#define MIFARECLASSICTRAILEREDITOR_H

#include <QDialog>

#include "mifare_global.h"

namespace Ui {
class MifareClassicTrailerEditor;
}

class MifareClassicKey;

class MifareClassicTrailerEditor : public QDialog
{
        Q_OBJECT

    public:
        enum Mode
        {
            TrailerEditor,
            BlockEditor
        };

        explicit MifareClassicTrailerEditor(QWidget *parent = 0);
        ~MifareClassicTrailerEditor();

        void setUIMode(Mode mode);
        inline Mode uiMode() const
        {
            return m_mode;
        }

        void setMode(quint8 mode, bool on);
        quint8 getMode() const;

        inline void setAccessKey(MifareClassicKeyType type)
        {
            m_acc_key = type;
        }

        void setKey(MifareClassicKeyType type, const MifareClassicKey& key);
        void setKeyAvailable(MifareClassicKeyType type, bool on);
        bool keyModified(MifareClassicKeyType type) const;
        MifareClassicKey getKey(MifareClassicKeyType type) const;
    private slots:
        void on_trailerModeCombo_currentIndexChanged(int index);
    private:
        Ui::MifareClassicTrailerEditor *ui;
        Mode m_mode;
        MifareClassicKeyType m_acc_key;
};

#endif // MIFARECLASSICTRAILEREDITOR_H
