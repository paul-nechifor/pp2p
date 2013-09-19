#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <QtGui>
#include <QtXml>
#include <QRegExp>
#include "FereastraPrincipala.h"


void ThreadDescarcare::run()
{
    for (;;)
    {
        fp->descarcaCeva();
        msleep(100);
    }
}
void ThreadIncarcare::run()
{
    fp->buclaIncarcare(*this);
}
void ThreadIncarcare::somnUsor(int ms)
{
    msleep(ms);
}

FereastraPrincipala::FereastraPrincipala(QWidget* parent) : QMainWindow(parent)
{
    QWidget* cw = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout();
        tabw = new QTabWidget();
        tabw->addTab(creazaTabConectare(), "Conectare");
        tabw->addTab(creazaTabCautare(), QString::fromUtf8("Căutare"));
        tabw->addTab(creazaTabDescarcari(), QString::fromUtf8("Descărcări"));
        pbDeconectare->setEnabled(false);
        tabw->widget(1)->setEnabled(false);
        tabw->widget(2)->setEnabled(false);
    layout->addWidget(tabw);
    cw->setLayout(layout);
    
    resize(700,500);
    setWindowTitle("PP2P");
    statusBar();
    setCentralWidget(cw);

    iaDateDinSetari();

    descarcareCurenta = 0;
    td.fp = this;
    td.start();
    ti.fp = this;
    ti.start();
}
FereastraPrincipala::~FereastraPrincipala()
{
    td.exit();
    ti.exit();
}
// la fiecare iterație, funcția asta trebuie să descarce primul fișier care-l găsește
void FereastraPrincipala::descarcaCeva()
{   
    if (descarcareCurenta >= descarcari.size())
        return;

    int sd;
    struct sockaddr_in server;
    double raport;
    Descarcare& dc = descarcari[descarcareCurenta];
    QDir uita = QDir(dc.locatie);
    QString fisier = uita.dirName(); 

    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        diep("Eroare la crearea socket-ului in client: ");

    QStringList parti = dc.ipport.split(":");
    char* adresa = parti[0].toAscii().data();
    int port = parti[1].toInt();

    server.sin_family = AF_INET;
    server.sin_addr.s_addr = inet_addr(adresa);
    server.sin_port = htons(port);

    //statusBar()->showMessage(QString::fromUtf8("Acum descarc „%1” de la %2 (%3).")
    //        .arg(fisier).arg(dc.nume).arg(dc.ipport));

    qDebug() << "Incerc sa ma conectez la" << dc.ipport;

    int r = -1;
    for (int i = 1<<13; i < 1<<23 && r == -1; i<<=1)
    {
        r = ::connect(sd, (struct sockaddr*)&server, sizeof(struct sockaddr));
        usleep(i);
    }

    // dacă n-am reușit să mă conectez
    if (r == -1)
    {
        dc.rand->setText("Eroare");
        qDebug() << "E de rau";
        //mesajEroare(QString::fromUtf8("Nu am putut să mă conectez la %1. Trec la următorul fișier.").arg(dc.ipport));
        descarcareCurenta++;
        return;
    }
    qDebug() << "E de bine";

    // îi trimit ce vreau să iau
    QDomDocument doc;
    QDomElement root = doc.createElement("fisier");
    root.setAttribute("locatie", dc.locatie);
    root.setAttribute("marime", dc.marime);
    root.setAttribute("md5", dc.md5);
    doc.appendChild(root);

    char* m = doc.toString().toUtf8().data();
    trimite_mesaj_fix(sd, m);

    char rasp[100];
    primeste_mesaj(sd, rasp);
    int nrasp = atoi(rasp); // mărimea fișierului, -1 dacă nu-l are

    qDebug() << "El spune ca are" << nrasp;

    if (nrasp == -1)
    {
        mesajEroare(QString::fromUtf8("%1 spune că nu are fișierul „%2”. Trec la următorul fișier.")
                .arg(dc.nume).arg(fisier));
        descarcareCurenta++;
        return;
    }

    char buf[102400];
    QString unde = leDescarcare->text() + "/" + fisier;
    qDebug() << "Icep sa descarc in " << unde;
    QFile f(unde);
    if (!f.open(QIODevice::WriteOnly))
        die("Eroare la deschiderea fișierului pentru scriere.");

    while (dc.luat < dc.marime)
    {
        int n = ::read(sd, buf, sizeof(buf));

        if (n == -1) diep("Nu am putut să citesc: ");
        if (n == 0) die("S-a terminat prematur.");

        f.write(buf, n);
        dc.luat += n;

        raport = (dc.luat * 100.0) / dc.marime;
        dc.rand->setText(QString("%L1").arg(raport, 0, 'f', 2));
    }

    // după ce am terminat de descarcate asta
    f.close();
    ::close(sd);
    descarcareCurenta++;
}
void FereastraPrincipala::buclaIncarcare(ThreadIncarcare& inc)
{
    //pornește serverul de încarcare
    int sd, r;
    portTrimitere = 1337; // primul port de încercat

    do
    {
        struct sockaddr_in server;

        if (( sd = ::socket(AF_INET, SOCK_STREAM, 0) ) == -1)
            diep("Eroare la crearea socket-ului pentru trimitere: ");

        umpleStructuraServer(&server, NULL, portTrimitere);
        refolosesteAdresa(sd);

        qDebug() << "Incerc cu portul" << portTrimitere;

        /* atasez socket-ul */
        r = ::bind(sd, (struct sockaddr*)&server, sizeof(struct sockaddr));
        if (r == -1)
        {
            if (errno == EADDRINUSE)
            {
                portTrimitere++;
                ::close(sd);
            }
            else diep("Eroare la atasarea socket-ului: ");
        }
    
    } while (r == -1);

    faNonBlocant(sd);

    if (listen(sd, 10) == -1)
        diep("Eroare la ascultare: ");

    qDebug() << "Am deschis serverul de trimitere la portul" << portTrimitere;

    QList<Trimitere> trim;
    for (;;)
    {
        struct sockaddr_in de_la;
        socklen_t lungime = sizeof(de_la);
        int x, clientSd = accept(sd, (struct sockaddr*)&de_la, &lungime);

        if (clientSd == -1)
        {
            if (errno != EAGAIN) diep("Ceva p-acolo: ");
        }
        else
        {
            char rasp[1024];
            primeste_mesaj(clientSd, rasp);

            QDomDocument doc;
            doc.setContent(QString::fromUtf8(rasp));
            QDomElement root = doc.documentElement();

            qDebug() << "Am primit o cerere pentru fișierul cu locația" << root.attribute("locatie");

            QString loc = root.attribute("locatie");
            bool gasit = false;
            for (int i = 0; i < fisiereGasite.size(); i++)
                if (fisiereGasite[i].locatie == loc)
                {
                    gasit = true;
                    break;
                }

            if (gasit)
            {
                Trimitere t;
                t.sd = clientSd;
                t.f = new QFile(loc);
                if (!t.f->exists())
                    die("Fișierul %s nu există.", loc.toUtf8().data());
                if (!t.f->open(QIODevice::ReadOnly))
                    die("Nu am putut deschide fișierul %s.", loc.toUtf8().data());
                trim.append(t);
                
                char* m = root.attribute("marime").toAscii().data();
                x = ::write(clientSd, m, strlen(m) + 1);
            }
            else
            {
                x = ::write(clientSd, "-1", 3);
                ::close(clientSd);
            }
        }

        for (int i = 0; i < trim.size(); i++)
        {
            char buf[102400];
            qint64 r = trim[i].f->read(buf, sizeof(buf));

            if (r == -1) die("Nu am putut citi.");
            if (r == 0)
            {
                trim[i].f->close();
                delete trim[i].f;
                ::close(trim[i].sd);
                trim.removeAt(i);
                break; // pentru că am eliminat unul
            }
            else
            {
                x = ::write(trim[i].sd, buf, r);
            }
        }
        
        inc.somnUsor(10);
    }
}
QWidget* FereastraPrincipala::creazaTabConectare()
{
    QWidget* ret = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout();

        wValori = new QWidget();
        QGridLayout* glValori = new QGridLayout();
        leNume = new QLineEdit();
        leImpartit = new QLineEdit();
        QPushButton* pbImpartit = new QPushButton("Navigare");
        leDescarcare = new QLineEdit();
        QPushButton* pbDescarcare = new QPushButton("Navigare");
        leServer = new QLineEdit();
        glValori->addWidget(new QLabel("Nume:"), 0, 0);
        glValori->addWidget(leNume, 0, 1);
        glValori->addWidget(new QLabel(QString::fromUtf8("Dosar de împărțit:")), 1, 0);
        glValori->addWidget(leImpartit, 1, 1);
        glValori->addWidget(pbImpartit, 1, 2);
        glValori->addWidget(new QLabel("Dosar de descarcare:"), 2, 0);
        glValori->addWidget(leDescarcare, 2, 1);
        glValori->addWidget(pbDescarcare, 2, 2);
        glValori->addWidget(new QLabel("Server:"), 3, 0);
        glValori->addWidget(leServer, 3, 1);
        wValori->setLayout(glValori);

        QHBoxLayout* hblConectare = new QHBoxLayout();
        pbConectare = new QPushButton("Conectare");
        pbDeconectare = new QPushButton("Deconectare");
        hblConectare->addStretch();
        hblConectare->addWidget(pbConectare);
        hblConectare->addWidget(pbDeconectare);
        hblConectare->addStretch();
        
    layout->addWidget(wValori);
    layout->addLayout(hblConectare);
    layout->addStretch();
    ret->setLayout(layout);

    connect(pbImpartit, SIGNAL(clicked()), this, SLOT(alegeDosarDeImpartit()));
    connect(pbDescarcare, SIGNAL(clicked()), this, SLOT(alegeDosarDeDescarcare()));
    connect(pbConectare, SIGNAL(clicked()), this, SLOT(apasatConectare()));
    connect(pbDeconectare, SIGNAL(clicked()), this, SLOT(apasatDeconectare()));

    return ret;
}
QWidget* FereastraPrincipala::creazaTabCautare()
{
    QWidget* ret = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout();

        QGridLayout* glCauta = new QGridLayout();
        leCauta = new QLineEdit();
        leInterval = new QLineEdit();
        leMD5 = new QLineEdit();
        QPushButton* pbCauta = new QPushButton(QString::fromUtf8("Caută"));
        glCauta->addWidget(new QLabel("Nume:"), 0, 0);
        glCauta->addWidget(leCauta, 0, 1, 1, 3);
        glCauta->addWidget(new QLabel("Interval:"), 1, 0);
        glCauta->addWidget(leInterval, 1, 1);
        glCauta->addWidget(new QLabel("MD5:"), 1, 2);
        glCauta->addWidget(leMD5, 1, 3);
        glCauta->addWidget(pbCauta, 0, 4, 2, 1);

        tabel = new QTableWidget(0, 5);
        QHeaderView* hv = tabel->horizontalHeader();
        hv->resizeSection(0, 236);
        hv->resizeSection(1, 78);
        hv->resizeSection(2, 72);
        hv->resizeSection(3, 110);
        hv->resizeSection(4, 142);
        tabel->setSelectionBehavior(QAbstractItemView::SelectRows);
        QStringList labels;
        labels << QString::fromUtf8("Fișier") << QString::fromUtf8("Mărime") << "MD5" << "Nume" << "IP:Port";
        tabel->setHorizontalHeaderLabels(labels);
        tabel->verticalHeader()->hide();

        QHBoxLayout* hblDescarca = new QHBoxLayout();
        QPushButton* pbDescarca = new QPushButton(QString::fromUtf8("Descarcă"));
        hblDescarca->addStretch();
        hblDescarca->addWidget(pbDescarca);
        hblDescarca->addStretch();

    layout->addLayout(glCauta);
    layout->addWidget(tabel);
    layout->addLayout(hblDescarca);
    ret->setLayout(layout);

    connect(pbCauta, SIGNAL(clicked()), this, SLOT(apasatCauta()));
    connect(pbDescarca, SIGNAL(clicked()), this, SLOT(apasatDescarca()));

    return ret;
}
QWidget* FereastraPrincipala::creazaTabDescarcari()
{
    QWidget* ret = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout();

        tabelDesc = new QTableWidget(0, 6);
        QHeaderView* hv = tabelDesc->horizontalHeader();
        hv->resizeSection(0, 220);
        hv->resizeSection(1, 50);
        hv->resizeSection(2, 78);
        hv->resizeSection(3, 64);
        hv->resizeSection(4, 90);
        hv->resizeSection(5, 142);
        tabelDesc->setSelectionBehavior(QAbstractItemView::SelectRows);
        QStringList labels;
        labels << QString::fromUtf8("Fișier") << "Procent" << QString::fromUtf8("Mărime") << "MD5" << "Nume" << "IP:Port";
        tabelDesc->setHorizontalHeaderLabels(labels);
        tabelDesc->verticalHeader()->hide();

    layout->addWidget(tabelDesc);
    ret->setLayout(layout);
    return ret;
}
void FereastraPrincipala::gasesteFisiere(QDir& dir)
{
    QFileInfoList l = dir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs | QDir::Files);
    for (int i = 0; i < l.size(); i++)
    {
        if (l[i].isDir())
        {
            QDir d(l[i].absoluteFilePath());
            gasesteFisiere(d);
        }
        else
        {
            Fisier f;
            f.locatie = l[i].absoluteFilePath();
            f.marime = l[i].size();
            f.md5 = "";

            fisiereGasite.append(f);
            statusBar()->showMessage(QString::fromUtf8("Am găsit %1 fișiere deocamdată.").arg(fisiereGasite.size()));
        }
    }
}
void FereastraPrincipala::alegeDosarDeImpartit()
{
    QString s = QFileDialog::getExistingDirectory(this, "Alege dosarul", leImpartit->text(), QFileDialog::ShowDirsOnly);
    if (s != "") leImpartit->setText(s);
}
void FereastraPrincipala::alegeDosarDeDescarcare()
{
    QString s = QFileDialog::getExistingDirectory(this, "Alege dosarul", leDescarcare->text(), QFileDialog::ShowDirsOnly);
    if (s != "") leDescarcare->setText(s);
}
void FereastraPrincipala::apasatConectare()
{
    QString text = "";

    if (leNume->text() == "")
        text = QString::fromUtf8("Nu ți-ai ales un nume.\n");

    if (leImpartit->text() == "")
        text += QString::fromUtf8("Nu ai ales dosarul de împărțit.\n");
    else
    {
        QDir d(leImpartit->text());
        if (!d.exists()) text += QString::fromUtf8("Nu există directorul de împărțit.\n");
    }

    if (leDescarcare->text() == "")
        text += QString::fromUtf8("Nu ai ales dosarul de descărcare.\n");
    else
    {
        QDir d(leDescarcare->text());
        if (!d.exists()) text += QString::fromUtf8("Nu există directorul de descarcare.\n");
    }

    if (leServer->text() == "")
        text += QString::fromUtf8("Nu ai ales adresa și portul server-ului.\n");
    else
    {
        char server[200];
        int a=-1, b=-1, c=-1, d=-1, p=-1;
        strcpy(server, leServer->text().toAscii().data());
        sscanf(server, "%d.%d.%d.%d:%d", &a, &b, &c, &d, &p);
        if (p == -1)
            text += QString::fromUtf8("Adresa nu este corectă. Trebuie să fie de forma 123.100.150.200:9999.\n");
    }

    if (text != "")
    {
        mesajEroare(text);
        return;
    }
    else
    {
        QDir d(leImpartit->text());
        gasesteFisiere(d);

        if (existaSetari)
        {
            // nu este metoda cea mai bună, dar...
            for (int i = 0; i < fisiereGasite.size(); i++)
                for (int j = 0; j < fisiereGasiteSetari.size(); j++)
                    if (fisiereGasite[i].locatie == fisiereGasiteSetari[j].locatie)
                    {
                        fisiereGasite[i].md5 = fisiereGasiteSetari[j].md5;
                        break;
                    }
            fisiereGasiteSetari.clear();
        }
        calculeazaMD5();

        scrieFisierulSetari();

        pbConectare->setEnabled(false);
        wValori->setEnabled(false);
        pbDeconectare->setEnabled(true);

        // Aici incepe conectarea la server

        struct sockaddr_in server;

        if ((socketServ = socket(AF_INET, SOCK_STREAM, 0)) == -1)
            diep("Eroare la crearea socket-ului in client: ");

        QStringList parti = leServer->text().split(":");
        char* adresa = parti[0].toAscii().data();
        int port = parti[1].toInt();

        server.sin_family = AF_INET;
        server.sin_addr.s_addr = inet_addr(adresa);
        server.sin_port = htons(port);

        statusBar()->showMessage(QString::fromUtf8("Încerc să mă conectez la %1.").arg(leServer->text()));

        int r = -1;
        for (int i = 1<<13; i < 1<<23 && r == -1; i<<=1)
        {
            r = ::connect(socketServ, (struct sockaddr*)&server, sizeof(struct sockaddr)); // global connect
            usleep(i);
        }

        // dacă n-am reușit să mă conectez
        if (r == -1)
        {
            statusBar()->showMessage(QString::fromUtf8("Nu am reușit să mă conectez la server."));
            pbConectare->setEnabled(true);
            wValori->setEnabled(true);
            pbDeconectare->setEnabled(false);
            socketServ = -1;

            fisiereGasite.clear();
            iaDateDinSetari();
        }
        else
        {
            char* m = reprezentareaXml().toUtf8().data();
            trimite_mesaj_fix(socketServ, m);

            // activez widget-urile după conectare
            tabw->widget(1)->setEnabled(true);
            tabw->widget(2)->setEnabled(true);
            tabw->setCurrentIndex(1);
            statusBar()->showMessage("M-am conectat la server.");
        }
    }
}
void FereastraPrincipala::apasatDeconectare()
{
    leCauta->setText("");
    leInterval->setText("");
    leMD5->setText("");
    for (int i = tabel->rowCount() - 1; i >= 0; i--)
        tabel->removeRow(i);

    fisiereGasite.clear();
    fisiereGasiteSetari.clear();
    rezultate.clear();
    iaDateDinSetari();
    
    pbConectare->setEnabled(true);
    pbDeconectare->setEnabled(false);
    wValori->setEnabled(true);
    tabw->widget(1)->setEnabled(false);
    tabw->widget(2)->setEnabled(false);

    ::close(socketServ);
}
void FereastraPrincipala::calculeazaMD5()
{
    int size = fisiereGasite.size();
    for (int i = 0; i < size; i++)
    {
        if (fisiereGasite[i].md5 != "") continue;
        fisiereGasite[i].md5 = md5(fisiereGasite[i].locatie);
        statusBar()->showMessage(QString::fromUtf8("Am calculat suma MD5 pentru %1 fișiere din %2.").arg(i+1).arg(size));
    }
}
void FereastraPrincipala::iaDateDinSetari()
{
    QFile f(QDir::homePath() + "/setari.xml");
    if (!f.exists())
    {
        existaSetari = false;
        leNume->setText("Nimeni");
        leImpartit->setText(QDir::homePath());
        leDescarcare->setText(QDir::homePath());
        leServer->setText("127.0.0.1:11235");
        return;
    }
    existaSetari = true;

    QDomDocument doc;
    if (!f.open(QIODevice::ReadOnly))
        die("Nu am putut deschide fișierul setari.xml.");
    if (!doc.setContent(&f))
        die("Nu a mers setContent().");
    f.close();

    QDomElement root = doc.documentElement();
    leNume->setText(root.attribute("nume"));
    leImpartit->setText(root.attribute("impartit"));
    leDescarcare->setText(root.attribute("descarcare"));
    leServer->setText(root.attribute("server"));

    QDomNodeList fisiere = root.childNodes();
    for (unsigned i = 0; i < fisiere.length(); i++)
    {
        Fisier a;
        a.locatie = fisiere.at(i).toElement().attribute("locatie");
        a.marime = fisiere.at(i).toElement().attribute("marime").toLongLong();
        a.md5 = fisiere.at(i).toElement().attribute("md5");
        fisiereGasiteSetari.append(a);
    }
}
void FereastraPrincipala::scrieFisierulSetari()
{
    QDomDocument doc;
    QDomElement root = doc.createElement("setari");
    doc.appendChild(root);
    root.setAttribute("nume", leNume->text());
    root.setAttribute("impartit", leImpartit->text());
    root.setAttribute("descarcare", leDescarcare->text());
    root.setAttribute("server", leServer->text());

    for (int i = 0; i < fisiereGasite.size(); i++)
    {
        QDomElement e = doc.createElement("fisier");
        e.setAttribute("locatie", fisiereGasite[i].locatie);
        e.setAttribute("marime", fisiereGasite[i].marime);
        e.setAttribute("md5", fisiereGasite[i].md5);
        root.appendChild(e);
    }

    QFile setari(QDir::homePath() + "/setari.xml");
    if (!setari.open(QIODevice::WriteOnly))
        die("Eroare la deschiderea fișierului setari.xml");

    setari.write(doc.toString().toUtf8());
    setari.close();
}
QString FereastraPrincipala::reprezentareaXml()
{
    QDomDocument doc;
    QDomElement root = doc.createElement("conectare");
    doc.appendChild(root);
    root.setAttribute("nume", leNume->text());
    root.setAttribute("port", portTrimitere);

    for (int i = 0; i < fisiereGasite.size(); i++)
    {
        QDomElement e = doc.createElement("fisier");
        e.setAttribute("locatie", fisiereGasite[i].locatie);
        e.setAttribute("marime", fisiereGasite[i].marime);
        e.setAttribute("md5", fisiereGasite[i].md5);
        root.appendChild(e);
    }

    return doc.toString();
}
void FereastraPrincipala::amPrimitRezultatele(QString xml)
{

    // golesc lista curentă
    for (int i = tabel->rowCount() - 1; i >= 0; i--)
        tabel->removeRow(i);

    int nrRezultate = 0;
    QDomDocument doc;
    doc.setContent(xml);
    QDomElement root = doc.documentElement();

    QDomNodeList colegi = root.childNodes();
    for (unsigned i = 0; i < colegi.length(); i++)
    {
        QDomElement e = colegi.at(i).toElement();
        QString nume = e.attribute("nume");
        QString ipport = e.attribute("ipport");

        QDomNodeList fisiere = e.childNodes();
        nrRezultate += fisiere.length();
        for (unsigned j = 0; j < fisiere.length(); j++)
        {
            QDomElement f = fisiere.at(j).toElement();
            QStringList l;
            l << f.attribute("locatie") << f.attribute("marime") << f.attribute("md5") << nume << ipport;
            rezultate.append(l);
            insereazaRand(l);
        }
    }

    statusBar()->showMessage(QString::fromUtf8("Au fost găsite %1 rezultate de la %2 colegi.").arg(nrRezultate).arg(colegi.length()));
}
void FereastraPrincipala::apasatCauta()
{
    QString eroare = "";
    QString nume = leCauta->text(), inter = leInterval->text(), md5 = leMD5->text();

    if (nume == "" && inter == "" && md5 == "")
        eroare += QString::fromUtf8("Toate câmpurile sunt goale.\n");
    if (md5 != "" && (inter != "" || nume != ""))
        eroare += QString::fromUtf8("Dacă cauți după MD5, nu poți să cauți după nume sau după mărime.\n");
    if (nume != "")
    {
        QRegExp ex(nume);
        if (!ex.isValid())
            eroare += QString::fromUtf8("Expresia regulată nu este validă.\n");
    }
    if (inter != "")
    {
        QRegExp ex("([0-9]*-[0-9]+|[0-9]+-[0-9]*)");
        if (!ex.exactMatch(inter))
            eroare += QString::fromUtf8("Intervalul nu este valid. Trebuie să fie de forma „x-y”, „-y” sau „x-”.\n");
        // TODO trebuie să verific dacă numerele sunt în ordine XXX
    }
    if (md5 != "")
    {
        QRegExp ex("[0-9a-fA-F]{32}");
        if (!ex.exactMatch(md5))
            eroare += QString::fromUtf8("Hash-ul MD5 nu este valid.\n");
    }

    if (eroare != "")
    {
        mesajEroare(eroare);
        return;
    }
    else
    {
        rezultate.clear();

        statusBar()->showMessage(QString::fromUtf8("Caut rezultate."), 2000);

        QDomDocument doc;
        QDomElement root = doc.createElement("cauta");
        doc.appendChild(root);

        if (md5 != "") root.setAttribute("md5", md5);
        if (nume != "") root.setAttribute("nume", nume);
        if (inter != "") root.setAttribute("interval", inter);

        // trimit asta la server
        char *m = doc.toString().toUtf8().data();
        trimite_mesaj_fix(socketServ, m);

        char rasp[50000];
        primeste_mesaj(socketServ, rasp);

        QString qRasp = QString::fromUtf8(rasp);
        amPrimitRezultatele(qRasp);
    }
}
void FereastraPrincipala::apasatDescarca()
{
    QModelIndexList l = tabel->selectionModel()->selection().indexes();
    QList<int> randuri;
    for (int i = 0; i < l.size(); i++)
        if (l[i].column() == 0)
            randuri.append(l[i].row());

    qSort(randuri.begin(), randuri.end());

    if (randuri.size() == 0)
    {
        mesajEroare(QString::fromUtf8("Nu ai selectat nimic pentru descărcare."));
        return;
    }

    tabw->setCurrentIndex(2);

    for (int i = 0; i < randuri.size(); i++)
    {
        int r = insereazaRandDesc(rezultate[randuri[i]]);
        Descarcare d;
        d.luat = 0;
        d.locatie = rezultate[randuri[i]][0];
        d.marime = rezultate[randuri[i]][1].toLongLong();
        d.md5 = rezultate[randuri[i]][2];
        d.nume = rezultate[randuri[i]][3];
        d.ipport = rezultate[randuri[i]][4];
        d.rand = tabelDesc->item(r, 1);
        descarcari.append(d);
    }

    statusBar()->showMessage(QString::fromUtf8("Am adăugat %1 fișiere pentru a fi descărcate.").arg(randuri.size()));
}
void FereastraPrincipala::insereazaRand(QStringList& lista)
{
    if (lista.size() != 5)
    {
        fprintf(stderr, "Trebuie 5 elemente!");
        exit(EXIT_FAILURE);
    }

    lista[1] = unitate(lista[1]);

    QTableWidgetItem* elem;
    int rand = tabel->rowCount();
    tabel->insertRow(rand);
    
    for (int i = 0; i < 5; i++)
    {
        elem = new QTableWidgetItem(lista.at(i));
        elem->setFlags(elem->flags() ^ Qt::ItemIsEditable);
        if (i == 0) elem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        else if (i == 1) elem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        else if (i == 2) elem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        else if (i == 3) elem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        else if (i == 4) elem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        tabel->setItem(rand, i, elem);
        tabel->setRowHeight(rand, 20);
    }
}
int FereastraPrincipala::insereazaRandDesc(QStringList& lista)
{
    if (lista.size() != 5)
    {
        fprintf(stderr, "Trebuie 5 elemente!");
        exit(EXIT_FAILURE);
    }

    QStringList lista2;
    lista2 << lista.at(0) << "0,00" << unitate(lista.at(1)) << lista.at(2) << lista.at(3) << lista.at(4);

    QTableWidgetItem* elem;
    int rand = tabelDesc->rowCount();
    tabelDesc->insertRow(rand);
    
    for (int i = 0; i < 6; i++)
    {
        elem = new QTableWidgetItem(lista2.at(i));
        elem->setFlags(elem->flags() ^ Qt::ItemIsEditable);
        if (i == 0) elem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        else if (i == 1) elem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        else if (i == 2) elem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        else if (i == 3) elem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        else if (i == 4) elem->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        else if (i == 5) elem->setTextAlignment(Qt::AlignCenter | Qt::AlignVCenter);
        tabelDesc->setItem(rand, i, elem);
        tabelDesc->setRowHeight(rand, 20);
    }

    return rand;
}
QString md5(QString& locatie)
{
    char ret[33], *r;
    FILE* f;
    QString loc = locatie;
    loc.replace("'", "'\\''");
    loc = QString("md5sum '%1'").arg(loc);

    if (!( f = popen(loc.toUtf8().data(), "r") ))
    {
        fprintf(stderr, "Eroare la invocarea md5sum.\n");
        exit(EXIT_FAILURE);
    }
    r = fgets(ret, 33, f);
    pclose(f);
    
    return QString(ret);
}
QString unitate(QString numar)
{
    double n = numar.toDouble();

    if (n < 1024.0) return numar + " b";
    n /= 1024.0;
    if (n < 1024.0) return QString("%L1 kib").arg(n, 0, 'f', 2);
    n /= 1024.0;
    if (n < 1024.0) return QString("%L1 Mib").arg(n, 0, 'f', 2);
    n /= 1024.0;
    return QString("%L1 Gib").arg(n, 0, 'f', 2);
}
void mesajEroare(QString text)
{
    QMessageBox mbox;
    mbox.setWindowTitle("Eroare");
    mbox.setIcon(QMessageBox::Warning);
    mbox.setText(text);
    mbox.exec();
}
void umpleStructuraServer(struct sockaddr_in* server, char* adresa, int port)
{
    memset(server, 0, sizeof(*server));
    server->sin_family = AF_INET;
    if (adresa == NULL)
        server->sin_addr.s_addr = htonl(INADDR_ANY);
    else
        server->sin_addr.s_addr = inet_addr(adresa);
    server->sin_port = htons(port);
}
void refolosesteAdresa(int sd)
{
    int opt = 1;
    setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
}
void faNonBlocant(int sd)
{
    int arg;

    /* setez socket-ul sa fie non-blocant */
    if ((arg = fcntl(sd, F_GETFL, NULL)) < 0)
        diep("Eroare la citirea flag-urilor. ");
    if (fcntl(sd, F_SETFL, arg | O_NONBLOCK) == -1) 
        diep("Eroare la setarea flag-urilor. ");
}
void trimite_mesaj_fix(int sd, char* mesaj)
{
    int len = strlen(mesaj);
    if (write(sd, mesaj, len + 1) == -1)
        diep("Nu am putut să scriu mesajul. ");
}
int primeste_mesaj(int sd, char* mesaj)
{
    int n, l = 0;
    char c;
    do
    {
        n = read(sd, &c, 1);
        if (n == 0) return -1; // inseamna ca am citit cand nu trebuia sau ca clientul a închis
        if (n == -1) diep("Nu am citit tot ce trebuia: ");
        mesaj[l++] = c;
    } while (c != '\0');
    return 0;
}
