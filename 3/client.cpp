// HTTP-клиент на C++ (Linux, POSIX сокеты)
// Задание 3: HTTP-CONNECTION
// Пункт а) + б): клиент отправляет GET-запрос к любому HTTP-серверу
//
// Примеры использования:
//   Host: httpbin.org   Port: 80  Path: /html
//   Host: json.org      Port: 80  Path: /
//   Host: localhost     Port: 8080  Path: /

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <sys/time.h>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdio>
#include <cstdlib>

using namespace std;

const int BUFFER_SIZE = 4096;
const int TIMEOUT_SEC = 5;

// Устанавливаем таймаут на чтение сокета
void set_timeout(int sock) {
    struct timeval timeout;
    timeout.tv_sec  = TIMEOUT_SEC;
    timeout.tv_usec = 0;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
}

// Читаем ровно size байт
bool recv_exact(int s, char* buf, int size) {
    int recvd = 0;
    while (recvd < size) {
        int n = recv(s, buf + recvd, size - recvd, 0);
        if (n <= 0) return false;
        recvd += n;
    }
    return true;
}

// Читаем HTTP-заголовки до двойного CRLF (\r\n\r\n)
string recv_headers(int sock) {
    string headers;
    char ch;
    int crlf_count = 0;

    while (crlf_count < 4) {
        int n = recv(sock, &ch, 1, 0);
        if (n <= 0) return "";
        headers += ch;

        if (ch == '\r' || ch == '\n') {
            crlf_count++;
        } else {
            crlf_count = 0;
        }
    }

    return headers;
}

// Выполняем GET-запрос
void request(const string& host, int port, const string& path) {
    int sock;
    addrinfo hints, *res, *ptr;
    char req[1024];

    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        perror("socket");
        return;
    }

    set_timeout(sock);

    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    int rc = getaddrinfo(host.c_str(), to_string(port).c_str(), &hints, &res);
    if (rc != 0) {
        cerr << "getaddrinfo error: " << gai_strerror(rc) << endl;
        close(sock);
        return;
    }

    bool connected = false;
    for (ptr = res; ptr != nullptr; ptr = ptr->ai_next) {
        if (connect(sock, ptr->ai_addr, (socklen_t)ptr->ai_addrlen) == 0) {
            connected = true;
            break;
        }
    }
    freeaddrinfo(res);

    if (!connected) {
        cerr << "Connection failed to " << host << ":" << port << endl;
        close(sock);
        return;
    }

    // Формируем HTTP GET-запрос
    snprintf(req, sizeof(req),
        "GET %s HTTP/1.1\r\n"
        "Host: %s\r\n"
        "Connection: close\r\n"
        "\r\n",
        path.c_str(), host.c_str());

    send(sock, req, (int)strlen(req), 0);

    // Получаем заголовки ответа
    string headers = recv_headers(sock);
    if (headers.empty()) {
        cout << "[!] Timeout: no response from server" << endl;
        close(sock);
        return;
    }

    cout << "\n--- Response Headers ---\n" << headers;

    // Извлекаем Content-Length из заголовков
    int content_length = -1;
    const char* cl = strstr(headers.c_str(), "Content-Length:");
    if (cl) {
        cl += 15;   // пропускаем "Content-Length:"
        while (*cl == ' ') cl++;
        content_length = atoi(cl);
    }

    cout << "--- Response Body ---\n";
    char buffer[BUFFER_SIZE];

    if (content_length > 0) {
        // Читаем ровно content_length байт
        int remaining = content_length;
        while (remaining > 0) {
            int to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
            if (recv_exact(sock, buffer, to_read)) {
                cout.write(buffer, to_read);
                remaining -= to_read;
            } else {
                cout << "\n[!] Error reading body" << endl;
                break;
            }
        }
    } else {
        // Content-Length отсутствует (chunked или сервер закрывает соединение)
        // Читаем до закрытия соединения
        int n;
        while ((n = recv(sock, buffer, BUFFER_SIZE - 1, 0)) > 0) {
            cout.write(buffer, n);
        }
    }

    cout << "\n--- End ---" << endl;
    close(sock);
}

int main() {
    string host, path;

    cout << "=== HTTP Client (Linux) ===" << endl;
    cout << "Введите 'q' в поле Host для выхода\n" << endl;

    while (true) {
        cout << "Host: ";
        getline(cin, host);
        if (host == "q" || host == "quit") break;
        if (host.empty()) continue;

        cout << "Port [80]: ";
        string p;
        getline(cin, p);
        int port = p.empty() ? (host == "localhost" ? 8080 : 80) : stoi(p);

        cout << "Path [/]: ";
        getline(cin, path);
        if (path.empty()) path = "/";

        cout << "\n> GET http://" << host << ":" << port << path << endl;
        request(host, port, path);
    }

    return 0;
}

// --- Примеры ---
// Host: httpbin.org   Port: 80  Path: /html
// Host: json.org      Port: 80  Path: /
// Host: localhost     Port: 8080  Path: /
