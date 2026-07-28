#ifndef MINI_DBMS_TABLE_STORAGE_H
#define MINI_DBMS_TABLE_STORAGE_H

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog_manager.h"
#include "common/status.h"
#include "common/types.h"
#include "storage/record.h"
#include "storage/record_codec.h"

#include <optional>
#include <string>
#include <vector>

namespace minidbms {

// Capa de mutaciones de una tabla.
//
// Su responsabilidad es mantener sincronizados:
//   HeapFile <-> HashIndex
//
// El parser y el QueryExecutor pueden utilizar esta clase sin conocer
// detalles físicos de páginas, slots o buckets.
class TableStorage {
public:
    TableStorage(
        BufferPoolManager* buffer_pool_manager,
        CatalogManager* catalog_manager
    );

    Status InsertRecord(
        const std::string& table_name,
        const std::vector<FieldValue>& values,
        RecordID* rid
    );

    Status GetRecord(
        const std::string& table_name,
        RecordID rid,
        Record* record
    );

    Status UpdateRecord(
        const std::string& table_name,
        RecordID rid,
        const std::vector<FieldValue>& values
    );

    Status DeleteRecord(
        const std::string& table_name,
        RecordID rid
    );

private:
    struct IndexBinding {
        std::string index_name;
        std::string column_name;
        uint32_t column_index{0};
        HashIndex* index{nullptr};
    };

    struct EncodedIndexKey {
        const IndexBinding* binding{nullptr};
        std::optional<std::string> key;
    };

    Status ValidateDependencies() const;

    Status LoadTable(
        const std::string& table_name,
        Schema* schema,
        PageId* first_page_id,
        std::vector<IndexBinding>* bindings
    ) const;

    Status ValidateRecordBelongsToTable(
        PageId first_page_id,
        RecordID rid
    ) const;

    Status EncodeIndexKeys(
        const Schema& schema,
        const Record& record,
        const std::vector<IndexBinding>& bindings,
        std::vector<EncodedIndexKey>* keys
    ) const;

    Status RollbackInsertedIndexEntries(
        RecordID rid,
        const std::vector<EncodedIndexKey>& keys,
        std::size_t inserted_count
    ) const;

    Status RollbackRemovedIndexEntries(
        RecordID rid,
        const std::vector<EncodedIndexKey>& keys,
        const std::vector<std::size_t>& removed_positions
    ) const;

    BufferPoolManager* bpm_;
    CatalogManager* catalog_;
};

} // namespace minidbms

#endif // MINI_DBMS_TABLE_STORAGE_H
