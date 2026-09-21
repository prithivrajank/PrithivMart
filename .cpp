#include <iostream>
#include <unordered_map>
using namespace std;

class Account
{
public:
    string name;
    double balance = 0;
};

int main()
{
    unordered_map<int, Account> acc;
    int ch, id;
    double amt;
    string name;

    while (true)
    {
        cout << "\n1.Create  2.Deposit  3.Withdraw  4.Balance  5.Exit\n";
        cin >> ch;

        if (ch == 1)
        {
            cout << "Enter Account ID: ";
            cin >> id;

            if (acc.count(id))
                cout << "Account already exists\n";
            else
            {
                cout << "Enter Customer Name: ";
                cin >> name;

                acc[id] = {name, 0};
                cout << "Account Created\n";
            }
        }

        else if (ch == 2)
        {
            cout << "Enter ID and Amount: ";
            cin >> id >> amt;

            if (acc.count(id) && amt > 0)
            {
                acc[id].balance += amt;
                cout << "Deposit Successful\n";
            }
            else
                cout << "Invalid Account/Amount\n";
        }

        else if (ch == 3)
        {
            cout << "Enter ID and Amount: ";
            cin >> id >> amt;

            if (acc.count(id) && amt > 0 && acc[id].balance >= amt)
            {
                acc[id].balance -= amt;
                cout << "Withdraw Successful\n";
            }
            else
                cout << "Invalid Account/Balance\n";
        }

        else if (ch == 4)
        {
            cout << "Enter Account ID: ";
            cin >> id;

            if (acc.count(id))
            {
                cout << "Customer: " << acc[id].name << endl;
                cout << "Balance: " << acc[id].balance << endl;
            }
            else
                cout << "Account Not Found\n";
        }

        else if (ch == 5)
            break;
    }

    return 0;
}