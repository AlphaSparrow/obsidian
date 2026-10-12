#include "transaction.hpp"
#include "lakehouse_table.hpp"
#include <thread>
#include <chrono>

namespace obsidian {
namespace storage {
namespace lakehouse {

Transaction::Transaction(std::shared_ptr<LakehouseTable> table, OperationType op)
    : table_(std::move(table)), op_(op), base_snapshot_id_(table_->current_snapshot_id()) {}

Transaction& Transaction::AppendFile(DataFile file) {
    added_files_.push_back(std::move(file));
    return *this;
}

Transaction& Transaction::DeleteFile(DataFile file) {
    deleted_files_.push_back(std::move(file));
    return *this;
}

Status Transaction::Commit() {
    if (!table_) {
        return Status::InvalidArgument("Transaction table reference is null");
    }
    return table_->CommitTransaction(base_snapshot_id_, op_, added_files_, deleted_files_);
}

Status TransactionRunner::Run(
    const std::shared_ptr<LakehouseTable>& table,
    OperationType op,
    int max_retries,
    const std::function<Status(Transaction& tx)>& action) {

    for (int attempt = 0; attempt <= max_retries; ++attempt) {
        Transaction tx(table, op);
        Status status = action(tx);
        if (!status.ok()) {
            return status;
        }

        status = tx.Commit();
        if (status.ok()) {
            return Status::Ok();
        }

        if (status.code() == StatusCode::ConcurrentModification && attempt < max_retries) {
            auto delay_ms = 10 * (1 << attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            continue;
        }

        return status;
    }

    return Status::ConcurrentModification("Max retries exceeded during optimistic transaction commit");
}

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
