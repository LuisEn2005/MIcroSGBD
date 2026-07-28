#include "../../include/catalog/catalog_manager.h"

namespace minidbms {
    Status CatalogManager::CreateTable(const std::string& table_name, const Schema& schema, PageId first_page_id) {
        if (tables_.find(table_name) != tables_.end()) {
            return Status(StatusCode::INVALID_ARGUMENT, "La tabla ya existe en el catalogo");
        }

        tables_.emplace(table_name, TableMetaData{table_name, schema, first_page_id});
        return Status::OK();
    }

    Status CatalogManager::GetTableSchema(const std::string& table_name, Schema* schema) const {
        auto it = tables_.find(table_name);
        if (it == tables_.end()) {
            return Status(StatusCode::NOT_FOUND, "Tabla no encontrada en el catalogo");
        }

        if (schema) {
            *schema = it->second.schema;
        }
        return Status::OK();
    }

    Status CatalogManager::GetTableFirstPageId(const std::string& table_name, PageId* first_page_id) const {
        auto it = tables_.find(table_name);
        if (it == tables_.end()) {
            return Status(StatusCode::NOT_FOUND, "Tabla no encontrada en el catalogo");
        }

        if (first_page_id) {
            *first_page_id = it->second.first_page_id;
        }
        return Status::OK();
    }

    Status CatalogManager::CreateIndex(const std::string& index_name, const std::string& table_name, const std::string& column_name, HashIndex* index_ptr) {
        (void)index_name;
        std::string key = table_name + "." + column_name;
        indexes_[key] = index_ptr;
        return Status::OK();
    }

    bool CatalogManager::HasIndex(const std::string& table_name, const std::string& column_name) const {
        std::string key = table_name + "." + column_name;
        return indexes_.find(key) != indexes_.end();
    }

    HashIndex* CatalogManager::GetIndex(const std::string& table_name, const std::string& column_name) const {
        std::string key = table_name + "." + column_name;
        auto it = indexes_.find(key);
        if (it != indexes_.end()) {
            return it->second;
        }
        return nullptr;
    }
} // namespace minidbms
