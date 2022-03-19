#include <boost/asio.hpp>
#include <cassert>
#include <exception>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>
#include "bank.h"

using boost::asio::ip::tcp;

void process_client(bank::ledger &ld, tcp::socket socket, std::mutex &mutex) {
    tcp::iostream client(std::move(socket));
    client << "What is your name?" << std::endl;
    std::string user_name;
    client >> user_name;
    bank::user &current_user = ld.get_or_create_user(user_name);
    client << "Hi " << user_name << std::endl;
    std::string command;
    while (client >> command) {
        if (command == "balance") {
            client << current_user.balance_xts() << std::endl;
        } else if (command == "transfer") {
            std::string counterparty;
            client >> counterparty;
            int amount = 0;
            client >> amount;
            std::string comment;
            client.get();
            getline(client, comment);
            try {
                current_user.transfer(ld.get_or_create_user(counterparty),
                                      amount, comment);
                client << "OK" << std::endl;
            } catch (bank::transfer_error &error) {
                client << error.what() << std::endl;
            }
        } else if (command == "transactions" || command == "monitor") {
            int num = 0;
            client >> num;
            client << "CPTY\tBAL\tCOMM\n";
            std::vector<bank::transaction> transaction_snap;
            auto iterator = current_user.snapshot_transactions(
                [&](const auto &transactions, [[maybe_unused]] int balance) {
                    if (transactions.size() <= static_cast<unsigned>(num)) {
                        transaction_snap = std::vector(transactions.begin(),
                                                       transactions.end());
                    } else {
                        transaction_snap = std::vector(transactions.end() - num,
                                                       transactions.end());
                    }
                });
            for (auto &transaction : transaction_snap) {
                if (transaction.counterparty == nullptr) {
                    client << "-\t" << transaction.balance_delta_xts << "\t"
                           << transaction.comment << std::endl;
                } else {
                    client << transaction.counterparty->name() << "\t"
                           << transaction.balance_delta_xts << '\t'
                           << transaction.comment << std::endl;
                }
            }
            client << "===== BALANCE: " << current_user.balance_xts()
                   << " XTS =====" << std::endl;
            if (command == "monitor") {
                while (true) {
                    bank::transaction new_transaction =
                        iterator.wait_next_transaction();
                    client << new_transaction.counterparty->name() << "\t"
                           << new_transaction.balance_delta_xts << "\t"
                           << new_transaction.comment << std::endl;
                }
            }
        } else {
            client << "Unknown command: '" << command << "'" << std::endl;
        }
    }
    mutex.lock();
    std::cout << "Disconnected " << client.socket().remote_endpoint() << " --> "
              << client.socket().local_endpoint() << std::endl;
    mutex.unlock();
}

int main([[maybe_unused]] int argc, char *argv[]) {
    try {
        assert(argc == 3);

        boost::asio::io_context io_context;
        // NOLINT
        tcp::acceptor acceptor(
            io_context,
            tcp::endpoint(tcp::v4(), static_cast<unsigned short>(
                                         std::atoi(argv[1]))));  // NOLINT
        std::cout << "Listening at " << acceptor.local_endpoint() << std::endl;

        std::string filename(argv[2]);  // NOLINT
        std::ofstream os(filename);
        os << acceptor.local_endpoint().port();
        os.close();

        std::mutex mutex;
        bank::ledger ld;

        while (true) {
            tcp::socket socket = acceptor.accept();
            mutex.lock();
            std::cout << "Listening at " << acceptor.local_endpoint()
                      << std::endl;
            mutex.unlock();
            std::thread t(process_client, std::ref(ld), std::move(socket),
                          std::ref(mutex));

            t.detach();
        }
    } catch (const std::runtime_error &e) {
        std::cerr << e.what() << std::endl;
    }
}
