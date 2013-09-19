#ifndef FEREASTRA_PRINCIPALA_H
#define FEREASTRA_PRINCIPALA_H

#include <QtGui>

#define die(msg, ...) { fprintf(stderr, msg "\n", ## __VA_ARGS__); exit(EXIT_SUCCESS); }
#define diep(format, ...) { fprintf(stderr, format "%s\n", ## __VA_ARGS__, strerror(errno)); exit(EXIT_FAILURE); }

class Fisier
{
    public:
        QString locatie;
        QString md5;
        qint64 marime;
};
class Descarcare
{
    public:
        qint64 luat;
        QString locatie;
        qint64 marime;
        QString md5;
        QString nume;
        QString ipport;
        QTableWidgetItem* rand;
};
// Așa țin minte ce trebuie să trimit și cui
class Trimitere
{
    public:
        int sd;
        QFile* f;
};
class FereastraPrincipala; //hehe
class ThreadDescarcare : public QThread
{
    public:
        virtual void run();
        FereastraPrincipala* fp;
};
class ThreadIncarcare : public QThread
{
    public:
        virtual void run();
        void somnUsor(int ms);
        FereastraPrincipala* fp;
};

class FereastraPrincipala : public QMainWindow
{
    Q_OBJECT
    public:
        FereastraPrincipala(QWidget* parent = 0);
        ~FereastraPrincipala();
        void descarcaCeva();
        void buclaIncarcare(ThreadIncarcare& inc);
    private slots:
        void alegeDosarDeImpartit();
        void alegeDosarDeDescarcare();
        void apasatConectare();
        void apasatDeconectare();
        void apasatCauta();
        void apasatDescarca();
    private:
        ThreadDescarcare td;
        ThreadIncarcare ti;
        bool existaSetari;
        QWidget* wValori;
        QTabWidget* tabw;
        QTableWidget* tabel;
        QTableWidget* tabelDesc;
        QPushButton* pbConectare;
        QPushButton* pbDeconectare;
        QLineEdit* leNume;
        QLineEdit* leImpartit;
        QLineEdit* leDescarcare;
        QLineEdit* leServer;
        QLineEdit* leCauta;
        QLineEdit* leInterval;
        QLineEdit* leMD5;

        QList<Fisier> fisiereGasite;
        QList<Fisier> fisiereGasiteSetari;
        QList<QStringList> rezultate;
        int portTrimitere;
        int socketServ;
        QList<Descarcare> descarcari;
        int descarcareCurenta;

        QWidget* creazaTabConectare();
        QWidget* creazaTabCautare();
        QWidget* creazaTabDescarcari();
        void gasesteFisiere(QDir& dir);
        void calculeazaMD5();
        void iaDateDinSetari();
        void scrieFisierulSetari();
        QString reprezentareaXml();
        void amPrimitRezultatele(QString xml);
        void insereazaRand(QStringList& lista);
        // întoarce rândul pentru a fi folosit la actualizarea procentului
        int insereazaRandDesc(QStringList& lista);
};

QString md5(QString& locatie);
QString unitate(QString numar);
void mesajEroare(QString text);
void umpleStructuraServer(struct sockaddr_in* server, char* adresa, int port);
void refolosesteAdresa(int sd);
void faNonBlocant(int sd);
void trimite_mesaj_fix(int sd, char* mesaj);
int primeste_mesaj(int sd, char* mesaj);

#endif
