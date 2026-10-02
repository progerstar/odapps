#include "qtoollineedit.h"

#include <QStyle>
#include <QSettings>
#include <QMimeData>
#include <QFileDialog>

#if QTOOLLINEEDIT_MATERIAL_ICONS
#include <qmaterialfont.h>
#endif

QToolLineEdit::QToolLineEdit(QWidget* parent, QToolLineEdit::Actions act) :
    QLineEdit(parent), fileFilters(tr("All Files (*.*)"))
{
    toolButton = new QToolButton(this);
    toolButton->setStyleSheet(QString("QToolButton { border: none; background:transparent; "
                                      "padding: 2px; margin: 0px; }"));
    toolButton->setIconSize(QSize(22, 22));
    toolButton->setCursor(Qt::PointingHandCursor);
    toolButton->hide();

    QMargins margins(0, 0, 26 , 0);
    setTextMargins(margins);
    setMinimumSize(minimumSizeHint().width() , 26);
    updateSize();

    setAction(act);
}

void QToolLineEdit::setAction(QToolLineEdit::Actions act)
{
    toolButton->disconnect();
    switch(act)
    {
        case Clear:
#if QTOOLLINEEDIT_MATERIAL_ICONS
            setupToolButton(tr("Clear"), MaterialIcon("backspace"));
#else
            setupToolButton(tr("Clear"), QIcon(":/qtoollineedit/images/edit-clear.ico"));
#endif

            connect(toolButton, SIGNAL(clicked()), this, SLOT(clear()));
            break;
        case Paste:
#if QTOOLLINEEDIT_MATERIAL_ICONS
            setupToolButton(tr("Paste"), MaterialIcon("content_paste"));
#else
            setupToolButton(tr("Paste"), QIcon(":/qtoollineedit/edit-paste.ico"));
#endif
            connect(toolButton, SIGNAL(clicked()), this, SLOT(paste()));
            break;
        case File:
#if QTOOLLINEEDIT_MATERIAL_ICONS
            setupToolButton(tr("Open"), MaterialIcon("upload"));
#else
            setupToolButton(tr("Open"), QIcon(":/qtoollineedit/images/document-open.ico"));
#endif
            connect(toolButton, SIGNAL(clicked()), this, SLOT(open()));
            break;
        case Dir:
#if QTOOLLINEEDIT_MATERIAL_ICONS
            setupToolButton(tr("Open"), MaterialIcon("folder_open"));
#else
            setupToolButton(tr("Open"), QIcon(":/qtoollineedit/images/document-open.ico"));
#endif
            connect(toolButton, SIGNAL(clicked()), this, SLOT(openDir()));
            break;
    }
}

void QToolLineEdit::open()
{
    QString newfile = QFileDialog::getOpenFileName(this,tr("Select File"),text(),fileFilters);
    if(newfile.isEmpty()) return;
    setText(newfile);
}

void QToolLineEdit::openDir()
{
    QString newDir = QFileDialog::getExistingDirectory(this, tr("Select Folder"), text());
    if(!newDir.isEmpty()) {
        setText(newDir);
    }
}

void QToolLineEdit::setupToolButton(const QString& toolTip,
                                    const QIcon& icon)
{
    if(!icon.isNull())
        setToolButtonIcon(icon);
    setToolButtonToolTip(toolTip);
}

void QToolLineEdit::setToolButtonToolTip(const QString& text)
{
    toolButton->setToolTip(text);
}

void QToolLineEdit::setToolButtonIcon(const QIcon& icon)
{
    toolButton->setIcon(icon);
}

void QToolLineEdit::resizeEvent(QResizeEvent* e)
{
    Q_UNUSED(e);
    updateSize();
}

void QToolLineEdit::enterEvent(QEvent* e)
{
    toolButton->setVisible(true);
    QLineEdit::enterEvent(e);
}
void QToolLineEdit::leaveEvent(QEvent* e)
{
    toolButton->setVisible(false);
    QLineEdit::leaveEvent(e);
}

void QToolLineEdit::dragEnterEvent(QDragEnterEvent* e)
{
    if(e->mimeData()->hasText())
    {
        e->acceptProposedAction();
    }
    else
    {
        QLineEdit::dragEnterEvent(e);
    }
}

void QToolLineEdit::dropEvent(QDropEvent* e)
{
    QString text = e->mimeData()->text();
    if(!text.isEmpty())
    {
        setText(text);
    }
    e->accept();
    return;
}

void QToolLineEdit::updateSize()
{
    QRect curRect = rect();
    //int iconSize = curRect.height() - 4;
    toolButton->setFixedSize(26, 26);
    toolButton->move(curRect.width() - 26 , (curRect.height() - toolButton->height()) / 2);
}
