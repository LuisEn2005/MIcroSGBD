#ifndef MINI_DBMS_SEQ_SCAN_OPERATOR_H
#define MINI_DBMS_SEQ_SCAN_OPERATOR_H

#include "abstract_operator.h"
#include "../../storage/heap_file.h"

namespace minidbms {
    class SeqScanOperator : public AbstractOperator {
        public:
            SeqScanOperator(HeapFile* heap_file) : heap_file_(heap_file) {}

            Status Open() override;
            bool Next(Record* record, RecordID* rid) override;
            Status Close() override;

        private:
            HeapFile* heap_file_;
            PageId current_page_id_{INVALID_PAGE_ID};
            SlotId current_slot_id_{0};
    };

}

#endif // MINI_DBMS_SEQ_SCAN_OPERATOR_H
