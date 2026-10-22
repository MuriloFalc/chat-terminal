#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <pthread.h>
#include <arpa/inet.h>

#define MAX_CLIENTS 100
#define BUFFER_SIZE 2048
#define NICK_LEN 32

typedef struct {
    struct sockaddr_in addr;
    int sockfd;
    int uid;
    char nick[NICK_LEN];
} client_t;

static client_t *clients[MAX_CLIENTS];
static pthread_mutex_t clients_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile sig_atomic_t server_running = 1;

static void sig_handler(int sig) {
    (void)sig;
    server_running = 0;
}

static void add_client(client_t *cl) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (!clients[i]) {
            clients[i] = cl;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static void remove_client(int uid) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->uid == uid) {
            clients[i] = NULL;
            break;
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static void broadcast(const char *msg, int sender_uid) {
    pthread_mutex_lock(&clients_mutex);
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (clients[i] && clients[i]->uid != sender_uid) {
            if (write(clients[i]->sockfd, msg, strlen(msg)) < 0) {
                perror("write");
                break;
            }
        }
    }
    pthread_mutex_unlock(&clients_mutex);
}

static int uid_counter = 0;

static void *handle_client(void *arg) {
    char buff[BUFFER_SIZE];
    char msg[BUFFER_SIZE + NICK_LEN + 4];
    int leave = 0;

    client_t *cli = (client_t *)arg;

    recv(cli->sockfd, cli->nick, NICK_LEN, 0);
    cli->nick[strcspn(cli->nick, "\n")] = '\0';
    cli->nick[strcspn(cli->nick, "\r")] = '\0';

    if (strlen(cli->nick) == 0) {
        snprintf(cli->nick, NICK_LEN, "anon_%d", cli->uid);
    }

    snprintf(msg, sizeof(msg), ">> %s entrou no chat.\n", cli->nick);
    printf("%s", msg);
    broadcast(msg, cli->uid);

    while (!leave) {
        int rcv = recv(cli->sockfd, buff, sizeof(buff) - 1, 0);
        if (rcv <= 0) {
            break;
        }
        buff[rcv] = '\0';
        buff[strcspn(buff, "\n")] = '\0';
        buff[strcspn(buff, "\r")] = '\0';

        if (strlen(buff) == 0) continue;

        if (strcmp(buff, "/sair") == 0) {
            leave = 1;
        } else if (strcmp(buff, "/lista") == 0) {
            snprintf(msg, sizeof(msg), "--- Usuarios online ---\n");
            write(cli->sockfd, msg, strlen(msg));
            pthread_mutex_lock(&clients_mutex);
            for (int i = 0; i < MAX_CLIENTS; ++i) {
                if (clients[i]) {
                    snprintf(msg, sizeof(msg), "  * %s\n", clients[i]->nick);
                    write(cli->sockfd, msg, strlen(msg));
                }
            }
            pthread_mutex_unlock(&clients_mutex);
            snprintf(msg, sizeof(msg), "------------------------\n");
            write(cli->sockfd, msg, strlen(msg));
        } else {
            snprintf(msg, sizeof(msg), "[%s]: %s\n", cli->nick, buff);
            printf("%s", msg);
            broadcast(msg, cli->uid);
        }
    }

    snprintf(msg, sizeof(msg), "<< %s saiu do chat.\n", cli->nick);
    printf("%s", msg);
    broadcast(msg, cli->uid);

    close(cli->sockfd);
    remove_client(cli->uid);
    free(cli);
    pthread_detach(pthread_self());
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Uso: %s <porta>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Porta invalida.\n");
        return 1;
    }

    signal(SIGINT, sig_handler);
    signal(SIGPIPE, SIG_IGN);

    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port);

    if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind");
        close(listenfd);
        return 1;
    }

    if (listen(listenfd, 10) < 0) {
        perror("listen");
        close(listenfd);
        return 1;
    }

    printf("=== Chat Server iniciado na porta %d ===\n", port);
    printf("Aguardando conexoes...\n");

    while (server_running) {
        struct sockaddr_in cli_addr;
        socklen_t cli_len = sizeof(cli_addr);

        int connfd = accept(listenfd, (struct sockaddr *)&cli_addr, &cli_len);
        if (connfd < 0) {
            if (!server_running) break;
            perror("accept");
            continue;
        }

        client_t *cli = (client_t *)malloc(sizeof(client_t));
        cli->addr = cli_addr;
        cli->sockfd = connfd;
        cli->uid = uid_counter++;

        add_client(cli);

        pthread_t tid;
        if (pthread_create(&tid, NULL, handle_client, (void *)cli) != 0) {
            perror("pthread_create");
            remove_client(cli->uid);
            close(connfd);
            free(cli);
        }
    }

    printf("\nServidor encerrado.\n");
    close(listenfd);
    return 0;
}
