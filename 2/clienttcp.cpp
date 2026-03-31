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

using namespace std;

const int DEFAULT_PORT = 8080;
const char* DEFAULT_SERVER_IP = "127.0.0.1";
const int BUFFER_SIZE = 4096;

string serverIP = DEFAULT_SERVER_IP;
int port = DEFAULT_PORT;

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
        cout << "ID: " << id << ", Имя: " << name << " Зарплата: " << salary << " руб." << "\n";
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
    emp.id = inputInt("ID: ");
    emp.name = inputString("Имя: ");
    emp.salary = inputDouble("Зарплата: ");
    return emp;
}

void printUsage() {
    cout << "Использование: программа [IP] [порт]" << endl;
    cout << "Или измените глобальные переменные serverIP и port в коде" << endl;
}

int main(int argc, char* argv[]) {
    cout << "TCP КЛИЕНТ" << endl;
    cout << "Сервер: " << serverIP << ":" << port << endl;

    int clientSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (clientSocket < 0) {
        cerr << "Ошибка создания сокета" << endl;
        return 1;
    }

    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    if (inet_pton(AF_INET, serverIP.c_str(), &serverAddr.sin_addr) <= 0) {
        cerr << "Неверный IP адрес: " << serverIP << endl;
        close(clientSocket);
        return 1;
    }

    cout << "Подключение к серверу..." << endl;

    if (connect(clientSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Ошибка подключения" << endl;
        close(clientSocket);
        return 1;
    }

    cout << "Подключено успешно\n" << endl;

    char dataBuffer[BUFFER_SIZE];      
    char responseBuffer[BUFFER_SIZE]; 

    while (true) {
        Employee emp = inputEmployee();

        if (emp.id == 0) {
            cout << "Завершение работы." << endl;
            break;
        }

        cout << "\nОтправка данных" << endl;
        emp.print();

        int dataSize = emp.ZnachByte(dataBuffer);  

        if (send(clientSocket, (char*)&dataSize, sizeof(dataSize), 0) <= 0) {
            cerr << "Ошибка отправки размера" << endl;
            break;
        }

        if (send(clientSocket, dataBuffer, dataSize, 0) <= 0) {
            cerr << "Ошибка отправки данных" << endl;
            break;
        }

        int responseSize;
        if (recv(clientSocket, (char*)&responseSize, sizeof(responseSize), 0) <= 0) {
            cerr << "Ошибка получения размера ответа" << endl;
            break;
        }

        if (responseSize >= BUFFER_SIZE) {
            cerr << "Ответ слишком большой для стекового буфера!" << endl;
            break;
        }

        if (recv(clientSocket, responseBuffer, responseSize, 0) <= 0) {
            cerr << "Ошибка получения ответа" << endl;
            break;
        }

        responseBuffer[responseSize] = '\0'; 
        cout << "\nОТВЕТ ОТ СЕРВЕРА:" << endl;
        cout << responseBuffer << endl;
    }

    close(clientSocket);
    return 0;
}