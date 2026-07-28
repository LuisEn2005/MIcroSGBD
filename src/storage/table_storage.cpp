#include "storage/table_storage.h"

#include "index/index_key.h"
#include "storage/heap_file.h"

#include <algorithm>
#include <utility>

namespace minidbms {
namespace {

Status FindColumnIndex(
    const Schema& schema,
    const std::string& column_name,
    uint32_t* column_index
) {
    if (column_index == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Column index output cannot be null"
        );
    }

    const auto& columns = schema.GetColumns();

    for (uint32_t index = 0;
         index < columns.size();
         ++index) {
        if (columns[index].name == column_name) {
            *column_index = index;
            return Status::OK();
        }
    }

    return Status::NotFound(
        "Indexed column does not exist in table schema: " +
        column_name
    );
}

bool SameOptionalKey(
    const std::optional<std::string>& left,
    const std::optional<std::string>& right
) {
    if (left.has_value() != right.has_value()) {
        return false;
    }

    if (!left.has_value()) {
        return true;
    }

    return left.value() == right.value();
}

Status CombineFailure(
    const Status& primary,
    const Status& rollback,
    const std::string& context
) {
    if (rollback.ok()) {
        return primary;
    }

    return Status::IOError(
        context + "; original error: " + primary.message() +
        "; rollback error: " + rollback.message()
    );
}

} // namespace

TableStorage::TableStorage(
    BufferPoolManager* buffer_pool_manager,
    CatalogManager* catalog_manager
)
    : bpm_(buffer_pool_manager),
      catalog_(catalog_manager) {}

Status TableStorage::ValidateDependencies() const {
    if (bpm_ == nullptr || catalog_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "TableStorage dependencies cannot be null"
        );
    }

    const Status catalog_status =
        catalog_->GetInitializationStatus();

    if (!catalog_status.ok()) {
        return catalog_status;
    }

    return Status::OK();
}

Status TableStorage::LoadTable(
    const std::string& table_name,
    Schema* schema,
    PageId* first_page_id,
    std::vector<IndexBinding>* bindings
) const {
    Status status = ValidateDependencies();
    if (!status.ok()) {
        return status;
    }

    if (table_name.empty() ||
        schema == nullptr ||
        first_page_id == nullptr ||
        bindings == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Invalid TableStorage table context output"
        );
    }

    status = catalog_->GetTableSchema(
        table_name,
        schema
    );
    if (!status.ok()) {
        return status;
    }

    status = catalog_->GetTableFirstPageId(
        table_name,
        first_page_id
    );
    if (!status.ok()) {
        return status;
    }

    std::vector<CatalogIndexInfo> index_metadata;
    status = catalog_->GetTableIndexes(
        table_name,
        &index_metadata
    );
    if (!status.ok()) {
        return status;
    }

    bindings->clear();
    bindings->reserve(index_metadata.size());

    for (const CatalogIndexInfo& metadata :
         index_metadata) {
        uint32_t column_index = 0;
        status = FindColumnIndex(
            *schema,
            metadata.column_name,
            &column_index
        );
        if (!status.ok()) {
            return status;
        }

        HashIndex* index = catalog_->GetIndex(
            table_name,
            metadata.column_name
        );

        if (index == nullptr) {
            return Status::IOError(
                "Catalog index metadata has no open HashIndex: " +
                metadata.index_name
            );
        }

        bindings->push_back({
            metadata.index_name,
            metadata.column_name,
            column_index,
            index
        });
    }

    return Status::OK();
}

Status TableStorage::ValidateRecordBelongsToTable(
    PageId first_page_id,
    RecordID rid
) const {
    if (rid.page_id <= 0) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "RecordID contains an invalid page id"
        );
    }

    HeapFile heap_file(bpm_, first_page_id);
    bool contains_page = false;

    Status status = heap_file.ContainsPage(
        rid.page_id,
        &contains_page
    );
    if (!status.ok()) {
        return status;
    }

    if (!contains_page) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "RecordID does not belong to the requested table"
        );
    }

    return Status::OK();
}

Status TableStorage::EncodeIndexKeys(
    const Schema& schema,
    const Record& record,
    const std::vector<IndexBinding>& bindings,
    std::vector<EncodedIndexKey>* keys
) const {
    if (keys == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Encoded index key output cannot be null"
        );
    }

    keys->clear();
    keys->reserve(bindings.size());

    for (const IndexBinding& binding : bindings) {
        FieldValue value;
        Status status = RecordCodec::GetValue(
            schema,
            record,
            binding.column_index,
            &value
        );
        if (!status.ok()) {
            return status;
        }

        EncodedIndexKey encoded;
        encoded.binding = &binding;

        if (!std::holds_alternative<std::monostate>(
                value
            )) {
            std::string key;
            status = IndexKey::Encode(
                value,
                &key
            );
            if (!status.ok()) {
                return status;
            }

            encoded.key = std::move(key);
        }

        keys->push_back(std::move(encoded));
    }

    return Status::OK();
}

Status TableStorage::RollbackInsertedIndexEntries(
    RecordID rid,
    const std::vector<EncodedIndexKey>& keys,
    std::size_t inserted_count
) const {
    while (inserted_count > 0) {
        --inserted_count;
        const EncodedIndexKey& encoded =
            keys[inserted_count];

        if (!encoded.key.has_value()) {
            continue;
        }

        Status status = encoded.binding->index->Remove(
            encoded.key.value(),
            rid
        );

        if (!status.ok() &&
            status.code() != StatusCode::NOT_FOUND) {
            return status;
        }
    }

    return Status::OK();
}

Status TableStorage::RollbackRemovedIndexEntries(
    RecordID rid,
    const std::vector<EncodedIndexKey>& keys,
    const std::vector<std::size_t>& removed_positions
) const {
    for (auto iterator = removed_positions.rbegin();
         iterator != removed_positions.rend();
         ++iterator) {
        const EncodedIndexKey& encoded = keys[*iterator];

        if (!encoded.key.has_value()) {
            continue;
        }

        Status status = encoded.binding->index->Insert(
            encoded.key.value(),
            rid
        );
        if (!status.ok()) {
            return status;
        }
    }

    return Status::OK();
}

Status TableStorage::InsertRecord(
    const std::string& table_name,
    const std::vector<FieldValue>& values,
    RecordID* rid
) {
    if (rid == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Inserted RecordID output cannot be null"
        );
    }

    *rid = {};

    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;
    std::vector<IndexBinding> bindings;

    Status status = LoadTable(
        table_name,
        &schema,
        &first_page_id,
        &bindings
    );
    if (!status.ok()) {
        return status;
    }

    Record record;
    status = RecordCodec::Serialize(
        schema,
        values,
        &record
    );
    if (!status.ok()) {
        return status;
    }

    // Todas las claves se validan antes de modificar el HeapFile.
    std::vector<EncodedIndexKey> keys;
    status = EncodeIndexKeys(
        schema,
        record,
        bindings,
        &keys
    );
    if (!status.ok()) {
        return status;
    }

    HeapFile heap_file(bpm_, first_page_id);
    RecordID inserted_rid;

    status = heap_file.InsertRecord(
        record,
        &inserted_rid
    );
    if (!status.ok()) {
        return status;
    }

    std::size_t inserted_count = 0;

    for (const EncodedIndexKey& encoded : keys) {
        if (encoded.key.has_value()) {
            status = encoded.binding->index->Insert(
                encoded.key.value(),
                inserted_rid
            );

            if (!status.ok()) {
                const Status index_rollback =
                    RollbackInsertedIndexEntries(
                        inserted_rid,
                        keys,
                        inserted_count
                    );

                const Status heap_rollback =
                    heap_file.DeleteRecord(inserted_rid);

                if (!index_rollback.ok()) {
                    return CombineFailure(
                        status,
                        index_rollback,
                        "Insert index rollback failed"
                    );
                }

                if (!heap_rollback.ok()) {
                    return CombineFailure(
                        status,
                        heap_rollback,
                        "Insert HeapFile rollback failed"
                    );
                }

                return status;
            }
        }

        ++inserted_count;
    }

    *rid = inserted_rid;
    return Status::OK();
}

Status TableStorage::GetRecord(
    const std::string& table_name,
    RecordID rid,
    Record* record
) {
    if (record == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Record output cannot be null"
        );
    }

    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;
    std::vector<IndexBinding> bindings;

    Status status = LoadTable(
        table_name,
        &schema,
        &first_page_id,
        &bindings
    );
    if (!status.ok()) {
        return status;
    }

    status = ValidateRecordBelongsToTable(
        first_page_id,
        rid
    );
    if (!status.ok()) {
        return status;
    }

    HeapFile heap_file(bpm_, first_page_id);
    return heap_file.GetRecord(rid, record);
}

Status TableStorage::UpdateRecord(
    const std::string& table_name,
    RecordID rid,
    const std::vector<FieldValue>& values
) {
    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;
    std::vector<IndexBinding> bindings;

    Status status = LoadTable(
        table_name,
        &schema,
        &first_page_id,
        &bindings
    );
    if (!status.ok()) {
        return status;
    }

    status = ValidateRecordBelongsToTable(
        first_page_id,
        rid
    );
    if (!status.ok()) {
        return status;
    }

    HeapFile heap_file(bpm_, first_page_id);

    Record old_record;
    status = heap_file.GetRecord(
        rid,
        &old_record
    );
    if (!status.ok()) {
        return status;
    }

    Record new_record;
    status = RecordCodec::Serialize(
        schema,
        values,
        &new_record
    );
    if (!status.ok()) {
        return status;
    }
    new_record.SetRecordID(rid);

    std::vector<EncodedIndexKey> old_keys;
    std::vector<EncodedIndexKey> new_keys;

    status = EncodeIndexKeys(
        schema,
        old_record,
        bindings,
        &old_keys
    );
    if (!status.ok()) {
        return status;
    }

    status = EncodeIndexKeys(
        schema,
        new_record,
        bindings,
        &new_keys
    );
    if (!status.ok()) {
        return status;
    }

    status = heap_file.UpdateRecord(new_record);
    if (!status.ok()) {
        return status;
    }

    struct AppliedChange {
        std::size_t position{0};
        bool removed_old{false};
        bool inserted_new{false};
    };

    std::vector<AppliedChange> applied;

    for (std::size_t position = 0;
         position < bindings.size();
         ++position) {
        if (SameOptionalKey(
                old_keys[position].key,
                new_keys[position].key
            )) {
            continue;
        }

        AppliedChange change;
        change.position = position;

        if (old_keys[position].key.has_value()) {
            status = bindings[position].index->Remove(
                old_keys[position].key.value(),
                rid
            );

            if (!status.ok()) {
                break;
            }

            change.removed_old = true;
        }

        if (new_keys[position].key.has_value()) {
            status = bindings[position].index->Insert(
                new_keys[position].key.value(),
                rid
            );

            if (!status.ok()) {
                if (change.removed_old) {
                    const Status local_rollback =
                        bindings[position].index->Insert(
                            old_keys[position].key.value(),
                            rid
                        );
                    if (!local_rollback.ok()) {
                        status = CombineFailure(
                            status,
                            local_rollback,
                            "Update local index rollback failed"
                        );
                    }
                }
                break;
            }

            change.inserted_new = true;
        }

        applied.push_back(change);
    }

    if (status.ok()) {
        return Status::OK();
    }

    Status rollback_status = Status::OK();

    for (auto iterator = applied.rbegin();
         iterator != applied.rend();
         ++iterator) {
        const std::size_t position = iterator->position;

        if (iterator->inserted_new &&
            new_keys[position].key.has_value()) {
            const Status remove_status =
                bindings[position].index->Remove(
                    new_keys[position].key.value(),
                    rid
                );

            if (!remove_status.ok() &&
                remove_status.code() != StatusCode::NOT_FOUND &&
                rollback_status.ok()) {
                rollback_status = remove_status;
            }
        }

        if (iterator->removed_old &&
            old_keys[position].key.has_value()) {
            const Status insert_status =
                bindings[position].index->Insert(
                    old_keys[position].key.value(),
                    rid
                );

            if (!insert_status.ok() && rollback_status.ok()) {
                rollback_status = insert_status;
            }
        }
    }

    old_record.SetRecordID(rid);
    const Status heap_rollback =
        heap_file.UpdateRecord(old_record);

    if (!rollback_status.ok()) {
        return CombineFailure(
            status,
            rollback_status,
            "Update index rollback failed"
        );
    }

    if (!heap_rollback.ok()) {
        return CombineFailure(
            status,
            heap_rollback,
            "Update HeapFile rollback failed"
        );
    }

    return status;
}

Status TableStorage::DeleteRecord(
    const std::string& table_name,
    RecordID rid
) {
    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;
    std::vector<IndexBinding> bindings;

    Status status = LoadTable(
        table_name,
        &schema,
        &first_page_id,
        &bindings
    );
    if (!status.ok()) {
        return status;
    }

    status = ValidateRecordBelongsToTable(
        first_page_id,
        rid
    );
    if (!status.ok()) {
        return status;
    }

    HeapFile heap_file(bpm_, first_page_id);

    Record old_record;
    status = heap_file.GetRecord(
        rid,
        &old_record
    );
    if (!status.ok()) {
        return status;
    }

    std::vector<EncodedIndexKey> keys;
    status = EncodeIndexKeys(
        schema,
        old_record,
        bindings,
        &keys
    );
    if (!status.ok()) {
        return status;
    }

    std::vector<std::size_t> removed_positions;

    for (std::size_t position = 0;
         position < keys.size();
         ++position) {
        if (!keys[position].key.has_value()) {
            continue;
        }

        status = keys[position].binding->index->Remove(
            keys[position].key.value(),
            rid
        );

        if (!status.ok()) {
            const Status rollback =
                RollbackRemovedIndexEntries(
                    rid,
                    keys,
                    removed_positions
                );

            return CombineFailure(
                status,
                rollback,
                "Delete index rollback failed"
            );
        }

        removed_positions.push_back(position);
    }

    status = heap_file.DeleteRecord(rid);
    if (status.ok()) {
        return Status::OK();
    }

    const Status rollback =
        RollbackRemovedIndexEntries(
            rid,
            keys,
            removed_positions
        );

    return CombineFailure(
        status,
        rollback,
        "Delete HeapFile rollback failed"
    );
}

} // namespace minidbms
