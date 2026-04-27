// HTTP-сервер на C++ (Linux, POSIX сокеты)
// Задание 3: HTTP-CONNECTION
// Пункт а) + в): сервер принимает GET-запросы и отдаёт HTML-файл
// Проверка: открыть браузер и перейти на http://localhost:8080/

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/time.h>
#include <climits>
#include <iostream>
#include <fstream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace std;

const int PORT        = 8080;
const int BUFFER_SIZE = 4096;
const int TIMEOUT_SEC = 3;

// Устанавливаем таймаут на чтение сокета
void set_timeout(int sock) {
    struct timeval timeout;
    timeout.tv_sec  = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

// Отправляем HTTP-ответ с ошибкой
void send_error(int client, int code, const char* msg) {
    char response[512];
    int len = snprintf(response, sizeof(response),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n%s",
        code, msg, (int)strlen(msg), msg);

    send(client, response, len, 0);
}

// Отправляем файл клиенту
void send_file(int client, const char* filename) {
    char fullpath[PATH_MAX];
    getcwd(fullpath, PATH_MAX);   // текущий каталог (где запущен сервер)
    strcat(fullpath, "/");
    strcat(fullpath, filename);

    ifstream file(fullpath, ios::binary);
    if (!file.is_open()) {
        send_error(client, 404, "404 Not Found");
        return;
    }

    file.seekg(0, ios::end);
    long size = (long)file.tellg();
    file.seekg(0, ios::beg);

    char headers[512];
    int header_len = snprintf(headers, sizeof(headers),
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/html; charset=utf-8\r\n"
        "Content-Length: %ld\r\n"
        "Connection: close\r\n"
        "\r\n",
        size);

    send(client, headers, header_len, 0);

    char buffer[BUFFER_SIZE];
    long remaining = size;

    while (remaining > 0) {
        int to_read = (int)((remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE);
        file.read(buffer, to_read);
        int bytes_read = (int)file.gcount();

        if (bytes_read <= 0) break;

        send(client, buffer, bytes_read, 0);
        remaining -= bytes_read;
    }

    file.close();
}

int main() {
    int server_sock, client_sock;
    sockaddr_in addr;
    char buffer[BUFFER_SIZE];

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("socket");
        return 1;
    }

    // Разрешаем повторное использование порта сразу после закрытия
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons(PORT);

    if (bind(server_sock, (sockaddr*)&addr, sizeof(addr)) < 0) {
        perror("bind");
        return 1;
    }

    listen(server_sock, 5);
    cout << "HTTP Server listening on port " << PORT << endl;
    cout << "Open browser: http://localhost:" << PORT << "/" << endl;

    while (true) {
        socklen_t len = sizeof(addr);
        client_sock = accept(server_sock, (sockaddr*)&addr, &len);
        if (client_sock < 0) {
            perror("accept");
            continue;
        }

        set_timeout(client_sock);
        cout << "\n[+] Client connected: " << inet_ntoa(addr.sin_addr) << endl;

        int bytes = recv(client_sock, buffer, BUFFER_SIZE - 1, 0);

        if (bytes > 0) {
            buffer[bytes] = '\0';

            // Выводим первую строку запроса
            char buf_copy[BUFFER_SIZE];
            strncpy(buf_copy, buffer, BUFFER_SIZE - 1);
            buf_copy[BUFFER_SIZE - 1] = '\0';
            char* first_line = strtok(buf_copy, "\r\n");
            if (first_line) {
                cout << "Request: " << first_line << endl;
            }

            if (strncmp(buffer, "GET", 3) == 0) {
                // Извлекаем путь из запроса вида: GET /path HTTP/1.1
                char path[256] = "/";
                char* p = strchr(buffer, ' ');
                if (p) {
                    char* p2 = strchr(p + 1, ' ');
                    if (p2) {
                        int plen = (int)(p2 - (p + 1));
                        if (plen > 0 && plen < 255) {
                            memcpy(path, p + 1, plen);
                            path[plen] = '\0';
                        }
                    }
                }

                string filename;
                if (strcmp(path, "/") == 0) {
                    filename = "index.html";   // корень -> index.html
                } else {
                    filename = path + 1;       // убираем ведущий '/'
                }

                cout << "Serving: " << filename << endl;
                send_file(client_sock, filename.c_str());
            } else {
                send_error(client_sock, 501, "501 Not Implemented");
            }
        } else if (bytes == 0) {
            cout << "Client closed connection" << endl;
        } else {
            cout << "Timeout or recv error" << endl;
        }

        close(client_sock);
        cout << "[-] Client disconnected" << endl;
    }

    close(server_sock);
    return 0;
}
