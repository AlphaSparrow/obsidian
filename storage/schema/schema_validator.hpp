#pragma once

#include "market_data_schema.hpp"
#include "storage/common/status.hpp"
#include <string>
#include <vector>
#include <unordered_map>

namespace obsidian {
namespace storage {
namespace schema {

struct SchemaValidationResult {
    bool is_valid{true};
    bool requires_evolution{false};
    std::vector<std::string> drift_errors;
    std::vector<std::string> warnings;
    Schema evolved_schema{{}};
};

class SchemaValidator {
public:
    // Determines if a source data type can be safely widened/promoted to a target type without precision loss
    static bool CanPromote(DataType source, DataType target) noexcept;

    // Validates an incoming schema against the authoritative table base schema.
    // Detects type mismatches, illegal narrowing, missing required non-nullable fields, and unexpected fields.
    static SchemaValidationResult ValidateCompatibility(
        const Schema& base_schema,
        const Schema& incoming_schema,
        bool allow_additive_evolution = true);

    // Merges an incoming schema into a base schema, promoting compatible widened types
    static StatusOr<Schema> MergeSchemas(
        const Schema& base_schema,
        const Schema& incoming_schema);
};

} // namespace schema
} // namespace storage
} // namespace obsidian
