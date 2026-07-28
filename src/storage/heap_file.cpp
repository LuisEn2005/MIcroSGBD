#include "../../include/storage/heap_file.h"

#include "../../include/common/config.h"
#include "../../include/storage/slotted_page.h"

#include <stdexcept>
#include <unordered_set>

namespace minidbms {

HeapFile::HeapFile(
    BufferPoolManager* buffer_pool_manager,
    PageId first_page_id
)
    : bpm_(buffer_pool_manager),
      first_page_id_(first_page_id) {
    if (bpm_ == nullptr) {
        throw std::invalid_argument(
            "BufferPoolManager cannot be null"
        );
    }

    if (first_page_id_ <= HEADER_PAGE_ID) {
        throw std::invalid_argument(
            "HeapFile requires a valid data page as first page"
        );
    }
}

Status HeapFile::InsertRecord(
    const Record& record,
    RecordID* rid
) {
    if (rid == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "RecordID output cannot be null"
        );
    }

    if (record.GetData() == nullptr || record.GetSize() == 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record cannot be empty"
        );
    }

    std::unordered_set<PageId> visited_pages;
    PageId current_page_id = first_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        if (!visited_pages.insert(current_page_id).second) {
            return Status::IOError(
                "Cycle detected in HeapFile page chain"
            );
        }

        Page* page = bpm_->FetchPage(current_page_id);

        if (page == nullptr) {
            return Status::IOError(
                "Could not fetch HeapFile page"
            );
        }

        SlottedPage slotted_page(page);

        if (!slotted_page.IsInitialized()) {
            Status init_status = slotted_page.Init();
            if (!init_status.ok()) {
                bpm_->UnpinPage(current_page_id, false);
                return init_status;
            }
        }

        SlotId slot_id = 0;
        const Status insert_status = slotted_page.InsertRecord(
            record.GetData(),
            record.GetSize(),
            &slot_id
        );

        if (insert_status.ok()) {
            const bool unpinned = bpm_->UnpinPage(
                current_page_id,
                true
            );

            if (!unpinned) {
                return Status::IOError(
                    "Could not unpin modified HeapFile page"
                );
            }

            *rid = {current_page_id, slot_id};
            return Status::OK();
        }

        const PageId next_page_id =
            slotted_page.GetNextPageId();

        const bool unpinned = bpm_->UnpinPage(
            current_page_id,
            false
        );

        if (!unpinned) {
            return Status::IOError(
                "Could not unpin HeapFile page"
            );
        }

        if (insert_status.code() != StatusCode::OUT_OF_MEMORY) {
            return insert_status;
        }

        if (next_page_id != INVALID_PAGE_ID) {
            current_page_id = next_page_id;
            continue;
        }

        return AppendNewPage(
            current_page_id,
            record,
            rid
        );
    }

    return Status::IOError(
        "HeapFile page chain ended unexpectedly"
    );
}

Status HeapFile::AppendNewPage(
    PageId previous_page_id,
    const Record& record,
    RecordID* rid
) {
    PageId new_page_id = INVALID_PAGE_ID;
    Page* new_page = bpm_->NewPage(&new_page_id);

    if (new_page == nullptr) {
        return Status::OutOfMemory(
            "No buffer frame available for a new HeapFile page"
        );
    }

    SlottedPage new_slotted_page(new_page);

    Status status = new_slotted_page.Init();

    if (!status.ok()) {
        bpm_->UnpinPage(new_page_id, false);
        bpm_->DeletePage(new_page_id);
        return status;
    }

    SlotId new_slot_id = 0;
    status = new_slotted_page.InsertRecord(
        record.GetData(),
        record.GetSize(),
        &new_slot_id
    );

    if (!status.ok()) {
        bpm_->UnpinPage(new_page_id, false);
        bpm_->DeletePage(new_page_id);
        return status;
    }

    if (!bpm_->UnpinPage(new_page_id, true)) {
        return Status::IOError(
            "Could not unpin new HeapFile page"
        );
    }

    Page* previous_page = bpm_->FetchPage(previous_page_id);

    if (previous_page == nullptr) {
        bpm_->DeletePage(new_page_id);

        return Status::IOError(
            "Could not refetch previous HeapFile page"
        );
    }

    SlottedPage previous_slotted_page(previous_page);

    if (!previous_slotted_page.IsInitialized()) {
        bpm_->UnpinPage(previous_page_id, false);
        bpm_->DeletePage(new_page_id);

        return Status::IOError(
            "Previous HeapFile page is not initialized"
        );
    }

    status = previous_slotted_page.SetNextPageId(new_page_id);

    const bool previous_unpinned = bpm_->UnpinPage(
        previous_page_id,
        status.ok()
    );

    if (!previous_unpinned) {
        return Status::IOError(
            "Could not unpin previous HeapFile page"
        );
    }

    if (!status.ok()) {
        bpm_->DeletePage(new_page_id);
        return status;
    }

    *rid = {new_page_id, new_slot_id};
    return Status::OK();
}

Status HeapFile::GetRecord(
    RecordID rid,
    Record* record
) {
    if (record == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record output cannot be null"
        );
    }

    if (rid.page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "RecordID contains an invalid page id"
        );
    }

    Page* page = bpm_->FetchPage(rid.page_id);

    if (page == nullptr) {
        return Status::NotFound(
            "Record page could not be loaded"
        );
    }

    SlottedPage slotted_page(page);
    const Status status = slotted_page.ReadRecord(
        rid.slot_id,
        record
    );

    if (!bpm_->UnpinPage(rid.page_id, false)) {
        return Status::IOError(
            "Could not unpin record page"
        );
    }

    return status;
}

Status HeapFile::UpdateRecord(const Record& record) {
    const RecordID rid = record.GetRecordID();

    if (rid.page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record has an invalid RecordID"
        );
    }

    if (record.GetData() == nullptr || record.GetSize() == 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Updated record cannot be empty"
        );
    }

    Page* page = bpm_->FetchPage(rid.page_id);

    if (page == nullptr) {
        return Status::NotFound(
            "Record page could not be loaded"
        );
    }

    SlottedPage slotted_page(page);
    const Status status = slotted_page.UpdateRecord(
        rid.slot_id,
        record.GetData(),
        record.GetSize()
    );

    if (!bpm_->UnpinPage(rid.page_id, status.ok())) {
        return Status::IOError(
            "Could not unpin updated record page"
        );
    }

    return status;
}

Status HeapFile::DeleteRecord(RecordID rid) {
    if (rid.page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "RecordID contains an invalid page id"
        );
    }

    Page* page = bpm_->FetchPage(rid.page_id);

    if (page == nullptr) {
        return Status::NotFound(
            "Record page could not be loaded"
        );
    }

    SlottedPage slotted_page(page);
    const Status status = slotted_page.DeleteRecord(
        rid.slot_id
    );

    if (!bpm_->UnpinPage(rid.page_id, status.ok())) {
        return Status::IOError(
            "Could not unpin deleted record page"
        );
    }

    return status;
}

Status HeapFile::ContainsPage(
    PageId page_id,
    bool* contains
) {
    if (contains == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "ContainsPage output cannot be null"
        );
    }

    *contains = false;

    if (page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page id must refer to a data page"
        );
    }

    std::unordered_set<PageId> visited_pages;
    PageId current_page_id = first_page_id_;

    while (current_page_id != INVALID_PAGE_ID) {
        if (!visited_pages.insert(current_page_id).second) {
            return Status::IOError(
                "Cycle detected in HeapFile page chain"
            );
        }

        if (current_page_id == page_id) {
            *contains = true;
            return Status::OK();
        }

        Page* page = bpm_->FetchPage(current_page_id);
        if (page == nullptr) {
            return Status::IOError(
                "Could not fetch HeapFile page while validating ownership"
            );
        }

        SlottedPage slotted_page(page);
        if (!slotted_page.IsInitialized()) {
            bpm_->UnpinPage(current_page_id, false);
            return Status::IOError(
                "HeapFile contains an uninitialized page"
            );
        }

        const PageId next_page_id =
            slotted_page.GetNextPageId();

        if (!bpm_->UnpinPage(current_page_id, false)) {
            return Status::IOError(
                "Could not unpin HeapFile page while validating ownership"
            );
        }

        current_page_id = next_page_id;
    }

    return Status::OK();
}

Status HeapFile::GetFirstRecord(
    Record* record,
    RecordID* rid
) {
    return FindRecordFrom(
        first_page_id_,
        0,
        record,
        rid
    );
}

Status HeapFile::GetNextRecord(
    RecordID current_rid,
    Record* record,
    RecordID* next_rid
) {
    if (current_rid.page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Current RecordID contains an invalid page id"
        );
    }

    return FindRecordFrom(
        current_rid.page_id,
        static_cast<uint32_t>(current_rid.slot_id) + 1,
        record,
        next_rid
    );
}

Status HeapFile::FindRecordFrom(
    PageId page_id,
    uint32_t first_slot,
    Record* record,
    RecordID* rid
) {
    if (record == nullptr || rid == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record and RecordID outputs cannot be null"
        );
    }

    std::unordered_set<PageId> visited_pages;
    PageId current_page_id = page_id;
    uint32_t current_first_slot = first_slot;

    while (current_page_id != INVALID_PAGE_ID) {
        if (!visited_pages.insert(current_page_id).second) {
            return Status::IOError(
                "Cycle detected in HeapFile page chain"
            );
        }

        Page* page = bpm_->FetchPage(current_page_id);

        if (page == nullptr) {
            return Status::IOError(
                "Could not fetch HeapFile page while scanning"
            );
        }

        SlottedPage slotted_page(page);

        if (!slotted_page.IsInitialized()) {
            bpm_->UnpinPage(current_page_id, false);

            return Status::IOError(
                "HeapFile contains an uninitialized page"
            );
        }

        const uint16_t slot_count =
            slotted_page.GetSlotCount();
        const PageId next_page_id =
            slotted_page.GetNextPageId();

        for (uint32_t slot = current_first_slot;
             slot < slot_count;
             ++slot) {
            Record candidate;
            const Status read_status = slotted_page.ReadRecord(
                static_cast<SlotId>(slot),
                &candidate
            );

            if (read_status.ok()) {
                if (!bpm_->UnpinPage(current_page_id, false)) {
                    return Status::IOError(
                        "Could not unpin scanned HeapFile page"
                    );
                }

                *record = candidate;
                *rid = candidate.GetRecordID();
                return Status::OK();
            }

            if (read_status.code() != StatusCode::NOT_FOUND) {
                bpm_->UnpinPage(current_page_id, false);
                return read_status;
            }
        }

        if (!bpm_->UnpinPage(current_page_id, false)) {
            return Status::IOError(
                "Could not unpin scanned HeapFile page"
            );
        }

        current_page_id = next_page_id;
        current_first_slot = 0;
    }

    return Status::NotFound(
        "No more records in HeapFile"
    );
}

} // namespace minidbms