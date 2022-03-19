#include "bank.h"
#include <cassert>
#include <cstddef>
#include <mutex>
#include <string>

namespace bank {
user &ledger::get_or_create_user(const std::string &name) & {
    std::unique_lock lock{mut};
    return users.emplace(name, name).first->second;
}

user::user(std::string name_)
    : user_name(std::move(name_)),
      balance(DEFAULT_BALANCE),
      transactions({transaction{nullptr, DEFAULT_BALANCE,
                                "Initial deposit for " + user_name}}) {
}

const std::string &user::name() const noexcept {
    return user_name;
}

int user::balance_xts() const {
    std::shared_lock lock{mut};
    return balance;
}

void user::transfer(user &counterparty,
                    int amount_xts,
                    const std::string &comment) {
    if (this == &counterparty) {
        throw invalid_counterparty_error("Self-transaction");
    }

    std::scoped_lock lock{mut, counterparty.mut};

    if (amount_xts <= 0) {
        throw invalid_amount_error("Amount_xts " + std::to_string(amount_xts) +
                                   " is not valid");
    }

    if (amount_xts > balance) {
        throw not_enough_funds_error(
            "Not enough funds: " + std::to_string(balance) +
            " XTS available, " + std::to_string(amount_xts) + " XTS requested");
    }

    balance -= amount_xts;
    counterparty.balance += amount_xts;

    transactions.push_back({&counterparty, -amount_xts, comment});
    cond.notify_all();

    counterparty.transactions.push_back({this, amount_xts, comment});
    counterparty.cond.notify_all();
}

user_transactions_iterator user::monitor() const {
    std::unique_lock lock{mut};
    return user_transactions_iterator(this, transactions.size());
}

transfer_error::transfer_error(const std::string &message)
    : std::runtime_error(message) {
}

not_enough_funds_error::not_enough_funds_error(const std::string &message)
    : transfer_error(message) {
}

invalid_amount_error::invalid_amount_error(const std::string &message)
    : transfer_error(message) {
}

invalid_counterparty_error::invalid_counterparty_error(
    const std::string &message)
    : transfer_error(message) {
}

user_transactions_iterator::user_transactions_iterator(const user *usr_,
                                                       std::size_t sz_)
    : usr(usr_), next_index(sz_) {
}

transaction user_transactions_iterator::wait_next_transaction() {
    std::shared_lock lock(usr->mut);
    usr->cond.wait(lock, [&usr = usr, &next_index = next_index]() {
        return usr->transactions.size() > next_index;
    });
    return usr->transactions[next_index++];
}
}  // namespace bank