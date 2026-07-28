#ifndef MINI_DBMS_HEAP_FILE_H
#define MINI_DBMS_HEAP_FILE_H

#include "buffer/buffer_pool_manager.h"
#include "common/status.h"
#include "common/types.h"
#include "storage/record.h"

#include <cstdint>

namespace minidbms {

class HeapFile {
public:
    HeapFile(BufferPoolManager* buffer_pool_manager, PageId first_page_id);

    Status InsertRecord(const Record& record, RecordID* rid);
    Status GetRecord(RecordID rid, Record* record);
    Status UpdateRecord(const Record& record);
    Status DeleteRecord(RecordID rid);

    Status GetFirstRecord(Record* record, RecordID* rid);
    Status GetNextRecord(
        RecordID current_rid,
        Record* record,
        RecordID* next_rid
    );

    PageId GetFirstPageId() const {
        return first_page_id_;
    }

private:
    Status FindRecordFrom(
        PageId page_id,
        uint32_t first_slot,
        Record* record,
        RecordID* rid
    );

    Status AppendNewPage(
        PageId previous_page_id,
        const Record& record,
        RecordID* rid
    );

    BufferPoolManager* bpm_;
    PageId first_page_id_;
};

} // namespace minidbms

#endif // MINI_DBMS_HEAP_FILE_H
