#include <iostream>
#include <string>
#include <cstring>
#include <limits>
#include <iomanip>
#include <cctype>  
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>

#define NOMINMAX
#undef min
#undef max

using namespace std;

const int DEFAULT_PORT = 9090;
const char* DEFAULT_SERVER_IP = "127.0.0.1";
const int MAX_RETRIES = 3;
const int TIMEOUT_MS = 2000;
const int BUFFER_SIZE = 4096;

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

        if (ptr + sizeof(int) > buffer + size) return false;
        char* idPtr = (char*)&id;
        for (int i = 0; i < sizeof(id); i++) {
            idPtr[i] = *ptr;
            ptr++;
        }

        if (ptr + sizeof(int) > buffer + size) return false;
        int nameLen;
        char* lenPtr = (char*)&nameLen;
        for (int i = 0; i < sizeof(nameLen); i++) {
            lenPtr[i] = *ptr;
            ptr++;
        }

        if (ptr + nameLen > buffer + size) return false;
        name.assign(ptr, nameLen);
        ptr += nameLen;

        if (ptr + sizeof(double) > buffer + size) return false;
        char* salaryPtr = (char*)&salary;
        for (int i = 0; i < sizeof(salary); i++) {
            salaryPtr[i] = *ptr;
            ptr++;
        }

        return true;
    }

    void print() const {
        cout << "ID: " << id << ", Имя: " << name << ", Зарплата: " << salary << " руб." << endl;
    }
};

int inputInt(const string& prompt) {
    int value;
    while (true) {
        cout << prompt;
        cin >> value;

        if (cin.fail()) {
            cin.clear();
            cin.ignore((numeric_limits<streamsize>::max)(), '\n');
            cout << "Введите целое число." << endl;
        }
        else {
            cin.ignore((numeric_limits<streamsize>::max)(), '\n');
            return value;
        }
    }
}

double inputDouble(const string& prompt) {
    double value;
    while (true) {
        cout << prompt;
        cin >> value;

        if (cin.fail()) {
            cin.clear();
            cin.ignore((numeric_limits<streamsize>::max)(), '\n');
            cout << "Введите число." << endl;
        }
        else if (value <= 0) {
            cout << "Зарплата должна быть положительной." << endl;
        }
        else {
            cin.ignore((numeric_limits<streamsize>::max)(), '\n');
            return value;
        }
    }
}

bool isValidName(const string& name) {
    if (name.empty()) return false;
    for (char c : name) {
        if (!isalpha(static_cast<unsigned char>(c)) && c != ' ' && c != '-') {
            return false;
        }
    }
    return true;
}

string inputString(const string& prompt) {
    string value;
    while (true) {
        cout << prompt;
        getline(cin, value);
        size_t start = value.find_first_not_of(" \t");
        size_t end = value.find_last_not_of(" \t");
        if (start != string::npos && end != string::npos) {
            value = value.substr(start, end - start + 1);
        }

        if (value.empty()) {
            cout << "Имя не может быть пустым" << endl;
        }
        else if (!isValidName(value)) {
            cout << "Имя должно содержать только буквы, пробелы и дефисы" << endl;
        }
        else {
            return value;
        }
    }
}

Employee inputEmployee() {
    Employee emp;
    cout << "\nВвод данных сотрудника" << endl;
    emp.id = inputInt("ID: ");
    emp.name = inputString("Имя: ");
    emp.salary = inputDouble("Зарплата: ");
    return emp;
}

void printUsage(const char* programName) {
    cout << "Использование: " << programName << " [IP] [порт]" << endl;
}

void Sleep_ms(int ms) {
    usleep(ms * 1000);
}

bool sendEchoWithRetry(int socket, const char* data, int dataSize, sockaddr_in& serverAddr, Employee& receivedEmp) {
    int retryCount = 0;
    char echoBuffer[BUFFER_SIZE];

    while (retryCount < MAX_RETRIES) {
        if (sendto(socket, (char*)&dataSize, sizeof(dataSize), 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            retryCount++;
            continue;
        }

        if (sendto(socket, data, dataSize, 0, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
            retryCount++;
            continue;
        }

        sockaddr_in fromAddr;
        socklen_t fromAddrSize = sizeof(fromAddr);

        int echoSize;
        int result = recvfrom(socket, (char*)&echoSize, sizeof(echoSize), 0, (sockaddr*)&fromAddr, &fromAddrSize);

        if (result == sizeof(echoSize) && echoSize > 0) {
            if (echoSize != dataSize || echoSize >= BUFFER_SIZE) {
                retryCount++;
                continue;
            }

            result = recvfrom(socket, echoBuffer, echoSize, 0, (sockaddr*)&fromAddr, &fromAddrSize);

            if (result == echoSize) {
                if (receivedEmp.deserialize(echoBuffer, echoSize)) {
                    return true;
                }
            }
        }

        retryCount++;

        if (retryCount < MAX_RETRIES) {
            Sleep_ms(1000);
        }
    }
    return false;
}

int main(int argc, char* argv[]) {
    string serverIP = DEFAULT_SERVER_IP;
    int port = DEFAULT_PORT;

    cout << "UDP КЛИЕНТ" << endl;

    int clientSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientSocket < 0) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    struct timeval timeout;
    timeout.tv_sec = TIMEOUT_MS / 1000;
    timeout.tv_usec = (TIMEOUT_MS % 1000) * 1000;
    setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "Неверный IP адрес: " << serverIP << endl;
        close(clientSocket);
        return 1;
    }

    char dataBuffer[BUFFER_SIZE];

    while (true) {
        Employee emp = inputEmployee();

        if (emp.id == 0) {
            break;
        }

        cout << "\nОтправка данных на сервер" << endl;
        emp.print();

        int dataSize = emp.ZnachByte(dataBuffer);
        Employee receivedEmp;

        if (sendEchoWithRetry(clientSocket, dataBuffer, dataSize, serverAddr, receivedEmp)) {
            cout << "\nЭхо получено от сервера:" << endl;
            receivedEmp.print();

            if (receivedEmp.id == emp.id &&
                receivedEmp.name == emp.name &&
                receivedEmp.salary == emp.salary) {
                cout << "✓ Данные совпадают с отправленными" << endl;
            }
            else {
                cout << "✗ Данные не совпадают!" << endl;
            }
        }
        else {
            cout << "\n❌ Не удалось получить эхо от сервера после "
                << MAX_RETRIES << " попыток." << endl;
        }
    }

    close(clientSocket);
    return 0;
}