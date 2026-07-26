#ifndef MINI_DBMS_INDEX_SCAN_OPERATOR_H
#define MINI_DBMS_INDEX_SCAN_OPERATOR_H

#include "../../query/operators/abstract_operator.h"
#include "../../index/hash_index.h"
#include "../../storage/heap_file.h"
#include <vector>

namespace minidbms {
    class IndexScanOperator : public AbstractOperator {
        public:
            IndexScanOperator(HashIndex* index, HeapFile* heap_file, std::string key)
                : index_(index), heap_file_(heap_file), key_(std::move(key)) {}

            Status Open() override;
            bool Next(Record* record, RecordID* rid) override;
            Status Close() override;

        private:
            HashIndex* index_;
            HeapFile* heap_file_;
            std::string key_;
            std::vector<RecordID> matching_rids_;
            size_t cursor_{0};
    };

}

#endif // MINI_DBMS_INDEX_SCAN_OPERATOR_H
