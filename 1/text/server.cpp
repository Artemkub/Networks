#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
using namespace std;

struct Student {
    char surname[25];
    int grade1;
    int grade2;
    int grade3;
    int grade4;
};

long prev_size = 0;

int main() {
    SetConsoleCP(65001);
    SetConsoleOutputCP(65001);
    
    string f1 = "f1.txt"; 
    string f2 = "f2.txt"; 
    
    ifstream read_f1;
    ofstream write_f2;
    
    while(true) {
        read_f1.open(f1, ios::ate);
        if(read_f1.is_open()) {
            long current_size = read_f1.tellg(); 
            read_f1.close();
            
            if(current_size > prev_size) {
                read_f1.open(f1);
                if(read_f1.is_open()) {
                    read_f1.seekg(prev_size);
                    
                    char surname[25];
                    int g1, g2, g3, g4;
                    
                    if(!read_f1.getline(surname, 25)) {
                        cout << "error" << endl;
                    } else {
                        read_f1 >> g1 >> g2 >> g3 >> g4;
                        if(!read_f1) {
                            cout << "error" << endl;
                        } else {
                            read_f1.ignore(); 
                            
                            bool has_debt = false;
                            if(g1 < 3) has_debt = true;
                            if(g2 < 3) has_debt = true;
                            if(g3 < 3) has_debt = true;
                            if(g4 < 3) has_debt = true;

                            write_f2.open(f2, ios::app);
                            if(write_f2.is_open()) {
                                write_f2 << surname << endl;
                                if(has_debt)
                                    write_f2 << "1" << endl;
                                else
                                    write_f2 << "0" << endl;
                                write_f2.close();
                                
                                cout << "Обработан студент: " << surname << endl;
                            } else {
                                cout << "error" << endl;
                            }
                        }
                    }
                    
                    read_f1.close();
                    prev_size = current_size;
                } else {
                    cout << "error" << endl;
                }
            }
        } else {
            cout << "error" << endl;
        }
        Sleep(100);
    }
    return 0;
}