#include "query/operators/filter_operator.h"

#include "query/literal.h"
#include "storage/record_codec.h"

#include <string>

namespace minidbms {
namespace {

bool CompareIntegers(
    int32_t left,
    int32_t right,
    const std::string& op
) {
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

    column_found_ = false;
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
    return ConvertLiteral(
        schema_.GetColumns()[column_index_],
        condition_.value,
        false,
        &target_value_
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
        return right != nullptr &&
               CompareIntegers(*left, *right, condition_.op);
    }

    if (const auto* left = std::get_if<std::string>(&actual_value)) {
        const auto* right = std::get_if<std::string>(&target_value_);
        return right != nullptr &&
               CompareStrings(*left, *right, condition_.op);
    }

    if (const auto* left = std::get_if<bool>(&actual_value)) {
        const auto* right = std::get_if<bool>(&target_value_);
        return right != nullptr &&
               condition_.op == "=" &&
               *left == *right;
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
