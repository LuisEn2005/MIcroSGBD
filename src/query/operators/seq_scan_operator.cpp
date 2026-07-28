#include "query/operators/seq_scan_operator.h"

namespace minidbms {

SeqScanOperator::SeqScanOperator(
    std::unique_ptr<HeapFile> heap_file,
    QueryStats* stats
)
    : heap_file_(std::move(heap_file)),
      stats_(stats) {}

Status SeqScanOperator::Open() {
    if (!heap_file_) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "SeqScan requires a HeapFile"
        );
    }

    started_ = false;
    finished_ = false;
    last_page_id_ = INVALID_PAGE_ID;
    current_rid_ = {};
    scan_status_ = Status::OK();
    return Status::OK();
}

bool SeqScanOperator::Next(Record* record, RecordID* rid) {
    if (finished_ || !heap_file_) {
        return false;
    }

    if (record == nullptr || rid == nullptr) {
        scan_status_ = Status(
            StatusCode::INVALID_ARGUMENT,
            "SeqScan outputs cannot be null"
        );
        finished_ = true;
        return false;
    }

    RecordID found_rid{};
    Status status = started_
        ? heap_file_->GetNextRecord(current_rid_, record, &found_rid)
        : heap_file_->GetFirstRecord(record, &found_rid);

    started_ = true;

    if (status.code() == StatusCode::NOT_FOUND) {
        finished_ = true;
        return false;
    }

    if (!status.ok()) {
        scan_status_ = status;
        finished_ = true;
        return false;
    }

    current_rid_ = found_rid;
    *rid = found_rid;

    if (stats_ != nullptr) {
        ++stats_->records_examined;
        if (found_rid.page_id != last_page_id_) {
            ++stats_->pages_scanned;
            last_page_id_ = found_rid.page_id;
        }
    }

    return true;
}

Status SeqScanOperator::Close() {
    finished_ = true;
    return scan_status_;
}

} // namespace minidbms
