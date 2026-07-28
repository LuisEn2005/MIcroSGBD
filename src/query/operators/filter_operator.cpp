#include "query/operators/filter_operator.h"

#include "storage/record_codec.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>

namespace minidbms {
namespace {

bool CompareIntegers(int32_t left, int32_t right, const std::string& op) {
    if (op == "=") return left == right;
    if (op == ">") return left > right;
    if (op == "<") return left < right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    return false;
}

bool CompareStrings(
    const std::string& left,
    const std::string& right,
    const std::string& op
) {
    if (op == "=") return left == right;
    if (op == ">") return left > right;
    if (op == "<") return left < right;
    if (op == ">=") return left >= right;
    if (op == "<=") return left <= right;
    return false;
}

} // namespace

FilterOperator::FilterOperator(
    std::unique_ptr<AbstractOperator> child,
    Schema schema,
    Condition condition
)
    : child_(std::move(child)),
      schema_(std::move(schema)),
      condition_(std::move(condition)) {}

Status FilterOperator::Open() {
    if (!child_) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Filter requires a child operator"
        );
    }

    const auto& columns = schema_.GetColumns();
    for (uint32_t index = 0; index < columns.size(); ++index) {
        if (columns[index].name == condition_.column) {
            column_index_ = index;
            column_found_ = true;
            break;
        }
    }

    if (!column_found_) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Filter column does not exist: " + condition_.column
        );
    }

    filter_status_ = ParseTargetValue();
    if (!filter_status_.ok()) {
        return filter_status_;
    }

    return child_->Open();
}

Status FilterOperator::ParseTargetValue() {
    const Column& column = schema_.GetColumns()[column_index_];

    try {
        switch (column.type) {
            case TypeId::INTEGER: {
                std::size_t consumed = 0;
                const long long parsed = std::stoll(condition_.value, &consumed);
                if (consumed != condition_.value.size() ||
                    parsed < std::numeric_limits<int32_t>::min() ||
                    parsed > std::numeric_limits<int32_t>::max()) {
                    return Status(
                        StatusCode::INVALID_ARGUMENT,
                        "Invalid INTEGER filter value"
                    );
                }
                target_value_ = static_cast<int32_t>(parsed);
                return Status::OK();
            }

            case TypeId::BOOLEAN: {
                std::string normalized = condition_.value;
                std::transform(
                    normalized.begin(),
                    normalized.end(),
                    normalized.begin(),
                    [](unsigned char character) {
                        return static_cast<char>(std::tolower(character));
                    }
                );

                if (normalized == "true" || normalized == "1") {
                    target_value_ = true;
                    return Status::OK();
                }
                if (normalized == "false" || normalized == "0") {
                    target_value_ = false;
                    return Status::OK();
                }

                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Invalid BOOLEAN filter value"
                );
            }

            case TypeId::VARCHAR:
                target_value_ = condition_.value;
                return Status::OK();
        }
    } catch (const std::exception&) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid filter value"
        );
    }

    return Status(
        StatusCode::INVALID_ARGUMENT,
        "Unsupported filter type"
    );
}

bool FilterOperator::Evaluate(const Record& record) {
    FieldValue actual_value;
    filter_status_ = RecordCodec::GetValue(
        schema_,
        record,
        column_index_,
        &actual_value
    );

    if (!filter_status_.ok()) {
        return false;
    }

    if (std::holds_alternative<std::monostate>(actual_value)) {
        return false;
    }

    if (const auto* left = std::get_if<int32_t>(&actual_value)) {
        const auto* right = std::get_if<int32_t>(&target_value_);
        return right != nullptr && CompareIntegers(*left, *right, condition_.op);
    }

    if (const auto* left = std::get_if<std::string>(&actual_value)) {
        const auto* right = std::get_if<std::string>(&target_value_);
        return right != nullptr && CompareStrings(*left, *right, condition_.op);
    }

    if (const auto* left = std::get_if<bool>(&actual_value)) {
        const auto* right = std::get_if<bool>(&target_value_);
        if (right == nullptr || condition_.op != "=") {
            return false;
        }
        return *left == *right;
    }

    return false;
}

bool FilterOperator::Next(Record* record, RecordID* rid) {
    if (!child_ || !filter_status_.ok()) {
        return false;
    }

    while (child_->Next(record, rid)) {
        if (Evaluate(*record)) {
            return true;
        }
        if (!filter_status_.ok()) {
            return false;
        }
    }

    return false;
}

Status FilterOperator::Close() {
    Status child_status = child_ ? child_->Close() : Status::OK();
    if (!filter_status_.ok()) {
        return filter_status_;
    }
    return child_status;
}

} // namespace minidbms
