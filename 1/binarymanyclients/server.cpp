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
    
    ifstream read_f1;
    ofstream write_f2;
    
    while(true) {
        read_f1.open(f1, ios::binary | ios::ate);
        if(read_f1.is_open()) {
            long current_size = read_f1.tellg();
            read_f1.close();
            
            while(current_size - prev_size >= (long)sizeof(Request)) {
                read_f1.open(f1, ios::binary);
                if(read_f1.is_open()) {
                    read_f1.seekg(prev_size);
                    
                    Request req;
                    read_f1.read((char*)&req, sizeof(Request));
                    if(!read_f1) {
                        cout << "error" << endl;
                        read_f1.close();
                        break;
                    }
                    
                    Response resp;
                    strcpy_s(resp.surname, req.surname);
                    resp.has_debts = 0;
                    
                    for(int i = 0; i < 4; i++) {
                        if(req.grades[i] < 3) {
                            resp.has_debts = 1;
                            break;
                        }
                    }
                    
                    string f2_name = string("f2_") + req.client_name + ".bin";
                    
                    write_f2.open(f2_name, ios::binary | ios::app);
                    if(write_f2.is_open()) {
                        write_f2.write((char*)&resp, sizeof(Response));
                        write_f2.close();
                        
                        cout << "Клиент: " << req.client_name
                             << " - Студент: " << req.surname << endl;
                    } else {
                        cout << "error" << endl;
                    }
                    
                    read_f1.close();
                    prev_size += sizeof(Request);
                } else {
                    cout << "error" << endl;
                    break;
                }
            }
        } else {
            cout << "error" << endl;
        }
        Sleep(100);
    }
    return 0;
}