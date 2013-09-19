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
#include <QtXml>

#define die(msg, ...) { fprintf(stderr, msg "\n", ## __VA_ARGS__); exit(EXIT_SUCCESS); }
#define diep(format, ...) { fprintf(stderr, format "%s\n", ## __VA_ARGS__, strerror(errno)); exit(EXIT_FAILURE); }

#define PORT_SERVER 11235
#define CLIENTI_MAX 64

class Fisier
{
    public:
        QString locatie;
        QString md5;
        qint64 marime;
};

class Client
{
    public:
        QString nume;
        QString ip;
        int port;
        int portTrimitere;
        int sd;
        QList<Fisier> fisiere;
};

/* daca adresa==NULL, va fi ADDR_ANY */
void umpleStructuraServer(struct sockaddr_in* server, char* adresa, int port);
void refolosesteAdresa(int sd);
void conecteazaClientul(QList<Client>& clienti, int sd, struct sockaddr_in* saddr);
void trimite_mesaj_fix(int sd, char* mesaj);
int primeste_mesaj(int sd, char* mesaj);
void efectueazaCautare(QList<Client>& clienti, int care, QString md5, QString nume, QString interval);

int main(int argc, char *argv[])
{
    int port = PORT_SERVER, c;
    char utilizare[] =
        "Utilizare: main [OPTIUNI]\n"
        "\n"
        "  -p port              portul pe care se va deschide server-ul\n"
        "  -h                   afiseaza acest mesaj\n";

    /* interpretez parametrii */
    opterr = 0;
    while (( c = getopt(argc, argv, "p:h")) != -1)
    {
        switch (c)
        {
            case 'p': port = atoi(optarg); break;
            case 'h': puts(utilizare); exit(EXIT_SUCCESS); break;
            case '?':
                if (optopt == 'p')
                    {die("Optiunea -%c are nevoie de un argument.", optopt);}
                else if (isprint(optopt)) {die("Optiune necunoscuta '-%c'.", optopt);}
                else die("Optiune necunoscuta.");
            default: abort();
        }
    }

    int sd;
    struct sockaddr_in server;
    struct timeval tv;
    fd_set readfds;

    /* creez socket-ul initial */
    if (( sd = socket(AF_INET, SOCK_STREAM, 0) ) == -1)
        diep("Eroare la crearea socket-ului initial: ");

    umpleStructuraServer(&server, NULL, port);
    refolosesteAdresa(sd);

    /* atasez socket-ul */
    if (bind(sd, (struct sockaddr*)&server, sizeof(struct sockaddr)) == -1)
        diep("Eroare la atasarea socket-ului: ");

    qDebug() << "Ascult la portul " << port;

    /* ascult */
    if (listen(sd, CLIENTI_MAX) == -1)
        diep("Eroare la ascultare: ");

    /* setez multimea sd-urilor sa fie vida */
    FD_ZERO(&readfds);
    FD_SET(sd, &readfds);

    tv.tv_sec = 1;
    tv.tv_usec = 0;

    QList<Client> clienti;

    for (;;)
    {
        int max = sd;
        // TODO să creez corect setul XXX
        // creaza setul
        FD_ZERO(&readfds);
        FD_SET(sd, &readfds);
        for (int i = 0; i < clienti.size(); i++)
        {
            if (clienti[i].sd > max) max = clienti[i].sd;
            FD_SET(clienti[i].sd, &readfds);
        }

        if (select(max + 1, &readfds, NULL, NULL, &tv) == -1)
            diep("Eroare la select: ");

        // s-a conectat alt utilizator
        if (FD_ISSET(sd, &readfds))
        {
            int clientSd;
            struct sockaddr_in de_la;
            socklen_t lungime = sizeof(de_la);

            if (( clientSd = accept(sd, (struct sockaddr*)&de_la, &lungime)) == -1)
                diep("Eroare la acceptare: ");

            qDebug() << "Se conecteaza un client.";
            conecteazaClientul(clienti, clientSd, &de_la);
        }

        for (int i = 0; i < clienti.size(); i++)
            if (FD_ISSET(clienti[i].sd, &readfds))
            {
                qDebug() << "Un client vrea sa zica ceva.";

                char m[4096] = "";
                int n = primeste_mesaj(clienti[i].sd, m);
                if (n == -1)
                {
                    qDebug() << "A plecat clientul nr" << i << "cu numele" << clienti[i].nume;
                    clienti.removeAt(i);
                    break; // pentru că scot un element din listă
                }
                else
                {
                    qDebug() << "Am primit" << m;
                    QDomDocument doc;
                    if (!doc.setContent(QString::fromUtf8(m)))
                        die("Nu a mers setContent().");

                    QDomElement root = doc.documentElement();

                    // vezi dacă este o cerere de căutare (altceva n-are ce să fie oricum)
                    if (root.tagName() == "cauta")
                        efectueazaCautare(clienti, i, root.attribute("md5"), root.attribute("nume"), root.attribute("interval"));
                }
            }
        usleep(50000);
    }

    return 0;
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
void conecteazaClientul(QList<Client>& clienti, int sd, struct sockaddr_in* saddr)
{
    Client c;

    c.ip = QString(inet_ntoa(saddr->sin_addr));
    c.port = ntohs(saddr->sin_port);
    c.sd = sd;

    qDebug() << "Sa conectat clientul cu ip-ul" << c.ip << "si portul" << c.port;

    char mesajul[50000] = "";
    primeste_mesaj(sd, mesajul);

    QDomDocument doc;
    if (!doc.setContent(QString::fromUtf8(mesajul)))
        die("Nu a mers setContent().");

    QDomElement root = doc.documentElement();

    c.nume = root.attribute("nume");
    c.portTrimitere = root.attribute("port").toInt();

    qDebug() << "Nume:" << c.nume << "Port trimitere:" << c.portTrimitere;

    QDomNodeList fisiere = root.childNodes();
    for (unsigned i = 0; i < fisiere.length(); i++)
    {
        Fisier a;
        a.locatie = fisiere.at(i).toElement().attribute("locatie");
        a.marime = fisiere.at(i).toElement().attribute("marime").toLongLong();
        a.md5 = fisiere.at(i).toElement().attribute("md5");
        c.fisiere.append(a);
    }
    qDebug() << "Numar de fisiere:" << c.fisiere.size();

    clienti.append(c);
    qDebug() << "Numar de clienti acum:" << clienti.size();
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
void efectueazaCautare(QList<Client>& clienti, int care, QString md5, QString nume, QString interval)
{
    qDebug() << "Clientul nr." << care << "cu numele: " << clienti[care].nume << "vrea sa caute.";
    qDebug() << "md5" << md5 << "nume" << nume << "interval" << interval;

    qint64 int_min = 0, int_max = LLONG_MAX;
    
    if (interval != "")
    {
        QStringList ls = interval.split("-");
        if (ls[0] != "") int_min = ls[0].toLongLong();
        if (ls[1] != "") int_max = ls[1].toLongLong();
    }

    if (nume == "") nume = ".*";

    qDebug() << "int_min" << int_min << "int_max" << int_max;

    QDomDocument doc;
    QDomElement root = doc.createElement("rezultate");
    doc.appendChild(root);

    if (md5 != "")
    {
        qDebug() << "Caut după MD5-ul:" << md5;
        for (int i = 0; i < clienti.size(); i++)
        {
            int rez = 0;
            QDomElement cl = doc.createElement("client");
            cl.setAttribute("nume", clienti[i].nume);
            cl.setAttribute("ipport", QString("%1:%2").arg(clienti[i].ip).arg(clienti[i].portTrimitere));

            for (int j = 0; j < clienti[i].fisiere.size(); j++)
                if (clienti[i].fisiere[j].md5 == md5)
                {
                    QDomElement f = doc.createElement("fisier");
                    f.setAttribute("locatie", clienti[i].fisiere[j].locatie);
                    f.setAttribute("marime", clienti[i].fisiere[j].marime);
                    f.setAttribute("md5", clienti[i].fisiere[j].md5);
                    cl.appendChild(f);
                    rez++;
                }

            if (rez > 0) root.appendChild(cl);
        }
    }
    else
    {
        qDebug() << "Caut după nume și mărime";
        QRegExp expr(nume);
        for (int i = 0; i < clienti.size(); i++)
        {
            int rez = 0;
            QDomElement cl = doc.createElement("client");
            cl.setAttribute("nume", clienti[i].nume);
            cl.setAttribute("ipport", QString("%1:%2").arg(clienti[i].ip).arg(clienti[i].portTrimitere));

            for (int j = 0; j < clienti[i].fisiere.size(); j++)
                if (expr.exactMatch(clienti[i].fisiere[j].locatie) &&
                        clienti[i].fisiere[j].marime >= int_min && clienti[i].fisiere[j].marime <= int_max)
                {
                    QDomElement f = doc.createElement("fisier");
                    f.setAttribute("locatie", clienti[i].fisiere[j].locatie);
                    f.setAttribute("marime", clienti[i].fisiere[j].marime);
                    f.setAttribute("md5", clienti[i].fisiere[j].md5);
                    cl.appendChild(f);
                    rez++;
                }

            if (rez > 0) root.appendChild(cl);
        }
    }
    char *m = doc.toString().toUtf8().data();
    trimite_mesaj_fix(clienti[care].sd, m);
}
