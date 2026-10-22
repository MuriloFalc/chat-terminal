#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 2048
#define NICK_LEN 32

static int sockfd = 0;
static volatile sig_atomic_t running = 1;

static void sig_handler(int sig) {
    (void)sig;
    running = 0;
}

static void str_trim(char *s) {
    s[strcspn(s, "\n")] = '\0';
    s[strcspn(s, "\r")] = '\0';
}

static void *send_handler(void *arg) {
    (void)arg;
    char buff[BUFFER_SIZE];

    while (running) {
        if (fgets(buff, sizeof(buff), stdin) == NULL) break;
        str_trim(buff);

        if (strlen(buff) == 0) continue;

        if (strcmp(buff, "/sair") == 0) {
            write(sockfd, buff, strlen(buff));
            running = 0;
            break;
        }

        if (write(sockfd, buff, strlen(buff)) < 0) {
            perror("write");
            break;
        }
    }
    return NULL;
}

static void *recv_handler(void *arg) {
    (void)arg;
    char buff[BUFFER_SIZE];

    while (running) {
        int n = recv(sockfd, buff, sizeof(buff) - 1, 0);
        if (n <= 0) {
            if (n == 0) {
                printf("\nDesconectado do servidor.\n");
            } else {
                perror("recv");
            }
            running = 0;
            break;
        }
        buff[n] = '\0';
        printf("%s", buff);
        fflush(stdout);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Uso: %s <ip> <porta> <apelido>\n", argv[0]);
        return 1;
    }

    const char *server_ip = argv[1];
    int port = atoi(argv[2]);
    const char *nick = argv[3];

    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Porta invalida.\n");
        return 1;
    }

    if (strlen(nick) == 0 || strlen(nick) > 30) {
        fprintf(stderr, "Apelido invalido (1-30 caracteres).\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, server_ip, &serv_addr.sin_addr) <= 0) {
        fprintf(stderr, "Endereco invalido: %s\n", server_ip);
        close(sockfd);
        return 1;
    }

    printf("Conectando a %s:%d...\n", server_ip, port);

    if (connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect");
        close(sockfd);
        return 1;
    }

    write(sockfd, nick, strlen(nick));

    printf("=== Conectado! Bem-vindo ao chat, %s ===\n", nick);
    printf("Comandos: /sair (sair), /lista (usuarios online)\n\n");

    pthread_t send_tid, recv_tid;
    pthread_create(&send_tid, NULL, send_handler, NULL);
    pthread_create(&recv_tid, NULL, recv_handler, NULL);

    while (running) {
        usleep(100000);
    }

    close(sockfd);
    printf("Desconectado.\n");
    return 0;
}
