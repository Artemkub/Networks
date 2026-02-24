#include <iostream>
#include <fstream>
#include <string>
#include <Windows.h>
using namespace std;

struct Request {
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
    string f2 = "f2.bin"; 
    
    
    while(true) {
        cout << "1 - Write f1" << endl;
        cout << "2 - Read f2 " << endl;
        
        int choice;
        cin >> choice;
        cin.ignore();
        
        if(choice == 1) {
            Request req;
            
            cout << "Фамилия: ";
            cin.getline(req.surname, 25);
            
            cout << "оценки: ";
            cin >> req.grades[0] >> req.grades[1] >> req.grades[2] >> req.grades[3];
            cin.ignore();
            
            ofstream write_f1(f1, ios::binary | ios::app);
            if(write_f1.is_open()) {
                write_f1.write((char*)&req, sizeof(Request));
                write_f1.close();
                cout << "Request send!" << endl;
            } else {
                cout << "Error" << endl;
            }
        }
        else if(choice == 2) {
            ifstream read_f2(f2, ios::binary);
            if(read_f2.is_open()) {
                read_f2.seekg(0, ios::end);
                long current_size = read_f2.tellg();
                read_f2.close();
                while(current_size - prev_size >= (long)sizeof(Response)) {
                    read_f2.open(f2, ios::binary);
                    if(read_f2.is_open()) {
                        read_f2.seekg(prev_size);
                        
                        Response resp;
                        read_f2.read((char*)&resp, sizeof(Response));
                        cout << resp.surname << endl;
                        if(resp.has_debts == 1) 
                            cout << "Has debts" << endl;
                        else 
                            cout << "No debts" << endl;
                        read_f2.close();
                        prev_size += sizeof(Response);
                    }
                }
                
                if(current_size == prev_size) {
                    cout << "No answers" << endl;
                }
            } else {
                cout << "Error f2 open" << endl;
            }
        }
    }
    return 0;
}
