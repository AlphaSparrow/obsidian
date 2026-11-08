#include "schema_validator.hpp"
#include <unordered_set>

namespace obsidian {
namespace storage {
namespace schema {

bool SchemaValidator::CanPromote(DataType source, DataType target) noexcept {
    if (source == target) return true;

    switch (source) {
        case DataType::Int8:
            return (target == DataType::Int16 || target == DataType::Int32 || target == DataType::Int64);
        case DataType::Int16:
            return (target == DataType::Int32 || target == DataType::Int64);
        case DataType::Int32:
            return (target == DataType::Int64);
        case DataType::Float:
            return (target == DataType::Double);
        default:
            return false;
    }
}

SchemaValidationResult SchemaValidator::ValidateCompatibility(
    const Schema& base_schema,
    const Schema& incoming_schema,
    bool allow_additive_evolution) {

    SchemaValidationResult result;
    std::unordered_map<std::string, const Field*> base_field_map;
    for (const auto& f : base_schema.fields()) {
        base_field_map[f.name()] = &f;
    }

    std::unordered_set<std::string> incoming_field_names;
    std::vector<Field> evolved_fields = base_schema.fields();

    for (const auto& inc_field : incoming_schema.fields()) {
        incoming_field_names.insert(inc_field.name());
        auto it = base_field_map.find(inc_field.name());

        if (it == base_field_map.end()) {
            // New column detected in incoming schema
            if (!allow_additive_evolution) {
                result.is_valid = false;
                result.drift_errors.push_back(
                    "Unexpected field in incoming schema: '" + inc_field.name() + "' (schema evolution disabled)");
            } else {
                result.requires_evolution = true;
                result.warnings.push_back(
                    "Additive schema evolution: new column '" + inc_field.name() + "' of type " +
                    DataTypeToString(inc_field.type()) + " added.");
                evolved_fields.push_back(inc_field);
            }
            continue;
        }

        const auto* base_field = it->second;

        // Check type compatibility
        if (base_field->type() != inc_field.type()) {
            if (CanPromote(base_field->type(), inc_field.type())) {
                // Base column needs promotion to incoming widened type
                result.requires_evolution = true;
                result.warnings.push_back(
                    "Type widening promotion for column '" + base_field->name() + "': " +
                    DataTypeToString(base_field->type()) + " -> " + DataTypeToString(inc_field.type()));

                // Update evolved field type
                for (auto& ef : evolved_fields) {
                    if (ef.name() == base_field->name()) {
                        ef = Field(ef.name(), inc_field.type(), ef.nullable());
                        break;
                    }
                }
            } else if (CanPromote(inc_field.type(), base_field->type())) {
                // Incoming data is narrower, base type accommodates it safely
                result.warnings.push_back(
                    "Incoming column '" + inc_field.name() + "' type " +
                    DataTypeToString(inc_field.type()) + " is safely promoted to table type " +
                    DataTypeToString(base_field->type()));
            } else {
                result.is_valid = false;
                result.drift_errors.push_back(
                    "Incompatible type drift in column '" + base_field->name() + "': expected " +
                    DataTypeToString(base_field->type()) + ", got " + DataTypeToString(inc_field.type()));
            }
        }

        // Check nullability conflict: incoming column nullable while base is required non-null
        if (!base_field->nullable() && inc_field.nullable()) {
            result.is_valid = false;
            result.drift_errors.push_back(
                "Nullability mismatch in column '" + base_field->name() + "': table column is non-nullable, but incoming is nullable.");
        }
    }

    // Check for missing required fields in incoming schema
    for (const auto& base_f : base_schema.fields()) {
        if (!base_f.nullable() && incoming_field_names.find(base_f.name()) == incoming_field_names.end()) {
            result.is_valid = false;
            result.drift_errors.push_back(
                "Missing required non-nullable column in incoming schema: '" + base_f.name() + "'");
        }
    }

    result.evolved_schema = Schema(std::move(evolved_fields));
    return result;
}

StatusOr<Schema> SchemaValidator::MergeSchemas(
    const Schema& base_schema,
    const Schema& incoming_schema) {

    auto res = ValidateCompatibility(base_schema, incoming_schema, true);
    if (!res.is_valid) {
        std::string err_msg = "Schema merge failed due to drift errors: ";
        for (size_t i = 0; i < res.drift_errors.size(); ++i) {
            if (i > 0) err_msg += "; ";
            err_msg += res.drift_errors[i];
        }
        return Status(StatusCode::InvalidArgument, err_msg);
    }

    return res.evolved_schema;
}

} // namespace schema
} // namespace storage
} // namespace obsidian
