#ifndef BANK_H
#define BANK_H

#include <condition_variable>
#include <cstddef>
#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace bank {
const size_t DEFAULT_BALANCE = 100;

struct user;

struct transaction {
    // cppcheck-suppress unusedStructMember
    const user *const counterparty;
    // cppcheck-suppress unusedStructMember
    const int balance_delta_xts;
    // cppcheck-suppress unusedStructMember
    const std::string comment;
};

struct user_transactions_iterator {
public:
    transaction wait_next_transaction();

private:
    explicit user_transactions_iterator(const user *usr_, std::size_t sz);
    const user *usr;
    std::size_t next_index;

    friend user;
};

struct user {
public:
    explicit user(std::string name_);

    user(user &&) = delete;

    user(const user &) = delete;

    ~user() = default;

    user &operator=(const user &) = delete;

    user &operator=(user &&) = delete;

    const std::string &name() const noexcept;

    int balance_xts() const;

    void transfer(user &counterparty,
                  int amount_xts,
                  const std::string &comment);

    template <typename T>
    user_transactions_iterator snapshot_transactions(T functor) const {
        std::shared_lock lock{mut};
        functor(transactions, balance);
        return user_transactions_iterator(this, transactions.size());
    }

    [[nodiscard]] user_transactions_iterator monitor() const;

    friend struct user_transactions_iterator;

private:
    std::string user_name;
    int balance;
    std::vector<transaction> transactions;
    mutable std::shared_mutex mut;
    mutable std::condition_variable_any cond;
};

struct ledger {
public:
    user &get_or_create_user(const std::string &name) &;

private:
    std::unordered_map<std::string, user> users;
    std::mutex mut;
};

struct transfer_error : std::runtime_error {
    explicit transfer_error(const std::string &message);
};

struct not_enough_funds_error : transfer_error {
    explicit not_enough_funds_error(const std::string &message);
};

struct invalid_amount_error : transfer_error {
    explicit invalid_amount_error(const std::string &message);
};

struct invalid_counterparty_error : transfer_error {
    explicit invalid_counterparty_error(const std::string &message);
};
}  // namespace bank

#endif