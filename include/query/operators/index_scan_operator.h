#ifndef MINI_DBMS_INDEX_SCAN_OPERATOR_H
#define MINI_DBMS_INDEX_SCAN_OPERATOR_H

#include "index/hash_index.h"
#include "metrics/query_stats.h"
#include "query/operators/abstract_operator.h"
#include "storage/heap_file.h"

#include <memory>
#include <string>
#include <vector>

namespace minidbms {

class IndexScanOperator : public AbstractOperator {
public:
    IndexScanOperator(
        HashIndex* index,
        std::unique_ptr<HeapFile> heap_file,
        std::string encoded_key,
        QueryStats* stats = nullptr
    );

    Status Open() override;
    bool Next(Record* record, RecordID* rid) override;
    Status Close() override;

private:
    HashIndex* index_{nullptr};
    std::unique_ptr<HeapFile> heap_file_;
    std::string encoded_key_;
    QueryStats* stats_{nullptr};

    std::vector<RecordID> matching_rids_;
    std::size_t cursor_{0};
    PageId last_page_id_{INVALID_PAGE_ID};
    Status scan_status_{};
};

} // namespace minidbms

#endif // MINI_DBMS_INDEX_SCAN_OPERATOR_H
