#include "query/operators/projection_operator.h"

#include "storage/record_codec.h"

namespace minidbms {

ProjectionOperator::ProjectionOperator(
    std::unique_ptr<AbstractOperator> child,
    Schema input_schema,
    std::vector<uint32_t> selected_fields
)
    : child_(std::move(child)),
      input_schema_(std::move(input_schema)),
      selected_fields_(std::move(selected_fields)) {}

Status ProjectionOperator::Open() {
    if (!child_) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Projection requires a child operator"
        );
    }

    if (selected_fields_.empty()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Projection requires at least one column"
        );
    }

    const auto column_count = input_schema_.GetColumnCount();
    for (uint32_t index : selected_fields_) {
        if (index >= column_count) {
            return Status(
                StatusCode::INVALID_ARGUMENT,
                "Projection column index is out of range"
            );
        }
    }

    projection_status_ = Status::OK();
    return child_->Open();
}

bool ProjectionOperator::Next(Record* record, RecordID* rid) {
    if (!child_ || !projection_status_.ok() || record == nullptr || rid == nullptr) {
        return false;
    }

    Record input_record;
    RecordID input_rid;

    if (!child_->Next(&input_record, &input_rid)) {
        return false;
    }

    projection_status_ = RecordCodec::Project(
        input_schema_,
        input_record,
        selected_fields_,
        record
    );

    if (!projection_status_.ok()) {
        return false;
    }

    *rid = input_rid;
    record->SetRecordID(input_rid);
    return true;
}

Status ProjectionOperator::Close() {
    Status child_status = child_ ? child_->Close() : Status::OK();
    if (!projection_status_.ok()) {
        return projection_status_;
    }
    return child_status;
}

} // namespace minidbms
