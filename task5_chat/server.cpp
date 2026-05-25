// Чат-сервер на C++ (Linux)
// Задание 5: многопоточный клиент-серверный чат
//
// Отличия от Windows-версии:
//   WinSock2            -> POSIX сокеты (sys/socket.h)
//   WSAEventSelect      -> epoll  (нет лимита в 64 события)
//   CRITICAL_SECTION    -> pthread_mutex_t
//   CreateThread        -> pthread_create
//   WSAWaitForMultiple  -> epoll_wait

#include <sys/socket.h>
#include <sys/epoll.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <string>
#include <queue>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <ctime>

#define PORT              8888
#define MAX_CLIENTS       10000
#define MAX_NAME_LEN      31
#define MAX_MSG_LEN       255
#define MAX_PACKET_SIZE   2048
#define MAX_EPOLL_EVENTS  256

#define MSG_LOGIN   1
#define MSG_PUBLIC  2
#define MSG_PRIVATE 3
#define MSG_LOGOUT  4
#define MSG_SYSTEM  5

// ─── Структуры ───────────────────────────────────────────────────────────────

struct Client {
    int    sock;
    char   name[MAX_NAME_LEN + 1];
    int    active;
    int    authenticated;
    time_t connectTime;
    time_t lastMsgTime;
    int    msgCountThisSecond;
};

struct Message {
    int  sock;
    int  type;
    char from[MAX_NAME_LEN + 1];
    char to  [MAX_NAME_LEN + 1];
    char text[MAX_MSG_LEN * 2 + 1];
};

struct SendRequest {
    int  sock;
    char data[MAX_PACKET_SIZE];
    int  len;
};

// ─── Глобальные переменные ────────────────────────────────────────────────────

pthread_mutex_t g_cs;             // защита g_clients[]
pthread_mutex_t g_sendQueueCs;    // защита g_sendQueue
pthread_mutex_t g_messageQueueCs; // защита g_messageQueue

std::queue<Message>     g_messageQueue;
std::queue<SendRequest> g_sendQueue;

int     g_serverSock  = -1;
int     g_epoll_fd    = -1;
Client* g_clients[MAX_CLIENTS];
int     g_clientCount = 0;
volatile int g_running = 1;

// Буферы для сборки неполных пакетов (по одному на каждого клиента)
char g_partialBuffers[MAX_CLIENTS][MAX_PACKET_SIZE];
int  g_partialSizes[MAX_CLIENTS];

// ─── Вспомогательные функции ──────────────────────────────────────────────────

void sigint_handler(int) { g_running = 0; }

int isValidNickChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           (c >= '0' && c <= '9') || c == '_';
}

int validateName(const char* name) {
    int len = (int)strlen(name);
    if (len == 0 || len > MAX_NAME_LEN) return 0;
    for (int i = 0; i < len; i++)
        if (!isValidNickChar(name[i])) return 0;
    return 1;
}

// Поиск клиента по нику (без учёта регистра)
int findByName(const char* name) {
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] && g_clients[i]->active && g_clients[i]->authenticated &&
            strcasecmp(g_clients[i]->name, name) == 0)
            return i;
    return -1;
}

void setNonBlocking(int sock) {
    int flags = fcntl(sock, F_GETFL, 0);
    fcntl(sock, F_SETFL, flags | O_NONBLOCK);
}

void setRecvTimeout(int sock, int seconds) {
    struct timeval tv = {seconds, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

// ─── Управление клиентами ────────────────────────────────────────────────────

int addClient(int sock, const char* name) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (!g_clients[i]) {
            g_clients[i] = (Client*)calloc(1, sizeof(Client));
            g_clients[i]->sock          = sock;
            g_clients[i]->active        = 1;
            g_clients[i]->authenticated = 1;
            g_clients[i]->connectTime   = time(NULL);
            g_clients[i]->lastMsgTime   = time(NULL);
            strncpy(g_clients[i]->name, name, MAX_NAME_LEN);

            // Регистрируем сокет в epoll, сохраняем индекс клиента в data.u32
            struct epoll_event ev;
            ev.events   = EPOLLIN | EPOLLRDHUP;
            ev.data.u32 = (uint32_t)i;
            epoll_ctl(g_epoll_fd, EPOLL_CTL_ADD, sock, &ev);

            g_clientCount++;
            return i;
        }
    }
    return -1;
}

void removeClient(int idx) {
    if (idx < 0 || idx >= MAX_CLIENTS || !g_clients[idx]) return;
    epoll_ctl(g_epoll_fd, EPOLL_CTL_DEL, g_clients[idx]->sock, NULL);
    close(g_clients[idx]->sock);
    free(g_clients[idx]);
    g_clients[idx] = NULL;
    g_partialSizes[idx] = 0;
    g_clientCount--;
}

// ─── Упаковка/отправка пакетов ───────────────────────────────────────────────

void packPacket(char* buf, int* outLen, int type,
                const char* from, const char* to, const char* text) {
    int pos = 0, len;

    memcpy(buf + pos, &type, sizeof(int)); pos += sizeof(int);

    len = (int)strlen(from) + 1;
    memcpy(buf + pos, &len, sizeof(int)); pos += sizeof(int);
    memcpy(buf + pos, from, len);         pos += len;

    len = (int)strlen(to) + 1;
    memcpy(buf + pos, &len, sizeof(int)); pos += sizeof(int);
    memcpy(buf + pos, to, len);           pos += len;

    len = (int)strlen(text) + 1;
    memcpy(buf + pos, &len, sizeof(int)); pos += sizeof(int);
    memcpy(buf + pos, text, len);         pos += len;

    *outLen = pos;
}

// Отправляет [4 байта длины][данные], возвращает 0 при ошибке
int trySendFull(int sock, const char* data, int dataLen) {
    // MSG_NOSIGNAL — не падать с SIGPIPE если клиент отключился
    if (send(sock, &dataLen, sizeof(int), MSG_NOSIGNAL) != sizeof(int)) return 0;
    if (send(sock, data, dataLen, MSG_NOSIGNAL) != dataLen)              return 0;
    return 1;
}

// Добавить пакет в очередь исходящих
void queueSend(int sock, int type,
               const char* from, const char* to, const char* text) {
    SendRequest req;
    req.sock = sock;
    packPacket(req.data, &req.len, type, from, to, text);

    pthread_mutex_lock(&g_sendQueueCs);
    g_sendQueue.push(req);
    pthread_mutex_unlock(&g_sendQueueCs);
}

// Разослать сообщение всем, кроме except_sock
void sendToAll(int type, const char* from, const char* text, int except_sock) {
    pthread_mutex_lock(&g_cs);
    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i] && g_clients[i]->active && g_clients[i]->authenticated &&
            g_clients[i]->sock != except_sock)
            queueSend(g_clients[i]->sock, type, from, "", text);
    pthread_mutex_unlock(&g_cs);
}

// ─── Rate limiting (не более 10 сообщений в секунду) ────────────────────────

int checkRateLimit(Client* client) {
    time_t now = time(NULL);
    if (now != client->lastMsgTime) {
        client->lastMsgTime = now;
        client->msgCountThisSecond = 1;
        return 1;
    }
    client->msgCountThisSecond++;
    return (client->msgCountThisSecond <= 10);
}

// ─── Сборка пакетов из потока байт ──────────────────────────────────────────

int processReadData(int idx, char* data, int dataLen) {
    if (g_partialSizes[idx] + dataLen > MAX_PACKET_SIZE) return -1;

    memcpy(g_partialBuffers[idx] + g_partialSizes[idx], data, dataLen);
    g_partialSizes[idx] += dataLen;

    if (g_partialSizes[idx] < 4) return 0; // ещё нет даже длины

    int packetLen;
    memcpy(&packetLen, g_partialBuffers[idx], sizeof(int));
    if (packetLen <= 0 || packetLen > MAX_PACKET_SIZE) return -1;

    if (g_partialSizes[idx] < 4 + packetLen) return 0; // пакет ещё не весь

    // Пакет собран — распаковываем
    int pos = 4, len;
    int type;
    char from[MAX_NAME_LEN + 1], to[MAX_NAME_LEN + 1], text[MAX_MSG_LEN * 2 + 1];

    memcpy(&type, g_partialBuffers[idx] + pos, sizeof(int)); pos += sizeof(int);
    memcpy(&len,  g_partialBuffers[idx] + pos, sizeof(int)); pos += sizeof(int);
    memcpy(from,  g_partialBuffers[idx] + pos, len);          pos += len;
    memcpy(&len,  g_partialBuffers[idx] + pos, sizeof(int)); pos += sizeof(int);
    memcpy(to,    g_partialBuffers[idx] + pos, len);          pos += len;
    memcpy(&len,  g_partialBuffers[idx] + pos, sizeof(int)); pos += sizeof(int);
    memcpy(text,  g_partialBuffers[idx] + pos, len);

    Client* client = g_clients[idx];

    if (!checkRateLimit(client)) {
        queueSend(client->sock, MSG_SYSTEM, "SYSTEM", "",
                  "Слишком много сообщений! Подождите.");
    } else {
        Message msg;
        msg.sock = client->sock;
        msg.type = type;
        strncpy(msg.from, from, MAX_NAME_LEN);
        strncpy(msg.to,   to,   MAX_NAME_LEN);
        strncpy(msg.text, text, MAX_MSG_LEN * 2);

        pthread_mutex_lock(&g_messageQueueCs);
        g_messageQueue.push(msg);
        pthread_mutex_unlock(&g_messageQueueCs);
    }

    // Сдвигаем остаток (начало следующего пакета) в начало буфера
    int remaining = g_partialSizes[idx] - (4 + packetLen);
    if (remaining > 0)
        memmove(g_partialBuffers[idx],
                g_partialBuffers[idx] + 4 + packetLen, remaining);
    g_partialSizes[idx] = remaining;

    return 1;
}

// ─── networkThread: чтение от клиентов + отправка из очереди ─────────────────
//
// Вместо WSAWaitForMultipleEvents (лимит 64) используем epoll — нет лимита.

void* networkThread(void*) {
    struct epoll_event events[MAX_EPOLL_EVENTS];

    while (g_running) {
        // 1. Опустошаем очередь исходящих пакетов
        while (true) {
            pthread_mutex_lock(&g_sendQueueCs);
            if (g_sendQueue.empty()) { pthread_mutex_unlock(&g_sendQueueCs); break; }
            SendRequest req = g_sendQueue.front();
            g_sendQueue.pop();
            pthread_mutex_unlock(&g_sendQueueCs);

            if (!trySendFull(req.sock, req.data, req.len)) {
                // Не удалось отправить — клиент отключился
                pthread_mutex_lock(&g_cs);
                for (int i = 0; i < MAX_CLIENTS; i++) {
                    if (g_clients[i] && g_clients[i]->sock == req.sock) {
                        removeClient(i);
                        break;
                    }
                }
                pthread_mutex_unlock(&g_cs);
            }
        }

        // 2. Ждём события от клиентов (таймаут 50 мс)
        int nfds = epoll_wait(g_epoll_fd, events, MAX_EPOLL_EVENTS, 50);
        if (nfds <= 0) continue;

        for (int i = 0; i < nfds; i++) {
            int clientIdx = (int)events[i].data.u32;

            pthread_mutex_lock(&g_cs);
            if (clientIdx < 0 || clientIdx >= MAX_CLIENTS || !g_clients[clientIdx]) {
                pthread_mutex_unlock(&g_cs);
                continue;
            }

            Client* client = g_clients[clientIdx];
            int sock = client->sock;

            // Клиент закрыл соединение
            if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                Message msg;
                msg.sock = sock;
                msg.type = MSG_LOGOUT;
                strncpy(msg.from, client->name, MAX_NAME_LEN);
                snprintf(msg.text, sizeof(msg.text), "%s покинул чат", client->name);

                removeClient(clientIdx);
                pthread_mutex_unlock(&g_cs);

                pthread_mutex_lock(&g_messageQueueCs);
                g_messageQueue.push(msg);
                pthread_mutex_unlock(&g_messageQueueCs);
                continue;
            }

            // Данные готовы к чтению
            if (events[i].events & EPOLLIN) {
                char buffer[8192];
                int received = recv(sock, buffer, sizeof(buffer), 0);

                if (received > 0) {
                    if (processReadData(clientIdx, buffer, received) == -1)
                        removeClient(clientIdx);
                    pthread_mutex_unlock(&g_cs);
                } else if (received == 0) {
                    // Клиент закрыл соединение (graceful)
                    Message msg;
                    msg.sock = sock;
                    msg.type = MSG_LOGOUT;
                    strncpy(msg.from, client->name, MAX_NAME_LEN);
                    snprintf(msg.text, sizeof(msg.text), "%s покинул чат", client->name);

                    removeClient(clientIdx);
                    pthread_mutex_unlock(&g_cs);

                    pthread_mutex_lock(&g_messageQueueCs);
                    g_messageQueue.push(msg);
                    pthread_mutex_unlock(&g_messageQueueCs);
                } else {
                    pthread_mutex_unlock(&g_cs);
                }
            } else {
                pthread_mutex_unlock(&g_cs);
            }
        }
    }
    return NULL;
}

// ─── Обработка нового подключения ────────────────────────────────────────────

static void handleNewConnection(int clientSock) {
    setRecvTimeout(clientSock, 5);

    char name[MAX_NAME_LEN + 1];
    int packetLen;
    char buffer[MAX_PACKET_SIZE];

    // Читаем длину пакета
    if (recv(clientSock, &packetLen, sizeof(int), MSG_WAITALL) != sizeof(int)) {
        close(clientSock); return;
    }
    // Читаем сам пакет
    if (recv(clientSock, buffer, packetLen, MSG_WAITALL) != packetLen) {
        close(clientSock); return;
    }

    int pos = 0, type, len;
    memcpy(&type, buffer + pos, sizeof(int)); pos += sizeof(int);
    memcpy(&len,  buffer + pos, sizeof(int)); pos += sizeof(int);
    memcpy(name,  buffer + pos, len);

    if (type != MSG_LOGIN || !validateName(name)) {
        close(clientSock); return;
    }

    pthread_mutex_lock(&g_cs);
    int nameExists = (findByName(name) != -1);
    pthread_mutex_unlock(&g_cs);

    if (nameExists) {
        const char* err = "Ник уже занят";
        send(clientSock, err, strlen(err) + 1, MSG_NOSIGNAL);
        close(clientSock); return;
    }

    setNonBlocking(clientSock);

    pthread_mutex_lock(&g_cs);
    int idx = addClient(clientSock, name);
    pthread_mutex_unlock(&g_cs);

    if (idx == -1) {
        const char* err = "Ошибка подключения";
        send(clientSock, err, strlen(err) + 1, MSG_NOSIGNAL);
        close(clientSock); return;
    }

    // Уведомляем всех о новом участнике
    Message joinMsg;
    joinMsg.sock = clientSock;
    joinMsg.type = MSG_SYSTEM;
    strncpy(joinMsg.from, "SYSTEM", MAX_NAME_LEN);
    snprintf(joinMsg.text, sizeof(joinMsg.text), "%s подключился к чату", name);

    pthread_mutex_lock(&g_messageQueueCs);
    g_messageQueue.push(joinMsg);
    pthread_mutex_unlock(&g_messageQueueCs);

    // Отправляем новому клиенту список участников
    char list[4096] = "Участники чата: ";
    pthread_mutex_lock(&g_cs);
    int first = 1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (g_clients[i] && g_clients[i]->active && g_clients[i]->authenticated &&
            g_clients[i]->sock != clientSock) {
            if (!first) strcat(list, ", ");
            strcat(list, g_clients[i]->name);
            first = 0;
        }
    }
    pthread_mutex_unlock(&g_cs);

    Message listMsg;
    listMsg.sock = clientSock;
    listMsg.type = MSG_SYSTEM;
    strncpy(listMsg.from, "SYSTEM", MAX_NAME_LEN);
    strncpy(listMsg.text, list, sizeof(listMsg.text) - 1);

    pthread_mutex_lock(&g_messageQueueCs);
    g_messageQueue.push(listMsg);
    pthread_mutex_unlock(&g_messageQueueCs);

    std::cout << "[+] " << name << " подключился (всего: " << g_clientCount << ")\n";
}

// ─── mainThread: accept + обработка очереди сообщений ───────────────────────

void* mainThread(void*) {
    while (g_running) {
        // Проверяем новые подключения (неблокирующий select)
        fd_set acceptSet;
        FD_ZERO(&acceptSet);
        FD_SET(g_serverSock, &acceptSet);
        struct timeval tv = {0, 0};

        if (select(g_serverSock + 1, &acceptSet, NULL, NULL, &tv) > 0) {
            int clientSock = accept(g_serverSock, NULL, NULL);
            if (clientSock >= 0) {
                if (g_clientCount < MAX_CLIENTS)
                    handleNewConnection(clientSock);
                else
                    close(clientSock);
            }
        }

        // Обрабатываем одно сообщение из очереди
        pthread_mutex_lock(&g_messageQueueCs);
        if (g_messageQueue.empty()) {
            pthread_mutex_unlock(&g_messageQueueCs);
            usleep(1000);
            continue;
        }
        Message msg = g_messageQueue.front();
        g_messageQueue.pop();
        pthread_mutex_unlock(&g_messageQueueCs);

        pthread_mutex_lock(&g_cs);

        if (msg.type == MSG_PUBLIC) {
            std::cout << "[" << msg.from << "] " << msg.text << "\n";
            char pub[MAX_MSG_LEN * 2 + MAX_NAME_LEN + 10];
            snprintf(pub, sizeof(pub), "%s: %s", msg.from, msg.text);
            sendToAll(MSG_PUBLIC, msg.from, pub, msg.sock);

        } else if (msg.type == MSG_PRIVATE) {
            std::cout << "[PRIVATE] " << msg.from << " -> " << msg.to
                      << ": " << msg.text << "\n";
            int targetIdx = findByName(msg.to);
            if (targetIdx != -1 && g_clients[targetIdx] && g_clients[targetIdx]->active) {
                char priv[MAX_PACKET_SIZE];
                snprintf(priv, sizeof(priv), "[Приватно] %s: %s", msg.from, msg.text);
                queueSend(g_clients[targetIdx]->sock, MSG_PRIVATE, msg.from, msg.to, priv);
                queueSend(msg.sock, MSG_SYSTEM, "SYSTEM", "", "Сообщение отправлено");
            } else {
                queueSend(msg.sock, MSG_SYSTEM, "SYSTEM", "", "Пользователь не найден");
            }

        } else if (msg.type == MSG_LOGOUT) {
            sendToAll(MSG_SYSTEM, "SYSTEM", msg.text, -1);
            std::cout << "[-] " << msg.text << "\n";

        } else if (msg.type == MSG_SYSTEM) {
            sendToAll(MSG_SYSTEM, "SYSTEM", msg.text, msg.sock);
        }

        pthread_mutex_unlock(&g_cs);
    }
    return NULL;
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    signal(SIGINT,  sigint_handler);
    signal(SIGPIPE, SIG_IGN); // не падать с SIGPIPE при записи в закрытый сокет

    std::cout << "Чат-сервер, порт: " << PORT << "\nCtrl+C для остановки\n\n";

    // Создаём epoll
    g_epoll_fd = epoll_create1(0);
    if (g_epoll_fd < 0) { perror("epoll_create1"); return 1; }

    // Создаём серверный сокет
    g_serverSock = socket(AF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(g_serverSock, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(g_serverSock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind"); return 1;
    }
    listen(g_serverSock, SOMAXCONN);

    // Инициализируем мьютексы
    pthread_mutex_init(&g_cs,             NULL);
    pthread_mutex_init(&g_sendQueueCs,    NULL);
    pthread_mutex_init(&g_messageQueueCs, NULL);

    memset(g_clients,       0, sizeof(g_clients));
    memset(g_partialSizes,  0, sizeof(g_partialSizes));

    // Запускаем потоки
    pthread_t hMain, hNet;
    pthread_create(&hMain, NULL, mainThread,    NULL);
    pthread_create(&hNet,  NULL, networkThread, NULL);

    pthread_join(hMain, NULL);

    g_running = 0;
    pthread_join(hNet, NULL);

    // Очистка
    pthread_mutex_destroy(&g_cs);
    pthread_mutex_destroy(&g_sendQueueCs);
    pthread_mutex_destroy(&g_messageQueueCs);

    for (int i = 0; i < MAX_CLIENTS; i++)
        if (g_clients[i]) free(g_clients[i]);

    close(g_serverSock);
    close(g_epoll_fd);

    std::cout << "Сервер завершён\n";
    return 0;
}
