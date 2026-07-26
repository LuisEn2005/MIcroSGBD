#ifndef MINI_DBMS_SLOTTED_PAGE_H
#define MINI_DBMS_SLOTTED_PAGE_H

#include "page.h"
#include "record.h"
#include "../common/status.h"

namespace minidbms {

class SlottedPage {
public:
    explicit SlottedPage(Page* page)
        : page_(page) {}

    void Init();

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

private:
    Page* page_;
};

} // namespace minidbms

#endif // MINI_DBMS_SLOTTED_PAGE_H
