// Чат-клиент на C++ (Linux)
// Задание 5: многопоточный клиент-серверный чат
//
// Использование:
//   ./client
//   Ваш ник: Alice
//   Команды:
//     /write to ник сообщение  — приватное сообщение
//     /help                    — справка
//     /quit                    — выход

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

#define PORT       8888
#define MAX_NAME   32
#define MAX_MSG    256
#define SERVER_IP  "localhost"

#define MSG_LOGIN   1
#define MSG_PUBLIC  2
#define MSG_PRIVATE 3
#define MSG_LOGOUT  4
#define MSG_SYSTEM  5

// ─── Глобальные переменные ────────────────────────────────────────────────────

int    g_sock      = -1;
char   g_name[MAX_NAME];
volatile int g_connected = 1;

pthread_mutex_t g_cs;       // защита g_connected и g_sock
pthread_mutex_t g_printCs;  // защита консольного вывода

// ─── Вспомогательные функции ──────────────────────────────────────────────────

// Безопасный вывод: затирает строку ввода, печатает сообщение, восстанавливает приглашение
void safePrint(const std::string& message, int showPrompt) {
    pthread_mutex_lock(&g_printCs);
    std::cout << "\r" << message << "\n";
    if (showPrompt) {
        std::cout << g_name << "> ";
        std::cout.flush();
    }
    pthread_mutex_unlock(&g_printCs);
}

int isConnected() {
    pthread_mutex_lock(&g_cs);
    int r = g_connected;
    pthread_mutex_unlock(&g_cs);
    return r;
}

// Отправить данные; при ошибке — сбросить флаг g_connected
int safeSend(const char* data, int len) {
    if (!isConnected()) return 0;
    // MSG_NOSIGNAL — не падать с SIGPIPE если сервер закрыл соединение
    int result = send(g_sock, data, len, MSG_NOSIGNAL);
    if (result < 0) {
        pthread_mutex_lock(&g_cs);
        g_connected = 0;
        pthread_mutex_unlock(&g_cs);
        return 0;
    }
    return 1;
}

// ─── Упаковка и отправка пакета ──────────────────────────────────────────────

void sendMsg(int type, const char* to, const char* text) {
    char buffer[1024];
    int pos = 0, len;

    memcpy(buffer + pos, &type, sizeof(int)); pos += sizeof(int);

    len = (int)strlen(g_name) + 1;
    memcpy(buffer + pos, &len, sizeof(int)); pos += sizeof(int);
    memcpy(buffer + pos, g_name, len);       pos += len;

    len = (int)strlen(to) + 1;
    memcpy(buffer + pos, &len, sizeof(int)); pos += sizeof(int);
    memcpy(buffer + pos, to, len);           pos += len;

    len = (int)strlen(text) + 1;
    memcpy(buffer + pos, &len, sizeof(int)); pos += sizeof(int);
    memcpy(buffer + pos, text, len);         pos += len;

    if (!safeSend((char*)&pos, sizeof(int))) return;
    safeSend(buffer, pos);
}

// ─── Приём пакета от сервера ─────────────────────────────────────────────────
//
// Возвращает:  1 — пакет получен
//              0 — таймаут (нет данных, попробовать снова)
//             -1 — ошибка / соединение закрыто

int recvPacket(int* type, char* from, char* to, char* msg) {
    // Таймаут 1 секунда — чтобы поток мог проверять g_connected
    struct timeval tv = {1, 0};
    setsockopt(g_sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    char buffer[1024];
    int size;

    int result = recv(g_sock, (char*)&size, sizeof(int), 0);
    if (result <= 0) {
        if (result == 0) return -1; // сервер закрыл соединение
        if (errno == EAGAIN || errno == EWOULDBLOCK) return 0; // таймаут
        return -1;
    }

    result = recv(g_sock, buffer, size, MSG_WAITALL);
    if (result <= 0) return -1;

    int pos = 0, len;
    memcpy(type, buffer + pos, sizeof(int)); pos += sizeof(int);

    memcpy(&len,  buffer + pos, sizeof(int)); pos += sizeof(int);
    memcpy(from,  buffer + pos, len);          pos += len;

    memcpy(&len,  buffer + pos, sizeof(int)); pos += sizeof(int);
    memcpy(to,    buffer + pos, len);          pos += len;

    memcpy(&len,  buffer + pos, sizeof(int)); pos += sizeof(int);
    memcpy(msg,   buffer + pos, len);

    return 1;
}

// ─── receiveThread: фоновый приём сообщений ──────────────────────────────────

void* receiveThread(void*) {
    int type;
    char from[MAX_NAME], to[MAX_NAME], msg[MAX_MSG * 2];

    while (isConnected()) {
        int result = recvPacket(&type, from, to, msg);

        if (result == -1) {
            pthread_mutex_lock(&g_cs);
            g_connected = 0;
            pthread_mutex_unlock(&g_cs);
            safePrint("[Сервер] Соединение разорвано", 0);
            break;
        }
        if (result == 0) continue; // таймаут — проверяем g_connected и ждём снова

        std::string output;
        if (type == MSG_PUBLIC) {
            output = "[Общий чат] ";
            output += msg;
        } else if (type == MSG_PRIVATE) {
            output = msg; // уже содержит "[Приватно] Ник: текст"
        } else if (type == MSG_SYSTEM) {
            output = "[Сервер] ";
            output += msg;
        }
        safePrint(output, 1);
    }
    return NULL;
}

// ─── cleanup ─────────────────────────────────────────────────────────────────

void cleanup() {
    pthread_mutex_lock(&g_cs);
    g_connected = 0;
    pthread_mutex_unlock(&g_cs);

    if (g_sock >= 0) {
        shutdown(g_sock, SHUT_RDWR);
        close(g_sock);
        g_sock = -1;
    }

    pthread_mutex_destroy(&g_cs);
    pthread_mutex_destroy(&g_printCs);
}

// ─── main ────────────────────────────────────────────────────────────────────

int main() {
    signal(SIGPIPE, SIG_IGN); // не падать с SIGPIPE

    std::cout << "Чат клиент\n\n";
    std::cout << "Ваш ник: ";
    std::cin.getline(g_name, MAX_NAME);
    if (strlen(g_name) == 0) strcpy(g_name, "Guest");

    pthread_mutex_init(&g_cs,      NULL);
    pthread_mutex_init(&g_printCs, NULL);

    // Создаём сокет
    g_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_sock < 0) {
        std::cout << "Ошибка создания сокета\n";
        cleanup();
        return 1;
    }

    // Резолвим имя хоста
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(SERVER_IP, std::to_string(PORT).c_str(), &hints, &res) != 0) {
        std::cout << "Ошибка разрешения имени: " << SERVER_IP << "\n";
        cleanup();
        return 1;
    }

    std::cout << "Подключение к серверу " << SERVER_IP << ":" << PORT << "...\n";

    // Неблокирующий connect с таймаутом 5 секунд
    int flags = fcntl(g_sock, F_GETFL, 0);
    fcntl(g_sock, F_SETFL, flags | O_NONBLOCK);

    connect(g_sock, res->ai_addr, res->ai_addrlen);
    freeaddrinfo(res);

    fd_set fdset;
    FD_ZERO(&fdset);
    FD_SET(g_sock, &fdset);
    struct timeval tv = {5, 0};

    if (select(g_sock + 1, NULL, &fdset, NULL, &tv) != 1) {
        std::cout << "Ошибка подключения (таймаут)\n";
        cleanup();
        return 1;
    }

    // Возвращаем блокирующий режим
    fcntl(g_sock, F_SETFL, flags & ~O_NONBLOCK);

    std::cout << "Подключено к серверу!\n\n";

    // Отправляем логин
    sendMsg(MSG_LOGIN, "", "");

    // Запускаем фоновый поток приёма
    pthread_t hThread;
    pthread_create(&hThread, NULL, receiveThread, NULL);

    std::cout << "Команды:\n";
    std::cout << "  /write to ник сообщение — приватное сообщение\n";
    std::cout << "  /quit                   — выйти из чата\n\n";

    char input[MAX_MSG];
    char target[MAX_NAME];

    std::cout << g_name << "> ";

    while (isConnected()) {
        std::cin.getline(input, MAX_MSG);

        if (!isConnected()) break;

        if (strlen(input) == 0) {
            std::cout << g_name << "> ";
            continue;
        }

        if (input[0] == '/') {
            if (strcmp(input, "/quit") == 0) {
                sendMsg(MSG_LOGOUT, "", "");
                break;

            } else if (strcmp(input, "/help") == 0) {
                safePrint("[Справка] /write to ник сообщение — личное сообщение, /quit — выход", 1);

            } else if (strncmp(input, "/write to ", 10) == 0) {
                char* ptr   = input + 10;           // "ник сообщение"
                char* space = strchr(ptr, ' ');     // найти пробел между ником и текстом
                if (space && strlen(space + 1) > 0) {
                    *space = '\0';
                    strncpy(target, ptr, MAX_NAME - 1);
                    sendMsg(MSG_PRIVATE, target, space + 1);
                } else {
                    safePrint("[Ошибка] Формат: /write to ник сообщение", 1);
                }

            } else {
                safePrint("[Ошибка] Неизвестная команда. Введите /help", 1);
            }
        } else {
            sendMsg(MSG_PUBLIC, "", input);
        }

        std::cout << g_name << "> ";
    }

    // Ждём завершения потока приёма
    pthread_mutex_lock(&g_cs);
    g_connected = 0;
    pthread_mutex_unlock(&g_cs);

    pthread_join(hThread, NULL);

    cleanup();
    std::cout << "\nВыход из чата\n";
    return 0;
}
