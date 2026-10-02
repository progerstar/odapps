#include "rfidautomodedialog.h"
#include <ui_rfidautomodedialog.h>

#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QInputDialog>
#include <QApplication>
#include <QClipboard>

#include <themedetector.h>
#include <qmaterialfont.h>
#include <rfidcardreaderinterface.h>

/*
 * Language:
 * one instruction per line
 * - wait card
 * - var = "text"
 * - get @var "Label"
 * - if @var == "value" then
 * -  <true>
 * - else
 * -  <false>
 *   fi
 * - label:
 * - goto label
 *
 * - copy uid
 * - key <A/B> <XXXXXXXXXXXX>
 * - password <XXXXXXXX>
 * - check uid <hex>
 * - check block <N> <hex>
 * - read <block> / [range] / (numbers) - (clipboard) / <file> / @var
 * - write <block> - (clipboard) / <file> / @var
 */

RFIDAutoModeDialog::RFIDAutoModeDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::RFIDAutoModeDialog),
    state(Idle), ecode(Finished), _op(nullptr)
{
    ui->setupUi(this);
    MaterialUI("close", ui->closeTool, ThemeDetector::iconColor());
    connect(ui->closeTool,SIGNAL(clicked()),this,SLOT(close()));
}

RFIDAutoModeDialog::~RFIDAutoModeDialog()
{
    _op = nullptr;
    delete ui;
}

bool RFIDAutoModeDialog::setOperator(RFIDCardReaderOperator *ptr)
{
    if(ptr->type() != RFIDCardReaderInterface::RFIDReader_Hardware)
    {
        qWarning()<<"AutoMode does not support simulator readers";
        return false;
    }

    if(_op)
    {
        _op->disconnect(this);
        _op = nullptr;
    }

    _op = ptr;
    connect(_op, &RFIDCardReaderOperator::cardStateChanged, this, &RFIDAutoModeDialog::cardStateChanged);
    connect(_op, &RFIDCardReaderOperator::readerValid, this, &RFIDAutoModeDialog::readerValid);
    connect(_op, &RFIDCardReaderOperator::readerVersion, this, &RFIDAutoModeDialog::readerVersion);
    connect(_op, &RFIDCardReaderOperator::passwordChanged, this, &RFIDAutoModeDialog::passwordChanged);
    connect(_op, &RFIDCardReaderOperator::packReceived, this, &RFIDAutoModeDialog::packReceived);
    connect(_op, &RFIDCardReaderOperator::cardDetected, this, &RFIDAutoModeDialog::cardDetected);
    connect(_op, &RFIDCardReaderOperator::cardUidChanged, this, &RFIDAutoModeDialog::cardUidChanged);
    connect(_op, &RFIDCardReaderOperator::blockRead, this, &RFIDAutoModeDialog::blockRead);

#warning "connect to CardStateChanged"
    //connect(_op, &RFIDCardReaderOperator::cardRemoved, this, &RFIDAutoModeDialog::cardRemoved);
    //connect(_op, &RFIDCardReaderOperator::readFinished, this, &RFIDAutoModeDialog::readFinished);
    //connect(_op, &RFIDCardReaderOperator::writeFinished, this, &RFIDAutoModeDialog::writeFinished);

    connect(_op, &RFIDCardReaderOperator::blockWritten, this, &RFIDAutoModeDialog::blockWritten);
    connect(_op, &RFIDCardReaderOperator::error, this, &RFIDAutoModeDialog::operatorError);

    return true;
}

void RFIDAutoModeDialog::on_runButton_clicked()
{
    currentLine = 0;
    variableHash.clear();
    labelHash.clear();

    scriptFile.setFileName(ui->scriptEdit->text());
    if(!scriptFile.open(QFile::ReadOnly))
    {
        QMessageBox::warning(this,tr("Open Script"),tr("File %1 is not readable:<br>%2")
                             .arg(scriptFile.fileName()).arg(scriptFile.errorString()));
        return;
    }

    ui->runButton->setText(tr("Stop"));
    nextScriptLine();
}

void RFIDAutoModeDialog::readerValid(bool on)
{
    if(!on)
    {
        QMessageBox::warning(this,tr("Automatic Mode"),
                             tr("Connection to the reader has been unexpectedly interrupted"));
        //do not own reader;
        _op=nullptr;
        ecode = Disconnected;
        reject();
    }
}
void RFIDAutoModeDialog::readerVersion(const QString& v)
{
    ui->logBrowser->append(tr("Reader v %1").arg(v));
}

void RFIDAutoModeDialog::passwordChanged(bool success, quint32 pwd)
{

}

void RFIDAutoModeDialog::packReceived(bool success, quint16 pack)
{

}

void RFIDAutoModeDialog::cardStateChanged(RFIDCardReaderOperator::CardState state)
{
    switch(state)
    {
        case RFIDCardReaderOperator::CS_WriteFailed:
        case RFIDCardReaderOperator::CS_ReadFailed:
        {
            //key failed / invalid / etc
            if(state != RFIDCardReaderOperator::CS_Idle)
            {
                QMessageBox::warning(this,tr("RFID Script"),
                                     tr("Tag read / write failure!"));
                scriptCanceled();
            }
            break;
        }
        default:break;
    }
}

void RFIDAutoModeDialog::cardDetected(const QByteArray& uid, int type)
{
    ui->logBrowser->append(tr("New card: %1").arg(QString::fromLatin1(uid.toHex().toUpper())));
    qWarning()<<"Got card "<<uid;
    if(state == Wait_Card)
    {
        nextScriptLine();
    }
}

void RFIDAutoModeDialog::cardRemoved()
{
    qWarning()<<"Card removed";
    if(state != Wait_Card)
    {
        //unexpectedly
        QMessageBox::warning(this,tr("RFID Script"),
                             tr("Card has been unexpectedly removed. Abort."));
        scriptCanceled();
        return;
    }
    qWarning()<<"Waiting for a new card";
}

void RFIDAutoModeDialog::cardUidChanged()
{

}

void RFIDAutoModeDialog::blockRead(int number, QByteArray data, int result)
{
    if(result != RFIDCardReaderInterface::RW_OK)
    {
        qWarning()<<"Block "<<number<<" read error";
        scriptCanceled();
        return;
    }
#warning "stub: process read data (i.e. the currentLine)"
}

void RFIDAutoModeDialog::readFinished()
{

}

void RFIDAutoModeDialog::blockWritten(int number, int code)
{
    if(code != RFIDCardReaderInterface::RW_OK)
    {
        qWarning()<<"Block "<<number<<" write error";
        scriptCanceled();
        return;
    }
}

void RFIDAutoModeDialog::writeFinished(bool success)
{

}

void RFIDAutoModeDialog::operatorError(RFIDCardReaderOperator::Level lev, const QString& title, const QString& msg)
{

}

QString RFIDAutoModeDialog::getNextLine(bool& ok)
{
    if(!scriptFile.isOpen()||scriptFile.atEnd())
    {
        ok = false;
        return QString();
    }

    QString line;
    while(!scriptFile.atEnd()&&
          (
              (line=QString::fromUtf8(scriptFile.readLine())).isEmpty()||
              (line.trimmed().startsWith("#"))
              )
          )
    {
        ++currentLine;
    }

    if(line.isEmpty() || line.trimmed().startsWith("#"))
    {
        ok = false;
        return QString();
    }
#warning "CHECK \\r\\n removed!"
    ++currentLine;
    ok = true;
    return line.trimmed();
}

void RFIDAutoModeDialog::scriptEOD()
{
#warning "stub"
}

void RFIDAutoModeDialog::scriptCanceled()
{
#warning "stub"
}

void RFIDAutoModeDialog::invalidLine(const QString& reason)
{
#warning "stub"
}

void RFIDAutoModeDialog::processConditionalBlock(const QStringList& block)
{
#warning "stub"
}

void RFIDAutoModeDialog::nextScriptLine()
{
    bool ok;
    QString line = getNextLine(ok);
    if(!ok)
    {
        scriptEOD();
        return;
    }

    if(line=="wait card")
    {
        state = Wait_Card;
    }
    else if(line.startsWith("get "))
    {
        int space = 5;
        while((space < line.size())&&(line.at(space).isSpace())) ++space;
        int space1 = line.indexOf(' ',space);
        if(space1==-1)
        {
            invalidLine(tr("Syntax: get @var \"Label text (optional)\""));
            return;
        }
        QString var_name = line.mid(space,space1-space);
        while((space1<line.size())&&(line.at(space1).isSpace())) ++space1;
        QString label;
        if(space1<line.size())
        {
            label = line.mid(space1);
            if(label.startsWith("\"")&&label.endsWith("\""))
            {
                label = label.mid(1,label.size()-2);
            }
        }
        else
        {
            label = tr("Input \"%1\":").arg(var_name);
        }
        QString var_value = QInputDialog::getText(this,tr("RFID Script"),label,
                                                  QLineEdit::Normal,variableHash.value(var_name),&ok);
        if(!ok)
        {
            scriptCanceled();
            return;
        }
        variableHash[var_name] = var_value;
    }
    else if(line.startsWith("if ") && line.endsWith(" then"))
    {
        //syntax: 'if <condition> then'

        //assemble full block
        int ifStart = currentLine;
        QStringList if_else_block = QStringList()<<line;
        do{
            line = getNextLine(ok);
            if(ok) if_else_block.append(line);
        }while(ok && (line != "fi"));

        if(!ok || (line != "fi"))
        {
            invalidLine(tr("No closing \"fi\" for \"if\" at line %1").arg(ifStart));
            return;
        }
        processConditionalBlock(if_else_block);
    }
    else if(line.endsWith(":"))
    {
        line.chop(1);
        labelHash.insert(line.isEmpty() ? QString("!%1").arg(currentLine) : line,currentLine+1);
    }
    else if(line.startsWith("goto "))
    {
        line = line.mid(5).trimmed();
        if(!labelHash.contains(line))
        {
            invalidLine(tr("Undefined label %1").arg(line));
            return;
        }

        if(!gotoLine(labelHash.value(line,-1)))
        {
            invalidLine(tr("Label %1 location not found").arg(line));
        }
    }
    else if(line=="copy uid")
    {
        if(_op)
        {
            qApp->clipboard()->setText(_op->uidString());
        }
    }
    else
    {
#warning "stub"
    }
}

bool RFIDAutoModeDialog::gotoLine(int n)
{
    if(n<=0)
    {
        return false;
    }

    currentLine = 0;
    scriptFile.seek(0);

    while(!scriptFile.atEnd() && ((currentLine+1) != n))
    {
        (void)scriptFile.readLine();
        ++currentLine;
    }

    //n is too big
    return !scriptFile.atEnd();
}
