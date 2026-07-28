#ifndef MINI_DBMS_SEQ_SCAN_OPERATOR_H
#define MINI_DBMS_SEQ_SCAN_OPERATOR_H

#include "abstract_operator.h"
#include "../../storage/heap_file.h"

namespace minidbms {
    class SeqScanOperator : public AbstractOperator {
        public:
            explicit SeqScanOperator(HeapFile* heap_file) 
                : heap_file_(heap_file), current_page_id_(INVALID_PAGE_ID), current_slot_id_(0) {}

            Status Open() override {
                if (heap_file_ != nullptr) {
                    current_page_id_ = heap_file_->GetFirstPageId();
                } else {
                    current_page_id_ = INVALID_PAGE_ID;
                }
                current_slot_id_ = 0;
                return Status::OK();
            }

            bool Next(Record* record, RecordID* rid) override {
                if (heap_file_ == nullptr || current_page_id_ == INVALID_PAGE_ID) {
                    return false;
                }

                RecordID current_rid{current_page_id_, current_slot_id_};
                Status status = heap_file_->GetRecord(current_rid, record);

                if (status.ok()) {
                    *rid = current_rid;
                    current_slot_id_++;
                    return true;
                }

                current_slot_id_++;
                return false;
            }

            Status Close() override {
                current_page_id_ = INVALID_PAGE_ID;
                current_slot_id_ = 0;
                return Status::OK();
            }

        private:
            HeapFile* heap_file_;
            PageId current_page_id_;
            SlotId current_slot_id_;
    };

} // namespace minidbms

#endif // MINI_DBMS_SEQ_SCAN_OPERATOR_H
