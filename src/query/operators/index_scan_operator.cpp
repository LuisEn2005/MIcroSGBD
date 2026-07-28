#include "query/operators/index_scan_operator.h"

namespace minidbms {

Status IndexScanOperator::Open() {
    if (index_ == nullptr || heap_file_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "IndexScanOperator requires valid HashIndex and HeapFile pointers"
        );
    }

    matching_rids_.clear();
    cursor_ = 0;

    Status status = index_->GetValue(key_, &matching_rids_);
    if (!status.ok()) {
        return status;
    }

    return Status::OK();
}

bool IndexScanOperator::Next(Record* record, RecordID* rid) {
    if (record == nullptr || rid == nullptr) {
        return false;
    }

    while (cursor_ < matching_rids_.size()) {
        const RecordID candidate_rid = matching_rids_[cursor_++];
        Status status = heap_file_->GetRecord(candidate_rid, record);
        if (status.ok()) {
            *rid = candidate_rid;
            return true;
        }
    }

    return false;
}

Status IndexScanOperator::Close() {
    matching_rids_.clear();
    cursor_ = 0;
    return Status::OK();
}

} // namespace minidbms
