#include "storage/slotted_page.h"

#include <cstring>
#include <limits>
#include <vector>

namespace minidbms {

namespace {

constexpr std::size_t PAGE_ID_OFFSET = 0;
constexpr std::size_t SLOT_COUNT_OFFSET = 4;
constexpr std::size_t FREE_SPACE_POINTER_OFFSET = 6;
constexpr std::size_t NEXT_PAGE_ID_OFFSET = 8;
constexpr std::size_t LSN_OFFSET = 12;

constexpr std::size_t PAGE_HEADER_SIZE = 16;
constexpr std::size_t SLOT_ENTRY_SIZE = 4;

template <typename T>
T ReadValue(const char* data, std::size_t offset) {
    T value{};
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

template <typename T>
void WriteValue(char* data, std::size_t offset, const T& value) {
    std::memcpy(data + offset, &value, sizeof(T));
}

std::size_t GetSlotOffset(SlotId slot_id) {
    return PAGE_HEADER_SIZE +
           static_cast<std::size_t>(slot_id) * SLOT_ENTRY_SIZE;
}

uint16_t ReadSlotRecordOffset(const char* page_data, SlotId slot_id) {
    return ReadValue<uint16_t>(page_data, GetSlotOffset(slot_id));
}

uint16_t ReadSlotRecordLength(const char* page_data, SlotId slot_id) {
    return ReadValue<uint16_t>(
        page_data,
        GetSlotOffset(slot_id) + sizeof(uint16_t)
    );
}

void WriteSlot(
    char* page_data,
    SlotId slot_id,
    uint16_t record_offset,
    uint16_t record_length
) {
    const std::size_t slot_offset = GetSlotOffset(slot_id);

    WriteValue<uint16_t>(page_data, slot_offset, record_offset);
    WriteValue<uint16_t>(
        page_data,
        slot_offset + sizeof(uint16_t),
        record_length
    );
}

Status LoadRecords(
    const char* page_data,
    uint16_t slot_count,
    std::vector<std::vector<char>>* records,
    std::vector<bool>* active_slots
) {
    const std::size_t directory_end =
        PAGE_HEADER_SIZE +
        static_cast<std::size_t>(slot_count) * SLOT_ENTRY_SIZE;

    if (directory_end > PAGE_SIZE) {
        return Status::IOError(
            "Corrupted slot directory"
        );
    }

    records->assign(slot_count, {});
    active_slots->assign(slot_count, false);

    for (SlotId slot_id = 0;
         slot_id < slot_count;
         ++slot_id) {
        const uint16_t record_offset =
            ReadSlotRecordOffset(page_data, slot_id);
        const uint16_t record_length =
            ReadSlotRecordLength(page_data, slot_id);

        if (record_offset == 0 && record_length == 0) {
            continue;
        }

        if (record_offset == 0 || record_length == 0) {
            return Status::IOError(
                "Corrupted slot entry"
            );
        }

        const std::size_t record_end =
            static_cast<std::size_t>(record_offset) +
            record_length;

        if (record_offset < directory_end || record_end > PAGE_SIZE) {
            return Status::IOError(
                "Corrupted record bounds"
            );
        }

        (*active_slots)[slot_id] = true;
        (*records)[slot_id].assign(
            page_data + record_offset,
            page_data + record_end
        );
    }

    return Status::OK();
}

Status RewriteRecords(
    Page* page,
    uint16_t slot_count,
    const std::vector<std::vector<char>>& records,
    const std::vector<bool>& active_slots
) {
    const std::size_t directory_end =
        PAGE_HEADER_SIZE +
        static_cast<std::size_t>(slot_count) * SLOT_ENTRY_SIZE;

    std::size_t total_record_bytes = 0;

    for (SlotId slot_id = 0;
         slot_id < slot_count;
         ++slot_id) {
        if (active_slots[slot_id]) {
            total_record_bytes += records[slot_id].size();
        }
    }

    if (directory_end + total_record_bytes > PAGE_SIZE) {
        return Status::OutOfMemory(
            "Not enough free space in page"
        );
    }

    char* page_data = page->GetData();

    // Conserva el encabezado, pero limpia el directorio y el área de datos.
    std::memset(
        page_data + PAGE_HEADER_SIZE,
        0,
        PAGE_SIZE - PAGE_HEADER_SIZE
    );

    WriteValue<uint16_t>(
        page_data,
        SLOT_COUNT_OFFSET,
        slot_count
    );

    uint16_t free_space_pointer =
        static_cast<uint16_t>(PAGE_SIZE);

    for (SlotId slot_id = 0;
         slot_id < slot_count;
         ++slot_id) {
        if (!active_slots[slot_id]) {
            WriteSlot(page_data, slot_id, 0, 0);
            continue;
        }

        const std::vector<char>& record_data = records[slot_id];

        free_space_pointer = static_cast<uint16_t>(
            free_space_pointer - record_data.size()
        );

        std::memcpy(
            page_data + free_space_pointer,
            record_data.data(),
            record_data.size()
        );

        WriteSlot(
            page_data,
            slot_id,
            free_space_pointer,
            static_cast<uint16_t>(record_data.size())
        );
    }

    WriteValue<uint16_t>(
        page_data,
        FREE_SPACE_POINTER_OFFSET,
        free_space_pointer
    );

    page->SetDirty(true);
    return Status::OK();
}

} // namespace

Status SlottedPage::Init() {
    if (page_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page cannot be null"
        );
    }

    if (page_->GetPageId() == INVALID_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page must have a valid PageId"
        );
    }

    char* page_data = page_->GetData();
    std::memset(page_data, 0, PAGE_SIZE);

    const PageId page_id = page_->GetPageId();
    const uint16_t slot_count = 0;
    const uint16_t free_space_pointer =
        static_cast<uint16_t>(PAGE_SIZE);
    const PageId next_page_id = INVALID_PAGE_ID;
    const uint32_t lsn = 0;

    WriteValue<PageId>(page_data, PAGE_ID_OFFSET, page_id);
    WriteValue<uint16_t>(page_data, SLOT_COUNT_OFFSET, slot_count);
    WriteValue<uint16_t>(
        page_data,
        FREE_SPACE_POINTER_OFFSET,
        free_space_pointer
    );
    WriteValue<PageId>(
        page_data,
        NEXT_PAGE_ID_OFFSET,
        next_page_id
    );
    WriteValue<uint32_t>(page_data, LSN_OFFSET, lsn);

    page_->SetDirty(true);
    return Status::OK();
}

bool SlottedPage::IsInitialized() const {
    if (page_ == nullptr || page_->GetPageId() == INVALID_PAGE_ID) {
        return false;
    }

    const char* page_data = page_->GetData();
    const PageId stored_page_id =
        ReadValue<PageId>(page_data, PAGE_ID_OFFSET);
    const uint16_t slot_count =
        ReadValue<uint16_t>(page_data, SLOT_COUNT_OFFSET);
    const uint16_t free_space_pointer =
        ReadValue<uint16_t>(page_data, FREE_SPACE_POINTER_OFFSET);

    const std::size_t directory_end =
        PAGE_HEADER_SIZE +
        static_cast<std::size_t>(slot_count) * SLOT_ENTRY_SIZE;

    return stored_page_id == page_->GetPageId() &&
           directory_end <= PAGE_SIZE &&
           free_space_pointer >= directory_end &&
           free_space_pointer <= PAGE_SIZE;
}

uint16_t SlottedPage::GetFreeSpace() const {
    if (!IsInitialized()) {
        return 0;
    }

    const char* page_data = page_->GetData();
    const uint16_t slot_count = GetSlotCount();
    const uint16_t free_space_pointer =
        ReadValue<uint16_t>(
            page_data,
            FREE_SPACE_POINTER_OFFSET
        );

    const std::size_t directory_end =
        PAGE_HEADER_SIZE +
        static_cast<std::size_t>(slot_count) * SLOT_ENTRY_SIZE;

    return static_cast<uint16_t>(
        free_space_pointer - directory_end
    );
}

uint16_t SlottedPage::GetSlotCount() const {
    if (page_ == nullptr) {
        return 0;
    }

    return ReadValue<uint16_t>(
        page_->GetData(),
        SLOT_COUNT_OFFSET
    );
}

PageId SlottedPage::GetNextPageId() const {
    if (!IsInitialized()) {
        return INVALID_PAGE_ID;
    }

    return ReadValue<PageId>(
        page_->GetData(),
        NEXT_PAGE_ID_OFFSET
    );
}

Status SlottedPage::SetNextPageId(PageId next_page_id) {
    if (!IsInitialized()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Slotted page is not initialized"
        );
    }

    if (next_page_id < INVALID_PAGE_ID ||
        next_page_id == page_->GetPageId()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid next page id"
        );
    }

    WriteValue<PageId>(
        page_->GetData(),
        NEXT_PAGE_ID_OFFSET,
        next_page_id
    );

    page_->SetDirty(true);
    return Status::OK();
}

Status SlottedPage::InsertRecord(
    const char* data,
    uint32_t size,
    SlotId* slot_id
) {
    if (page_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page cannot be null"
        );
    }

    if (!IsInitialized()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Slotted page is not initialized"
        );
    }

    if (data == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record data cannot be null"
        );
    }

    if (slot_id == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Slot id output cannot be null"
        );
    }

    if (size == 0 || size > PAGE_SIZE) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid record size"
        );
    }

    if (size > std::numeric_limits<uint16_t>::max()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record is too large"
        );
    }

    char* page_data = page_->GetData();
    uint16_t slot_count = GetSlotCount();
    uint16_t free_space_pointer = ReadValue<uint16_t>(
        page_data,
        FREE_SPACE_POINTER_OFFSET
    );

    SlotId selected_slot = slot_count;
    bool reusing_slot = false;

    for (SlotId current_slot = 0;
         current_slot < slot_count;
         ++current_slot) {
        const uint16_t current_offset =
            ReadSlotRecordOffset(page_data, current_slot);
        const uint16_t current_length =
            ReadSlotRecordLength(page_data, current_slot);

        if (current_offset == 0 && current_length == 0) {
            selected_slot = current_slot;
            reusing_slot = true;
            break;
        }
    }

    const std::size_t required_space =
        static_cast<std::size_t>(size) +
        (reusing_slot ? 0 : SLOT_ENTRY_SIZE);

    if (GetFreeSpace() < required_space) {
        return Status::OutOfMemory(
            "Not enough free space in page"
        );
    }

    const uint16_t record_offset =
        static_cast<uint16_t>(free_space_pointer - size);

    std::memcpy(page_data + record_offset, data, size);

    WriteSlot(
        page_data,
        selected_slot,
        record_offset,
        static_cast<uint16_t>(size)
    );

    if (!reusing_slot) {
        ++slot_count;
        WriteValue<uint16_t>(
            page_data,
            SLOT_COUNT_OFFSET,
            slot_count
        );
    }

    WriteValue<uint16_t>(
        page_data,
        FREE_SPACE_POINTER_OFFSET,
        record_offset
    );

    page_->SetDirty(true);
    *slot_id = selected_slot;

    return Status::OK();
}

Status SlottedPage::ReadRecord(
    SlotId slot_id,
    Record* record
) const {
    if (page_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Page cannot be null"
        );
    }

    if (!IsInitialized()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Slotted page is not initialized"
        );
    }

    if (record == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record output cannot be null"
        );
    }

    const char* page_data = page_->GetData();
    const uint16_t slot_count = GetSlotCount();

    if (slot_id >= slot_count) {
        return Status::NotFound(
            "Slot does not exist"
        );
    }

    const uint16_t record_offset =
        ReadSlotRecordOffset(page_data, slot_id);
    const uint16_t record_length =
        ReadSlotRecordLength(page_data, slot_id);

    if (record_offset == 0 && record_length == 0) {
        return Status::NotFound(
            "Record was deleted or slot is empty"
        );
    }

    const std::size_t directory_end =
        PAGE_HEADER_SIZE +
        static_cast<std::size_t>(slot_count) * SLOT_ENTRY_SIZE;
    const std::size_t record_end =
        static_cast<std::size_t>(record_offset) +
        record_length;

    if (record_offset < directory_end || record_end > PAGE_SIZE) {
        return Status::IOError(
            "Corrupted slot entry"
        );
    }

    record->SetRecordID({page_->GetPageId(), slot_id});
    record->SetData(page_data + record_offset, record_length);

    return Status::OK();
}

Status SlottedPage::UpdateRecord(
    SlotId slot_id,
    const char* data,
    uint32_t size
) {
    if (page_ == nullptr || !IsInitialized()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Slotted page is not initialized"
        );
    }

    if (data == nullptr || size == 0 || size > PAGE_SIZE) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid updated record"
        );
    }

    if (size > std::numeric_limits<uint16_t>::max()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Updated record is too large"
        );
    }

    const uint16_t slot_count = GetSlotCount();

    if (slot_id >= slot_count) {
        return Status::NotFound(
            "Slot does not exist"
        );
    }

    std::vector<std::vector<char>> records;
    std::vector<bool> active_slots;

    Status status = LoadRecords(
        page_->GetData(),
        slot_count,
        &records,
        &active_slots
    );

    if (!status.ok()) {
        return status;
    }

    if (!active_slots[slot_id]) {
        return Status::NotFound(
            "Record was deleted or slot is empty"
        );
    }

    records[slot_id].assign(data, data + size);

    return RewriteRecords(
        page_,
        slot_count,
        records,
        active_slots
    );
}

Status SlottedPage::DeleteRecord(SlotId slot_id) {
    if (page_ == nullptr || !IsInitialized()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Slotted page is not initialized"
        );
    }

    uint16_t slot_count = GetSlotCount();

    if (slot_id >= slot_count) {
        return Status::NotFound(
            "Slot does not exist"
        );
    }

    std::vector<std::vector<char>> records;
    std::vector<bool> active_slots;

    Status status = LoadRecords(
        page_->GetData(),
        slot_count,
        &records,
        &active_slots
    );

    if (!status.ok()) {
        return status;
    }

    if (!active_slots[slot_id]) {
        return Status::NotFound(
            "Record was already deleted"
        );
    }

    records[slot_id].clear();
    active_slots[slot_id] = false;

    // Elimina slots vacíos del final para recuperar también
    // el espacio ocupado por sus entradas del directorio.
    while (slot_count > 0 && !active_slots[slot_count - 1]) {
        --slot_count;
    }

    records.resize(slot_count);
    active_slots.resize(slot_count);

    return RewriteRecords(
        page_,
        slot_count,
        records,
        active_slots
    );
}

} // namespace minidbms
