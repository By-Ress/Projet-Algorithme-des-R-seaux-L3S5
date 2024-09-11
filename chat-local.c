#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <poll.h>
#include <netdb.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>



#define CHECK(op) do { if ((op) == -1) { perror(#op); exit(EXIT_FAILURE); } } while (0)

#define PORT(p) htons(p)

#define SIZE 100
#define TAILLE_BUFF 2048

typedef int bool;
#define TRUE 1
#define FALSE 0

char *HELO = "/HELO";
char *QUIT = "/QUIT";
char *FD = "/FILE";
char *retour = "\n";
char *recu = "fichier telecharger !\n";

struct Message
{
#ifdef BIN
    uint8_t cmd;
#endif
#ifdef FILEIO
    char nomFichier[TAILLE_BUFF];
    bool fichierEnvoyer;
#endif
#ifdef USR
    char NomUtilisateur[TAILLE_BUFF];
#endif
    char contenue[TAILLE_BUFF];
    uint16_t tailleContenue;
};

int main(int argc, char *argv[])
{

    // Test arg number
    if (argc != 2)
    {
        fprintf(stderr, "usage: ./client-chat port\n");
        exit(EXIT_FAILURE);
    }

    // Convert and check port number
    int numPort = atoi(argv[1]);
    if ((numPort < 10000) || (numPort > 65000))
    {
        fprintf(stderr, "%s: Le numéro de port doit être entre 10000 et 65000\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Create socket (use SOCK_DGRAM for UDP)
    int socketIP = socket(AF_INET6, SOCK_DGRAM, 0);
    CHECK(socketIP);

    // Set dual stack socket
    int value = 0;
    CHECK(setsockopt(socketIP, IPPROTO_IPV6, IPV6_V6ONLY, &value, sizeof value));

    // Set local addr
    struct sockaddr_storage ss;
    memset(&ss, 0, sizeof(ss));
    struct sockaddr *s = (struct sockaddr *)&ss;
    struct sockaddr_in6 *in6 = (struct sockaddr_in6 *)&ss;
    in6->sin6_family = AF_INET6;
    in6->sin6_port = PORT(numPort);
    in6->sin6_addr = in6addr_any;

    // Check if a client is already present
    int connection = bind(socketIP, s, sizeof(*in6));
    struct Message message;

#ifdef USR
    printf("Saissiser votre nom: ");
    char nom[TAILLE_BUFF];
    fgets(nom, sizeof(nom), stdin);
    nom[strcspn(nom, "\n")] = '\0';
    memcpy(message.NomUtilisateur,nom,strlen(nom));
#endif

    if (connection == -1 && errno == EADDRINUSE)
    {
#if defined(BIN)
        message.cmd = 1;
#else
        message.tailleContenue = strlen(HELO);
        memcpy(message.contenue, HELO, message.tailleContenue);
#endif
        ssize_t envoieHELO = sendto(socketIP, &message, sizeof(message), 0, s, sizeof(*in6));
        CHECK(envoieHELO);
    }

    // Prepare struct pollfd with stdin and socket for incoming data
    struct pollfd pollIP[2];
    pollIP[0].fd = STDIN_FILENO;
    pollIP[0].events = POLLIN;
    pollIP[1].fd = socketIP;
    pollIP[1].events = POLLIN;

    printf("Le chat a commencé. Pour quitter, écrivez /QUIT\n");
    char buffer[TAILLE_BUFF];

    // Main loop
    while (1)
    {
        int ret = poll(pollIP, 2, -1);
        CHECK(ret);

        if (pollIP[0].revents & POLLIN)
        {
            memset(buffer, 0, TAILLE_BUFF);
            ssize_t bytesRead = read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
            if (strcmp(retour,buffer)!=0)
            {
                if (bytesRead > 0)
                {
                    buffer[bytesRead] = '\0';

#ifdef BIN
                    if (strncmp(buffer, HELO, strlen(HELO)) == 0)
                        message.cmd = 1;
                    else if (strncmp(buffer, QUIT, strlen(QUIT)) == 0)
                        message.cmd = 2;
                    else
                    {
                        message.cmd = 0;
                        message.tailleContenue = bytesRead;
                        memcpy(message.contenue, buffer, message.tailleContenue);
                    }
#endif
#ifdef FILEIO
                    if (strncmp(buffer, FD, strlen(FD)) == 0)
                    {
                        int i = strcspn(buffer, " ");
                        char * fichier = buffer + i + 1 ;
                        fichier[strcspn(fichier, "\n")] = '\0';
                        int fd = open(fichier, O_RDONLY);
                        CHECK(fd);
                        ssize_t lecture;
                        while ((lecture = read(fd, message.contenue, TAILLE_BUFF)) > 0)
                        {
                            CHECK(lecture);
                        }
                        CHECK(lecture);
                        CHECK(close(fd));
                        message.tailleContenue = lecture;
                        message.fichierEnvoyer=TRUE;
                        memcpy(message.nomFichier,fichier,strlen(fichier));
                    }
                    else
                    {
                        message.tailleContenue = bytesRead;
                        memset(message.contenue,0,TAILLE_BUFF);
                        memcpy(message.contenue, buffer, message.tailleContenue);
                    }
#else
                    message.tailleContenue = bytesRead;
                    memset(message.contenue,0,TAILLE_BUFF);
                    memcpy(message.contenue, buffer, strlen(buffer));
#endif
                    ssize_t envoie = sendto(socketIP, &message, sizeof(message), 0, s, sizeof(*in6));
                    CHECK(envoie);

#ifdef FILEIO
                    memset(message.contenue,0,TAILLE_BUFF);
                    message.fichierEnvoyer = FALSE;
#endif

                    if (strncmp(buffer, "/QUIT", 5) == 0)
                    {
                        printf("Vous avez quitté le chat.\n");
                        break;
                    }
                }
            }
        }

        if (pollIP[1].revents & POLLIN)
        {
            memset(buffer, 0, TAILLE_BUFF);
            struct Message message_recu;
            socklen_t tailleAddrEnvoie = sizeof(*in6);
            ssize_t bytesRead = recvfrom(socketIP, &message_recu, sizeof(message_recu), 0, s, &tailleAddrEnvoie);

#ifdef BIN
            if (message_recu.cmd == 1)
                memcpy(buffer, HELO, strlen(HELO));
            if (message_recu.cmd == 2)
                memcpy(buffer, QUIT, strlen(QUIT));
            if (message_recu.cmd == 0)
            {
                memcpy(buffer, message_recu.contenue, message_recu.tailleContenue);
            }
#endif
#ifdef FILEIO
            if (message_recu.fichierEnvoyer == TRUE)
            {
                char nomfichier[TAILLE_BUFF+8];
                snprintf(nomfichier, sizeof(nomfichier), "copy-%s", message_recu.nomFichier);
                int fd = open(nomfichier, O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR);
                CHECK(fd);
                ssize_t ecriture = write(fd, message_recu.contenue, strlen(message_recu.contenue));
                CHECK(ecriture);
                CHECK(close(fd));
                message_recu.fichierEnvoyer = FALSE;
                memcpy(message_recu.contenue,recu,strlen(recu));
                memcpy(buffer,recu,strlen(recu));

            }
            else
            {
                memcpy(buffer, message_recu.contenue, message_recu.tailleContenue);
            }
#else
            message_recu.tailleContenue = bytesRead;
            memcpy(buffer, message_recu.contenue, strlen(message_recu.contenue));
#endif
            CHECK(bytesRead);
            if (bytesRead > 0)
            {

                if (strncmp(buffer, "/QUIT", 5) == 0)
                {
                    char NomUtilisateur[TAILLE_BUFF];
#ifdef USR
                    memcpy(NomUtilisateur,message_recu.NomUtilisateur, strlen(message_recu.NomUtilisateur));
#else
                    memcpy(NomUtilisateur,"L'autre utilisateur",sizeof("L'autre utilisateur"));
#endif
                    printf("%s a quitté le chat.\n",NomUtilisateur);
#ifndef USR
                    break;
#endif
                }
                else if (strncmp(buffer, "/HELO", 5) == 0)
                {
                    char portUTILISATEUR[NI_MAXSERV];
                    char ipUTILISATEUR[NI_MAXHOST];

                    int resultatDeGetNameInfo = getnameinfo(s, tailleAddrEnvoie, ipUTILISATEUR, NI_MAXHOST,
                                                            portUTILISATEUR, NI_MAXSERV, NI_NUMERICHOST);
                    if (resultatDeGetNameInfo != 0)
                        printf("erreur getnameinfo\n");

                    printf("%s %s\n", ipUTILISATEUR, portUTILISATEUR);
                }
                else
                {

#ifdef USR
                    char NomUtilisateur[TAILLE_BUFF];
                    memcpy(NomUtilisateur,message_recu.NomUtilisateur, strlen(message_recu.NomUtilisateur));
                    printf("%s: %s",NomUtilisateur, buffer);
#else
                    printf("Reçu: %s", buffer);
#endif

                }
            }
        }
    }

    // Close socket
    close(socketIP);

    // Free memory

    return 0;
}
