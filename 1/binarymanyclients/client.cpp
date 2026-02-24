#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
using namespace std;

struct Request {
    char client_name[25]; 
    char surname[25];    
    int grades[4]; 
};


struct Response {
    char surname[25]; 
    int has_debts;  
};

long prev_size = 0;

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    
    string f1 = "f1.bin";
    
    char client_name[25];
    cout << "Введите ваш ник: ";
    cin.getline(client_name, 25);
    
    string f2_name = string("f2_") + client_name + ".bin";
    
    
    while(true) {
        cout << "\n1 - Write f1" << endl;
        cout << "2. Check f2" << endl;
        
        int choice;
        cin >> choice;
        cin.ignore();
        
        if(choice == 1) {
            Request req;
            memset(&req, 0, sizeof(Request));
            
            strcpy_s(req.client_name, client_name);
            
            cout << "Фамилия студента: ";
            cin.getline(req.surname, 25);
            
            cout << "Введите 4 оценки: ";
            cin >> req.grades[0] >> req.grades[1] >> req.grades[2] >> req.grades[3];
            cin.ignore();
            
            ofstream write_f1(f1, ios::binary | ios::app);
            if(write_f1.is_open()) {
                write_f1.write((char*)&req, sizeof(Request));
                write_f1.close();
                cout << "Запрос отправлен!" << endl;
            } else {
                cout << "Ошибка: не удалось открыть файл f1.bin для записи" << endl;
            }
        }
        else if(choice == 2) {
            ifstream read_f2(f2_name, ios::binary);
            if(read_f2.is_open()) {
                read_f2.seekg(0, ios::end);
                long current_size = read_f2.tellg();
                read_f2.close();
                while(current_size - prev_size >= (long)sizeof(Response)) {
                    read_f2.open(f2_name, ios::binary);
                    if(read_f2.is_open()) {
                        read_f2.seekg(prev_size);
                        
                        Response resp;
                        read_f2.read((char*)&resp, sizeof(Response));
                        cout << "Студент: " << resp.surname << endl;
                        if(resp.has_debts == 1) 
                            cout << "Has debts" << endl;
                        else 
                            cout << "No debts" << endl;
                        
                        read_f2.close();
                        prev_size += sizeof(Response);
                    }
                }
                
                if(current_size == prev_size) {
                    cout << "Новых ответов нет" << endl;
                }
            } else {
                cout << "файла с ответами (" << f2_name << ")" << endl;
            }
        }
        else if(choice == 3) {
            break;
        }
    }
    return 0;
}