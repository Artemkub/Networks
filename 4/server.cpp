#include <iostream>
#include <string>
#include <cstring>
#include <thread>
#include <mutex>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

using namespace std;

mutex coutMutex;
mutex clientsMutex;
int activeCli = 0;
const int MAX_CLIENTS = 10;
const int buf_size = 4096;
const int PORT = 8080;

struct Employee {
    int id;
    string name;
    double salary;

    int ZnachByte(char* buffer) const {
        char* ptr = buffer;

        char* idPtr = (char*)&id;
        for (int i = 0; i < sizeof(id); i++) {
            *ptr = idPtr[i];
            ptr++;
        }

        int nameLen = name.length();
        char* lenPtr = (char*)&nameLen;
        for (int i = 0; i < sizeof(nameLen); i++) {
            *ptr = lenPtr[i];
            ptr++;
        }

        for (int i = 0; i < nameLen; i++) {
            *ptr = name[i];
            ptr++;
        }

        char* salaryPtr = (char*)&salary;
        for (int i = 0; i < sizeof(salary); i++) {
            *ptr = salaryPtr[i];
            ptr++;
        }

        return ptr - buffer;
    }

    bool deserialize(const char* buffer, int size) {
        const char* ptr = buffer;

        if (ptr + sizeof(int) > buffer + size)
            return false;
        char* idPtr = (char*)&id;
        for (int i = 0; i < sizeof(id); i++) {
            idPtr[i] = *ptr;
            ptr++;
        }

        if (ptr + sizeof(int) > buffer + size)
            return false;
        int nameLen;
        char* lenPtr = (char*)&nameLen;
        for (int i = 0; i < sizeof(nameLen); i++) {
            lenPtr[i] = *ptr;
            ptr++;
        }

        if (ptr + nameLen > buffer + size)
            return false;
        name.assign(ptr, nameLen);
        ptr += nameLen;

        if (ptr + sizeof(double) > buffer + size)
            return false;
        char* salaryPtr = (char*)&salary;
        for (int i = 0; i < sizeof(salary); i++) {
            salaryPtr[i] = *ptr;
            ptr++;
        }

        return true;
    }

    void print() const {
        cout << "ID: " << id << ", Имя: " << name << ", Зарплата: " << salary << " руб." << "\n";
    }
};


bool recvAll(int socket, char* buffer, int size) {
    int total = 0;
    while (total < size) {
        int bytes = recv(socket, buffer + total, size - total, 0);
        if (bytes <= 0) return false;
        total += bytes;
    }
    return true;
}

void HandleClient(int clientSocket, string clientIP) {
    struct timeval timeout;
    timeout.tv_sec = 60;
    timeout.tv_usec = 0;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char dataBuffer[buf_size];

    {
        lock_guard<mutex> lock(clientsMutex);
        activeCli++;
        cout << "\nКлиент " << clientIP << " подключился. Активных клиентов: "
            << activeCli << "/" << MAX_CLIENTS << endl;
    }

    while (true) {
        int dataSize;
        if (!recvAll(clientSocket, (char*)&dataSize, sizeof(dataSize))) {
            break;
        }

        if (dataSize <= 0 || dataSize >= buf_size) {
            break;
        }


        if (!recvAll(clientSocket, dataBuffer, dataSize)) {
            break;
        }

        Employee emp;
        if (!emp.deserialize(dataBuffer, dataSize)) {
            lock_guard<mutex> lock(coutMutex);
            cout << "[" << clientIP << "] Ошибка десериализации структуры" << endl;
            continue;
        }

        {
            lock_guard<mutex> lock(coutMutex);
            cout << "\n[КЛИЕНТ " << clientIP << "] прислал структуру:" << endl;
            emp.print();
            cout << "[СЕРВЕР] эхо-возврат " << dataSize << " байт" << endl;
        }

        if (send(clientSocket, (char*)&dataSize, sizeof(dataSize), 0) <= 0) break;
        if (send(clientSocket, dataBuffer, dataSize, 0) <= 0) break;
    }

    close(clientSocket);

    {
        lock_guard<mutex> lock(clientsMutex);
        activeCli--;
        cout << "\nКлиент " << clientIP << " отключился. Активных клиентов: "
            << activeCli << "/" << MAX_CLIENTS << endl;
    }
}

int main() {
    cout << "Порт: " << PORT << endl;

    signal(SIGPIPE, SIG_IGN);

    int listenSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (listenSocket < 0) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(listenSocket, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(opt));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(listenSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Ошибка привязки" << endl;
        close(listenSocket);
        return 1;
    }

    if (listen(listenSocket, SOMAXCONN) < 0) {
        cerr << "Ошибка прослушивания" << endl;
        close(listenSocket);
        return 1;
    }

    cout << "Ожидание подключений" << endl;

    while (true) {
        sockaddr_in clientAddr;
        socklen_t clientAddrSize = sizeof(clientAddr);
        int clientSocket = accept(listenSocket, (sockaddr*)&clientAddr, &clientAddrSize);

        if (clientSocket < 0) {
            cerr << "Ошибка accept" << endl;
            continue;
        }

        char clientIP[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);

        {
            lock_guard<mutex> lock(clientsMutex);
            if (activeCli >= MAX_CLIENTS) {
                string rejectMsg = "Сервер перегружен. Максимум клиентов: " + to_string(MAX_CLIENTS);
                send(clientSocket, rejectMsg.c_str(), rejectMsg.size(), 0);
                close(clientSocket);
                continue;
            }
        }

        thread clientThread(HandleClient, clientSocket, string(clientIP));
        clientThread.detach();
    }

    close(listenSocket);
    return 0;
}
