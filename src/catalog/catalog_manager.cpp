#include "catalog/catalog_manager.h"

#include "common/config.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace minidbms {
namespace {

constexpr uint32_t CATALOG_MAGIC = 0x31544143U; // "CAT1"
constexpr uint16_t CATALOG_VERSION = 1;
constexpr std::size_t CATALOG_HEADER_SIZE = 16;

constexpr std::size_t MAGIC_OFFSET = 0;
constexpr std::size_t VERSION_OFFSET = 4;
constexpr std::size_t TABLE_COUNT_OFFSET = 6;
constexpr std::size_t INDEX_COUNT_OFFSET = 8;
constexpr std::size_t USED_BYTES_OFFSET = 10;
constexpr std::size_t RESERVED_OFFSET = 12;

template <typename T>
bool ReadValue(
    const char* data,
    std::size_t limit,
    std::size_t* cursor,
    T* value
) {
    if (data == nullptr || cursor == nullptr || value == nullptr ||
        *cursor > limit || sizeof(T) > limit - *cursor) {
        return false;
    }

    std::memcpy(value, data + *cursor, sizeof(T));
    *cursor += sizeof(T);
    return true;
}

template <typename T>
bool WriteValue(
    std::vector<char>* data,
    std::size_t* cursor,
    const T& value
) {
    if (data == nullptr || cursor == nullptr ||
        *cursor > data->size() ||
        sizeof(T) > data->size() - *cursor) {
        return false;
    }

    std::memcpy(data->data() + *cursor, &value, sizeof(T));
    *cursor += sizeof(T);
    return true;
}

bool ReadString(
    const char* data,
    std::size_t limit,
    std::size_t* cursor,
    uint16_t length,
    std::string* value
) {
    if (value == nullptr || cursor == nullptr ||
        *cursor > limit ||
        static_cast<std::size_t>(length) > limit - *cursor) {
        return false;
    }

    value->assign(data + *cursor, data + *cursor + length);
    *cursor += length;
    return true;
}

bool WriteString(
    std::vector<char>* data,
    std::size_t* cursor,
    const std::string& value
) {
    if (data == nullptr || cursor == nullptr ||
        *cursor > data->size() ||
        value.size() > data->size() - *cursor) {
        return false;
    }

    if (!value.empty()) {
        std::memcpy(
            data->data() + *cursor,
            value.data(),
            value.size()
        );
    }

    *cursor += value.size();
    return true;
}

bool FitsUint16(std::size_t value) {
    return value <= std::numeric_limits<uint16_t>::max();
}

} // namespace

CatalogManager::CatalogManager(BufferPoolManager* bpm)
    : bpm_(bpm) {
    if (bpm_ == nullptr) {
        initialization_status_ = Status(
            StatusCode::INVALID_ARGUMENT,
            "Catalog BufferPoolManager cannot be null"
        );
        return;
    }

    initialization_status_ = Load();
}

std::string CatalogManager::MakeIndexKey(
    const std::string& table_name,
    const std::string& column_name
) {
    return table_name + "." + column_name;
}

Status CatalogManager::Load() {
    tables_.clear();
    indexes_.clear();
    index_names_.clear();

    if (bpm_ == nullptr) {
        initialization_status_ = Status::OK();
        return initialization_status_;
    }

    Page* page = bpm_->FetchPage(HEADER_PAGE_ID);
    if (page == nullptr) {
        initialization_status_ = Status::IOError(
            "Could not fetch catalog header page"
        );
        return initialization_status_;
    }

    const char* data = page->GetData();
    std::size_t cursor = 0;

    uint32_t magic = 0;
    if (!ReadValue(data, PAGE_SIZE, &cursor, &magic)) {
        bpm_->UnpinPage(HEADER_PAGE_ID, false);
        initialization_status_ = Status::IOError(
            "Could not read catalog magic"
        );
        return initialization_status_;
    }

    if (magic == 0) {
        bpm_->UnpinPage(HEADER_PAGE_ID, false);
        initialization_status_ = Status::OK();
        return initialization_status_;
    }

    uint16_t version = 0;
    uint16_t table_count = 0;
    uint16_t index_count = 0;
    uint16_t used_bytes = 0;
    uint32_t reserved = 0;

    const bool header_ok =
        ReadValue(data, PAGE_SIZE, &cursor, &version) &&
        ReadValue(data, PAGE_SIZE, &cursor, &table_count) &&
        ReadValue(data, PAGE_SIZE, &cursor, &index_count) &&
        ReadValue(data, PAGE_SIZE, &cursor, &used_bytes) &&
        ReadValue(data, PAGE_SIZE, &cursor, &reserved);

    if (!header_ok ||
        magic != CATALOG_MAGIC ||
        version != CATALOG_VERSION ||
        used_bytes < CATALOG_HEADER_SIZE ||
        used_bytes > PAGE_SIZE) {
        bpm_->UnpinPage(HEADER_PAGE_ID, false);
        initialization_status_ = Status::IOError(
            "Catalog header is corrupted or incompatible"
        );
        return initialization_status_;
    }

    const std::size_t limit = used_bytes;

    for (uint16_t table_index = 0;
         table_index < table_count;
         ++table_index) {
        const std::size_t entry_start = cursor;

        uint16_t entry_size = 0;
        uint16_t name_length = 0;
        uint16_t column_count = 0;
        uint16_t entry_reserved = 0;
        PageId first_page_id = INVALID_PAGE_ID;

        if (!ReadValue(data, limit, &cursor, &entry_size) ||
            !ReadValue(data, limit, &cursor, &name_length) ||
            !ReadValue(data, limit, &cursor, &column_count) ||
            !ReadValue(data, limit, &cursor, &entry_reserved) ||
            !ReadValue(data, limit, &cursor, &first_page_id) ||
            entry_size < 12 ||
            entry_start + entry_size > limit ||
            first_page_id <= HEADER_PAGE_ID) {
            bpm_->UnpinPage(HEADER_PAGE_ID, false);
            initialization_status_ = Status::IOError(
                "Catalog table entry is corrupted"
            );
            return initialization_status_;
        }

        std::string table_name;
        if (!ReadString(
                data,
                entry_start + entry_size,
                &cursor,
                name_length,
                &table_name
            ) ||
            table_name.empty()) {
            bpm_->UnpinPage(HEADER_PAGE_ID, false);
            initialization_status_ = Status::IOError(
                "Catalog table name is corrupted"
            );
            return initialization_status_;
        }

        std::vector<Column> columns;
        columns.reserve(column_count);

        for (uint16_t column_index = 0;
             column_index < column_count;
             ++column_index) {
            uint16_t column_name_length = 0;
            uint8_t type_value = 0;
            uint8_t column_reserved = 0;
            uint32_t declared_length = 0;

            if (!ReadValue(
                    data,
                    entry_start + entry_size,
                    &cursor,
                    &column_name_length
                ) ||
                !ReadValue(
                    data,
                    entry_start + entry_size,
                    &cursor,
                    &type_value
                ) ||
                !ReadValue(
                    data,
                    entry_start + entry_size,
                    &cursor,
                    &column_reserved
                ) ||
                !ReadValue(
                    data,
                    entry_start + entry_size,
                    &cursor,
                    &declared_length
                ) ||
                type_value > static_cast<uint8_t>(TypeId::BOOLEAN)) {
                bpm_->UnpinPage(HEADER_PAGE_ID, false);
                initialization_status_ = Status::IOError(
                    "Catalog column entry is corrupted"
                );
                return initialization_status_;
            }

            std::string column_name;
            if (!ReadString(
                    data,
                    entry_start + entry_size,
                    &cursor,
                    column_name_length,
                    &column_name
                ) ||
                column_name.empty()) {
                bpm_->UnpinPage(HEADER_PAGE_ID, false);
                initialization_status_ = Status::IOError(
                    "Catalog column name is corrupted"
                );
                return initialization_status_;
            }

            columns.push_back({
                std::move(column_name),
                static_cast<TypeId>(type_value),
                declared_length
            });
        }

        if (cursor != entry_start + entry_size) {
            bpm_->UnpinPage(HEADER_PAGE_ID, false);
            initialization_status_ = Status::IOError(
                "Catalog table entry size is inconsistent"
            );
            return initialization_status_;
        }

        tables_.emplace(
            table_name,
            TableMetadata{
                table_name,
                Schema(std::move(columns)),
                first_page_id
            }
        );
    }

    struct PendingIndex {
        std::string name;
        std::string table_name;
        std::string column_name;
        PageId header_page_id;
    };

    std::vector<PendingIndex> pending_indexes;
    pending_indexes.reserve(index_count);

    for (uint16_t index_number = 0;
         index_number < index_count;
         ++index_number) {
        const std::size_t entry_start = cursor;

        uint16_t entry_size = 0;
        uint16_t index_name_length = 0;
        uint16_t table_name_length = 0;
        uint16_t column_name_length = 0;
        PageId header_page_id = INVALID_PAGE_ID;

        if (!ReadValue(data, limit, &cursor, &entry_size) ||
            !ReadValue(data, limit, &cursor, &index_name_length) ||
            !ReadValue(data, limit, &cursor, &table_name_length) ||
            !ReadValue(data, limit, &cursor, &column_name_length) ||
            !ReadValue(data, limit, &cursor, &header_page_id) ||
            entry_size < 12 ||
            entry_start + entry_size > limit ||
            header_page_id <= HEADER_PAGE_ID) {
            bpm_->UnpinPage(HEADER_PAGE_ID, false);
            initialization_status_ = Status::IOError(
                "Catalog index entry is corrupted"
            );
            return initialization_status_;
        }

        PendingIndex pending;

        if (!ReadString(
                data,
                entry_start + entry_size,
                &cursor,
                index_name_length,
                &pending.name
            ) ||
            !ReadString(
                data,
                entry_start + entry_size,
                &cursor,
                table_name_length,
                &pending.table_name
            ) ||
            !ReadString(
                data,
                entry_start + entry_size,
                &cursor,
                column_name_length,
                &pending.column_name
            ) ||
            pending.name.empty() ||
            pending.table_name.empty() ||
            pending.column_name.empty() ||
            cursor != entry_start + entry_size) {
            bpm_->UnpinPage(HEADER_PAGE_ID, false);
            initialization_status_ = Status::IOError(
                "Catalog index strings are corrupted"
            );
            return initialization_status_;
        }

        pending.header_page_id = header_page_id;
        pending_indexes.push_back(std::move(pending));
    }

    if (cursor != limit) {
        bpm_->UnpinPage(HEADER_PAGE_ID, false);
        initialization_status_ = Status::IOError(
            "Catalog used-byte count is inconsistent"
        );
        return initialization_status_;
    }

    if (!bpm_->UnpinPage(HEADER_PAGE_ID, false)) {
        initialization_status_ = Status::IOError(
            "Could not unpin catalog header page"
        );
        return initialization_status_;
    }

    for (PendingIndex& pending : pending_indexes) {
        const auto table_it = tables_.find(pending.table_name);
        if (table_it == tables_.end()) {
            initialization_status_ = Status::IOError(
                "Catalog index references a missing table"
            );
            return initialization_status_;
        }

        bool column_found = false;
        for (const Column& column :
             table_it->second.schema.GetColumns()) {
            if (column.name == pending.column_name) {
                column_found = true;
                break;
            }
        }

        if (!column_found) {
            initialization_status_ = Status::IOError(
                "Catalog index references a missing column"
            );
            return initialization_status_;
        }

        std::unique_ptr<HashIndex> index;
        Status status = HashIndex::Open(
            bpm_,
            pending.header_page_id,
            &index
        );

        if (!status.ok()) {
            initialization_status_ = status;
            return initialization_status_;
        }

        const std::string key = MakeIndexKey(
            pending.table_name,
            pending.column_name
        );

        if (indexes_.find(key) != indexes_.end() ||
            index_names_.find(pending.name) != index_names_.end()) {
            initialization_status_ = Status::IOError(
                "Catalog contains duplicate index metadata"
            );
            return initialization_status_;
        }

        index_names_[pending.name] = key;
        indexes_.emplace(
            key,
            IndexMetadata{
                std::move(pending.name),
                std::move(pending.table_name),
                std::move(pending.column_name),
                pending.header_page_id,
                std::move(index)
            }
        );
    }

    initialization_status_ = Status::OK();
    return initialization_status_;
}

Status CatalogManager::Persist() {
    if (bpm_ == nullptr) {
        return Status::OK();
    }

    if (tables_.size() > std::numeric_limits<uint16_t>::max() ||
        indexes_.size() > std::numeric_limits<uint16_t>::max()) {
        return Status::OutOfMemory(
            "Catalog contains too many entries"
        );
    }

    std::vector<char> serialized(PAGE_SIZE, 0);
    std::size_t cursor = CATALOG_HEADER_SIZE;

    std::vector<std::string> table_names;
    table_names.reserve(tables_.size());
    for (const auto& entry : tables_) {
        table_names.push_back(entry.first);
    }
    std::sort(table_names.begin(), table_names.end());

    for (const std::string& table_name : table_names) {
        const TableMetadata& table = tables_.at(table_name);

        if (!FitsUint16(table.name.size()) ||
            table.schema.GetColumnCount() >
                std::numeric_limits<uint16_t>::max()) {
            return Status::OutOfMemory(
                "Catalog table metadata is too large"
            );
        }

        const std::size_t entry_start = cursor;
        uint16_t placeholder_size = 0;

        if (!WriteValue(&serialized, &cursor, placeholder_size) ||
            !WriteValue(
                &serialized,
                &cursor,
                static_cast<uint16_t>(table.name.size())
            ) ||
            !WriteValue(
                &serialized,
                &cursor,
                static_cast<uint16_t>(
                    table.schema.GetColumnCount()
                )
            ) ||
            !WriteValue(
                &serialized,
                &cursor,
                static_cast<uint16_t>(0)
            ) ||
            !WriteValue(
                &serialized,
                &cursor,
                table.first_page_id
            ) ||
            !WriteString(&serialized, &cursor, table.name)) {
            return Status::OutOfMemory(
                "Catalog page is full"
            );
        }

        for (const Column& column :
             table.schema.GetColumns()) {
            if (!FitsUint16(column.name.size()) ||
                !WriteValue(
                    &serialized,
                    &cursor,
                    static_cast<uint16_t>(column.name.size())
                ) ||
                !WriteValue(
                    &serialized,
                    &cursor,
                    static_cast<uint8_t>(column.type)
                ) ||
                !WriteValue(
                    &serialized,
                    &cursor,
                    static_cast<uint8_t>(0)
                ) ||
                !WriteValue(
                    &serialized,
                    &cursor,
                    column.length
                ) ||
                !WriteString(
                    &serialized,
                    &cursor,
                    column.name
                )) {
                return Status::OutOfMemory(
                    "Catalog page is full"
                );
            }
        }

        const std::size_t entry_size = cursor - entry_start;
        if (!FitsUint16(entry_size)) {
            return Status::OutOfMemory(
                "Catalog table entry is too large"
            );
        }

        const uint16_t stored_size =
            static_cast<uint16_t>(entry_size);
        std::memcpy(
            serialized.data() + entry_start,
            &stored_size,
            sizeof(stored_size)
        );
    }

    std::vector<std::string> index_keys;
    index_keys.reserve(indexes_.size());
    for (const auto& entry : indexes_) {
        index_keys.push_back(entry.first);
    }
    std::sort(index_keys.begin(), index_keys.end());

    for (const std::string& key : index_keys) {
        const IndexMetadata& metadata = indexes_.at(key);

        if (!FitsUint16(metadata.name.size()) ||
            !FitsUint16(metadata.table_name.size()) ||
            !FitsUint16(metadata.column_name.size())) {
            return Status::OutOfMemory(
                "Catalog index metadata is too large"
            );
        }

        const std::size_t entry_start = cursor;
        uint16_t placeholder_size = 0;

        if (!WriteValue(&serialized, &cursor, placeholder_size) ||
            !WriteValue(
                &serialized,
                &cursor,
                static_cast<uint16_t>(metadata.name.size())
            ) ||
            !WriteValue(
                &serialized,
                &cursor,
                static_cast<uint16_t>(
                    metadata.table_name.size()
                )
            ) ||
            !WriteValue(
                &serialized,
                &cursor,
                static_cast<uint16_t>(
                    metadata.column_name.size()
                )
            ) ||
            !WriteValue(
                &serialized,
                &cursor,
                metadata.header_page_id
            ) ||
            !WriteString(
                &serialized,
                &cursor,
                metadata.name
            ) ||
            !WriteString(
                &serialized,
                &cursor,
                metadata.table_name
            ) ||
            !WriteString(
                &serialized,
                &cursor,
                metadata.column_name
            )) {
            return Status::OutOfMemory(
                "Catalog page is full"
            );
        }

        const std::size_t entry_size = cursor - entry_start;
        if (!FitsUint16(entry_size)) {
            return Status::OutOfMemory(
                "Catalog index entry is too large"
            );
        }

        const uint16_t stored_size =
            static_cast<uint16_t>(entry_size);
        std::memcpy(
            serialized.data() + entry_start,
            &stored_size,
            sizeof(stored_size)
        );
    }

    if (!FitsUint16(cursor)) {
        return Status::OutOfMemory(
            "Catalog page is full"
        );
    }

    std::size_t header_cursor = 0;

    const bool header_written =
        WriteValue(
            &serialized,
            &header_cursor,
            CATALOG_MAGIC
        ) &&
        WriteValue(
            &serialized,
            &header_cursor,
            CATALOG_VERSION
        ) &&
        WriteValue(
            &serialized,
            &header_cursor,
            static_cast<uint16_t>(tables_.size())
        ) &&
        WriteValue(
            &serialized,
            &header_cursor,
            static_cast<uint16_t>(indexes_.size())
        ) &&
        WriteValue(
            &serialized,
            &header_cursor,
            static_cast<uint16_t>(cursor)
        ) &&
        WriteValue(
            &serialized,
            &header_cursor,
            static_cast<uint32_t>(0)
        );

    if (!header_written ||
        header_cursor != CATALOG_HEADER_SIZE) {
        return Status::IOError(
            "Could not serialize catalog header"
        );
    }

    Page* page = bpm_->FetchPage(HEADER_PAGE_ID);
    if (page == nullptr) {
        return Status::IOError(
            "Could not fetch catalog header page"
        );
    }

    std::memcpy(
        page->GetData(),
        serialized.data(),
        PAGE_SIZE
    );

    if (!bpm_->UnpinPage(HEADER_PAGE_ID, true)) {
        return Status::IOError(
            "Could not unpin catalog header page"
        );
    }

    if (!bpm_->FlushPage(HEADER_PAGE_ID)) {
        return Status::IOError(
            "Could not persist catalog header page"
        );
    }

    return Status::OK();
}

Status CatalogManager::Flush() {
    Status status = Persist();
    if (status.ok()) {
        initialization_status_ = status;
    }
    return status;
}

Status CatalogManager::CreateTable(
    const std::string& table_name,
    const Schema& schema,
    PageId first_page_id
) {
    if (!initialization_status_.ok()) {
        return initialization_status_;
    }

    if (table_name.empty()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Table name cannot be empty"
        );
    }

    if (first_page_id <= HEADER_PAGE_ID) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Table requires a valid first data page"
        );
    }

    if (schema.GetColumnCount() == 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Table schema cannot be empty"
        );
    }

    if (tables_.find(table_name) != tables_.end()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Table already exists in catalog"
        );
    }

    tables_.emplace(
        table_name,
        TableMetadata{
            table_name,
            schema,
            first_page_id
        }
    );

    Status status = Persist();
    if (!status.ok()) {
        tables_.erase(table_name);
        return status;
    }

    return Status::OK();
}

Status CatalogManager::GetTableSchema(
    const std::string& table_name,
    Schema* schema
) const {
    if (!initialization_status_.ok()) {
        return initialization_status_;
    }

    if (schema == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Schema output cannot be null"
        );
    }

    const auto iterator = tables_.find(table_name);
    if (iterator == tables_.end()) {
        return Status::NotFound(
            "Table was not found in catalog"
        );
    }

    *schema = iterator->second.schema;
    return Status::OK();
}

Status CatalogManager::GetTableFirstPageId(
    const std::string& table_name,
    PageId* first_page_id
) const {
    if (!initialization_status_.ok()) {
        return initialization_status_;
    }

    if (first_page_id == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "First page output cannot be null"
        );
    }

    const auto iterator = tables_.find(table_name);
    if (iterator == tables_.end()) {
        return Status::NotFound(
            "Table was not found in catalog"
        );
    }

    *first_page_id = iterator->second.first_page_id;
    return Status::OK();
}

Status CatalogManager::CreateIndex(
    const std::string& index_name,
    const std::string& table_name,
    const std::string& column_name,
    std::unique_ptr<HashIndex> index
) {
    if (!initialization_status_.ok()) {
        return initialization_status_;
    }

    if (index_name.empty() ||
        table_name.empty() ||
        column_name.empty() ||
        !index) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Index metadata and object must be valid"
        );
    }

    const auto table_iterator = tables_.find(table_name);
    if (table_iterator == tables_.end()) {
        return Status::NotFound(
            "Index table was not found in catalog"
        );
    }

    bool column_found = false;
    for (const Column& column :
         table_iterator->second.schema.GetColumns()) {
        if (column.name == column_name) {
            column_found = true;
            break;
        }
    }

    if (!column_found) {
        return Status::NotFound(
            "Index column was not found in table schema"
        );
    }

    const std::string key =
        MakeIndexKey(table_name, column_name);

    if (indexes_.find(key) != indexes_.end()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "An index already exists for this table column"
        );
    }

    if (index_names_.find(index_name) != index_names_.end()) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Index name already exists"
        );
    }

    const PageId header_page_id =
        index->GetHeaderPageId();

    index_names_[index_name] = key;
    indexes_.emplace(
        key,
        IndexMetadata{
            index_name,
            table_name,
            column_name,
            header_page_id,
            std::move(index)
        }
    );

    Status status = Persist();
    if (!status.ok()) {
        Status cleanup_status = Status::OK();
        const auto inserted = indexes_.find(key);
        if (inserted != indexes_.end() && inserted->second.index) {
            cleanup_status = inserted->second.index->Destroy();
        }

        indexes_.erase(key);
        index_names_.erase(index_name);

        if (!cleanup_status.ok()) {
            return Status::IOError(
                "Catalog persistence failed: " + status.message() +
                "; index cleanup failed: " + cleanup_status.message()
            );
        }

        return status;
    }

    return Status::OK();
}

bool CatalogManager::HasIndex(
    const std::string& table_name,
    const std::string& column_name
) const {
    return initialization_status_.ok() &&
           indexes_.find(
               MakeIndexKey(table_name, column_name)
           ) != indexes_.end();
}

bool CatalogManager::HasIndexName(
    const std::string& index_name
) const {
    return initialization_status_.ok() &&
           index_names_.find(index_name) != index_names_.end();
}

HashIndex* CatalogManager::GetIndex(
    const std::string& table_name,
    const std::string& column_name
) const {
    if (!initialization_status_.ok()) {
        return nullptr;
    }

    const auto iterator = indexes_.find(
        MakeIndexKey(table_name, column_name)
    );

    return iterator == indexes_.end()
        ? nullptr
        : iterator->second.index.get();
}

Status CatalogManager::GetIndexHeaderPageId(
    const std::string& table_name,
    const std::string& column_name,
    PageId* header_page_id
) const {
    if (!initialization_status_.ok()) {
        return initialization_status_;
    }

    if (header_page_id == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Index header page output cannot be null"
        );
    }

    const auto iterator = indexes_.find(
        MakeIndexKey(table_name, column_name)
    );

    if (iterator == indexes_.end()) {
        return Status::NotFound(
            "Index was not found in catalog"
        );
    }

    *header_page_id = iterator->second.header_page_id;
    return Status::OK();
}

Status CatalogManager::GetTableIndexes(
    const std::string& table_name,
    std::vector<CatalogIndexInfo>* indexes
) const {
    if (!initialization_status_.ok()) {
        return initialization_status_;
    }

    if (indexes == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Index metadata output cannot be null"
        );
    }

    if (tables_.find(table_name) == tables_.end()) {
        return Status::NotFound(
            "Table was not found in catalog"
        );
    }

    indexes->clear();

    for (const auto& entry : indexes_) {
        const IndexMetadata& metadata = entry.second;

        if (metadata.table_name == table_name) {
            indexes->push_back({
                metadata.name,
                metadata.table_name,
                metadata.column_name,
                metadata.header_page_id
            });
        }
    }

    std::sort(
        indexes->begin(),
        indexes->end(),
        [](const CatalogIndexInfo& left,
           const CatalogIndexInfo& right) {
            return left.index_name < right.index_name;
        }
    );

    return Status::OK();
}

} // namespace minidbms
