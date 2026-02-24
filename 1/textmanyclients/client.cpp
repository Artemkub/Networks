
#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
using namespace std;

long prev_size = 0;

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    
    string f1 = "f1.txt"; 
    
    char client_name[25];
    cout << "Enter your nick: ";
    cin.getline(client_name, 25);
    
    string f2_name = string("f2_") + client_name + ".txt";
    
    while(true) {
        cout << "\n1 - Write f1" << endl;
        cout << "2. Check f2" << endl;
        
        int choice;
        cin >> choice;
        cin.ignore();
        
        if(choice == 1) {
            char surname[25];
            int g1, g2, g3, g4;
            
            cout << "Фамилия студента: ";
            cin.getline(surname, 25);
            
            cout << "Введите 4 оценки: ";
            cin >> g1 >> g2 >> g3 >> g4;
            cin.ignore();
            
            ofstream write_f1(f1, ios::app);
            if(write_f1.is_open()) {
                char buffer[256];
                int len = sprintf_s(
                    buffer,
                    "%s\n%s\n%d %d %d %d\n",
                    client_name,
                    surname,
                    g1, g2, g3, g4
                );

                if(len > 0) {
                    write_f1.write(buffer, len);
                }

                write_f1.close();
                cout << "Request send!" << endl;
            } else {
                cout << "error" << endl;
            }
        }
        else if(choice == 2) {
            ifstream read_f2(f2_name);
            if(read_f2.is_open()) {
                read_f2.seekg(0, ios::end);
                long current_size = read_f2.tellg();
                read_f2.close();
                
                if(current_size > prev_size) {
                    read_f2.open(f2_name);
                    if(read_f2.is_open()) {
                        read_f2.seekg(prev_size);
                        
                        char surname[25];
                        char result[2];
                        
                        while(read_f2.getline(surname, 25) && read_f2.getline(result, 2)) {
                            cout << "Студент: " << surname << " - ";
                            if(result[0] == '1')
                                cout << "Has debts" << endl;
                            else
                                cout << "No debts" << endl;
                        }

                        read_f2.close();
                        prev_size = current_size;
                    }
                }
                else {
                    cout << "Новых ответов нет" << endl;
                }
            } else {
                cout << "Пока нет файла с ответами (" << f2_name << ")" << endl;
            }
        }
    }
    return 0;
}