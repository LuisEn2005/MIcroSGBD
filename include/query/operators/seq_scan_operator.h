#ifndef MINI_DBMS_SEQ_SCAN_OPERATOR_H
#define MINI_DBMS_SEQ_SCAN_OPERATOR_H

#include "query/operators/abstract_operator.h"
#include "metrics/query_stats.h"
#include "storage/heap_file.h"

#include <memory>

namespace minidbms {

class SeqScanOperator : public AbstractOperator {
public:
    SeqScanOperator(
        std::unique_ptr<HeapFile> heap_file,
        QueryStats* stats = nullptr
    );

    Status Open() override;
    bool Next(Record* record, RecordID* rid) override;
    Status Close() override;

private:
    std::unique_ptr<HeapFile> heap_file_;
    QueryStats* stats_{nullptr};

    bool started_{false};
    bool finished_{false};
    PageId last_page_id_{INVALID_PAGE_ID};
    RecordID current_rid_{};
    Status scan_status_{};
};

} // namespace minidbms

#endif // MINI_DBMS_SEQ_SCAN_OPERATOR_H
