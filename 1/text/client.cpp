

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
    string f2 = "f2.txt"; 
    
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
                write_f1 << surname << endl;
                write_f1 << g1 << " " << g2 << " " << g3 << " " << g4 << endl;
                write_f1.close();
                cout << "Request send!" << endl;
            } else {
                cout << "error" << endl;
            }
        }
        else if(choice == 2) {
            ifstream read_f2(f2);
            if(read_f2.is_open()) {
                read_f2.seekg(0, ios::end);
                long current_size = read_f2.tellg();
                read_f2.close();
                
                if(current_size > prev_size) {
                    read_f2.open(f2);
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
                cout << "error" << endl;
            }
        }
    }
    return 0;
}