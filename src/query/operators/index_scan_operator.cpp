#include "query/operators/index_scan_operator.h"

namespace minidbms {

IndexScanOperator::IndexScanOperator(
    HashIndex* index,
    std::unique_ptr<HeapFile> heap_file,
    std::string encoded_key,
    QueryStats* stats
)
    : index_(index),
      heap_file_(std::move(heap_file)),
      encoded_key_(std::move(encoded_key)),
      stats_(stats) {}

Status IndexScanOperator::Open() {
    if (index_ == nullptr || !heap_file_) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "IndexScan requires a HashIndex and HeapFile"
        );
    }

    matching_rids_.clear();
    cursor_ = 0;
    last_page_id_ = INVALID_PAGE_ID;
    scan_status_ = index_->GetValue(
        encoded_key_,
        &matching_rids_
    );

    return scan_status_;
}

bool IndexScanOperator::Next(
    Record* record,
    RecordID* rid
) {
    if (!scan_status_.ok() ||
        record == nullptr ||
        rid == nullptr) {
        return false;
    }

    while (cursor_ < matching_rids_.size()) {
        const RecordID candidate =
            matching_rids_[cursor_++];

        Status status = heap_file_->GetRecord(
            candidate,
            record
        );

        if (status.code() == StatusCode::NOT_FOUND) {
            // Un RID obsoleto no debe detener todo el escaneo.
            continue;
        }

        if (!status.ok()) {
            scan_status_ = status;
            return false;
        }

        *rid = candidate;

        if (stats_ != nullptr) {
            ++stats_->records_examined;

            if (candidate.page_id != last_page_id_) {
                ++stats_->pages_scanned;
                last_page_id_ = candidate.page_id;
            }
        }

        return true;
    }

    return false;
}

Status IndexScanOperator::Close() {
    matching_rids_.clear();
    cursor_ = 0;
    return scan_status_;
}

} // namespace minidbms
