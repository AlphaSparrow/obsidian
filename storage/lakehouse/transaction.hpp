#pragma once

#include "lakehouse_structures.hpp"
#include "manifest_manager.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <memory>
#include <functional>

namespace obsidian {
namespace storage {
namespace lakehouse {

class LakehouseTable;

enum class OperationType : uint8_t {
    Append,
    Overwrite,
    Delete,
    Compaction
};

inline const char* OperationTypeToString(OperationType op) {
    switch (op) {
        case OperationType::Append: return "append";
        case OperationType::Overwrite: return "overwrite";
        case OperationType::Delete: return "delete";
        case OperationType::Compaction: return "compaction";
        default: return "unknown";
    }
}

class Transaction {
public:
    Transaction(std::shared_ptr<LakehouseTable> table, OperationType op);

    // Staged data file to be committed to the table
    Transaction& AppendFile(DataFile file);

    // Marks an existing data file for deletion / replacement
    Transaction& DeleteFile(DataFile file);

    // Executes atomic optimistic concurrency commit
    Status Commit();

    [[nodiscard]] int64_t base_snapshot_id() const noexcept {
        return base_snapshot_id_;
    }

    [[nodiscard]] OperationType operation() const noexcept {
        return op_;
    }

private:
    std::shared_ptr<LakehouseTable> table_;
    OperationType op_;
    int64_t base_snapshot_id_;
    std::vector<DataFile> added_files_;
    std::vector<DataFile> deleted_files_;
};

// Helper for automatically retrying transactions on ConcurrentModification
class TransactionRunner {
public:
    static Status Run(
        const std::shared_ptr<LakehouseTable>& table,
        OperationType op,
        int max_retries,
        const std::function<Status(Transaction& tx)>& action);
};

} // namespace lakehouse
} // namespace storage
} // namespace obsidian
