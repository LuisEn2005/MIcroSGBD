#ifndef MINI_DBMS_INDEX_SCAN_OPERATOR_H
#define MINI_DBMS_INDEX_SCAN_OPERATOR_H

#include <vector>
#include <string>
#include "../operators/abstract_operator.h"
#include "../../index/hash_index.h"
#include "../../storage/heap_file.h"

namespace minidbms {
    class IndexScanOperator : public AbstractOperator {
        public:
            IndexScanOperator(HashIndex* index, HeapFile* heap_file, std::string target_key)
                : index_(index), heap_file_(heap_file), target_key_(std::move(target_key)), current_idx_(0) {}

            Status Open() override {
                current_idx_ = 0;
                matching_rids_.clear();

                if (index_ == nullptr || heap_file_ == nullptr) {
                    return Status(StatusCode::INVALID_ARGUMENT, "Indice o HeapFile invalido en IndexScanOperator");
                }

                return index_->GetValue(target_key_, &matching_rids_);
            }

            bool Next(Record* record, RecordID* rid) override {
                if (current_idx_ >= matching_rids_.size()) {
                    return false;
                }

                *rid = matching_rids_[current_idx_];
                current_idx_++;

                Status s = heap_file_->GetRecord(*rid, record);
                return s.ok();
            }

            Status Close() override {
                matching_rids_.clear();
                current_idx_ = 0;
                return Status::OK();
            }

        private:
            HashIndex* index_;
            HeapFile* heap_file_;
            std::string target_key_;
            std::vector<RecordID> matching_rids_;
            size_t current_idx_;
    };

} // namespace minidbms

#endif // MINI_DBMS_INDEX_SCAN_OPERATOR_H
