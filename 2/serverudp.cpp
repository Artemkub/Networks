#include <iostream>
#include <string>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define NOMINMAX
#undef min
#undef max

using namespace std;

const int BUFFER_SIZE = 65535;  

int main() {
    const int PORT = 9090;

    cout << "UDP СЕРВЕР" << endl;

    int serverSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverSocket < 0) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);

    if (bind(serverSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Ошибка привязки" << endl;
        close(serverSocket);
        return 1;
    }

    char buffer[BUFFER_SIZE]; 
    sockaddr_in clientAddr;
    socklen_t clientAddrSize = sizeof(clientAddr);

    while (true) {
        int dataSize;
        int result = recvfrom(serverSocket, (char*)&dataSize, sizeof(dataSize), 0,
            (sockaddr*)&clientAddr, &clientAddrSize);

        if (result == sizeof(dataSize) && dataSize > 0 && dataSize < BUFFER_SIZE) {
            result = recvfrom(serverSocket, buffer, dataSize, 0,
                (sockaddr*)&clientAddr, &clientAddrSize);

            if (result == dataSize) {
                char clientIP[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &(clientAddr.sin_addr), clientIP, INET_ADDRSTRLEN);

                cout << "\n[КЛИЕНТ " << clientIP << ":" << ntohs(clientAddr.sin_port) << "]" << endl;

                sendto(serverSocket, (char*)&dataSize, sizeof(dataSize), 0,(sockaddr*)&clientAddr, clientAddrSize);
                sendto(serverSocket, buffer, dataSize, 0, (sockaddr*)&clientAddr, clientAddrSize);
            }
        }
    }

    close(serverSocket);
    return 0;
}