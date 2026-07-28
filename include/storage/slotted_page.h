#ifndef MINI_DBMS_SLOTTED_PAGE_H
#define MINI_DBMS_SLOTTED_PAGE_H

#include "common/status.h"
#include "common/types.h"
#include "storage/page.h"
#include "storage/record.h"

#include <cstdint>

namespace minidbms {

class SlottedPage {
public:
    explicit SlottedPage(Page* page) : page_(page) {}

    Status Init();

    bool IsInitialized() const;

    Status InsertRecord(
        const char* data,
        uint32_t size,
        SlotId* slot_id
    );

    Status ReadRecord(
        SlotId slot_id,
        Record* record
    ) const;

    Status UpdateRecord(
        SlotId slot_id,
        const char* data,
        uint32_t size
    );

    Status DeleteRecord(SlotId slot_id);

    uint16_t GetFreeSpace() const;
    uint16_t GetSlotCount() const;

    PageId GetNextPageId() const;
    Status SetNextPageId(PageId next_page_id);

private:
    Page* page_;
};

} // namespace minidbms

#endif // MINI_DBMS_SLOTTED_PAGE_H
