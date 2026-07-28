#ifndef MINI_DBMS_CATALOG_MANAGER_H
#define MINI_DBMS_CATALOG_MANAGER_H

#include "../catalog/schema.h"
#include "../common/types.h"
#include "../common/status.h"
#include "../index/hash_index.h"
#include <unordered_map>

namespace minidbms {
    class CatalogManager {
        public:
            CatalogManager() = default;

            Status CreateTable(const std::string& table_name, const Schema& schema, PageId first_page_id);
            Status GetTableSchema(const std::string& table_name, Schema* schema) const;
            Status GetTableFirstPageId(const std::string& table_name, PageId* first_page_id) const;
            Status CreateIndex(const std::string& index_name, const std::string& table_name, const std::string& column_name, HashIndex* index_ptr);
            bool HasIndex(const std::string& table_name, const std::string& column_name) const;
            HashIndex* GetIndex(const std::string& table_name, const std::string& column_name) const;
        private:
            struct TableMetaData {
                std::string name;
                Schema schema;
                PageId first_page_id;
            };

            std::unordered_map<std::string, TableMetaData> tables_;
            std::unordered_map<std::string, HashIndex*> indexes_;
    };

}

#endif // MINI_DBMS_CATALOG_MANAGER_H
