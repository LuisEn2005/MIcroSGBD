#ifndef MINI_DBMS_CATALOG_MANAGER_H
#define MINI_DBMS_CATALOG_MANAGER_H

#include "buffer/buffer_pool_manager.h"
#include "catalog/schema.h"
#include "common/status.h"
#include "common/types.h"
#include "index/hash_index.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace minidbms {

struct CatalogIndexInfo {
    std::string index_name;
    std::string table_name;
    std::string column_name;
    PageId header_page_id{INVALID_PAGE_ID};
};

class CatalogManager {
public:
    CatalogManager() = default;
    explicit CatalogManager(BufferPoolManager* bpm);

    Status Load();
    Status Flush();

    const Status& GetInitializationStatus() const {
        return initialization_status_;
    }

    Status CreateTable(
        const std::string& table_name,
        const Schema& schema,
        PageId first_page_id
    );

    Status GetTableSchema(
        const std::string& table_name,
        Schema* schema
    ) const;

    Status GetTableFirstPageId(
        const std::string& table_name,
        PageId* first_page_id
    ) const;

    Status CreateIndex(
        const std::string& index_name,
        const std::string& table_name,
        const std::string& column_name,
        std::unique_ptr<HashIndex> index
    );

    bool HasIndex(
        const std::string& table_name,
        const std::string& column_name
    ) const;

    bool HasIndexName(const std::string& index_name) const;

    HashIndex* GetIndex(
        const std::string& table_name,
        const std::string& column_name
    ) const;

    Status GetIndexHeaderPageId(
        const std::string& table_name,
        const std::string& column_name,
        PageId* header_page_id
    ) const;

    Status GetTableIndexes(
        const std::string& table_name,
        std::vector<CatalogIndexInfo>* indexes
    ) const;

private:
    struct TableMetadata {
        std::string name;
        Schema schema;
        PageId first_page_id;
    };

    struct IndexMetadata {
        std::string name;
        std::string table_name;
        std::string column_name;
        PageId header_page_id;
        std::unique_ptr<HashIndex> index;
    };

    static std::string MakeIndexKey(
        const std::string& table_name,
        const std::string& column_name
    );

    Status Persist();

    BufferPoolManager* bpm_{nullptr};
    Status initialization_status_{};

    std::unordered_map<std::string, TableMetadata> tables_;
    std::unordered_map<std::string, IndexMetadata> indexes_;
    std::unordered_map<std::string, std::string> index_names_;
};

} // namespace minidbms

#endif // MINI_DBMS_CATALOG_MANAGER_H
