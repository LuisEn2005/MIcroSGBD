#include "../../include/query/executor.h"
#include "../../include/query/operators/seq_scan_operator.h"
#include "../../include/query/operators/index_scan_operator.h"
#include "../../include/query/operators/filter_operator.h"
#include "../../include/query/operators/projection_operator.h"
#include "../../include/storage/heap_file.h"
#include <chrono>

namespace minidbms {
    Status QueryExecutor::Execute(const SQLStatement& stmt, QueryStats* stats) {
        if (stats) {
            stats->Reset();
        }

        auto start_time = std::chrono::high_resolution_clock::now();

        Status status = Status::OK();

        if (stmt.GetType() == StatementType::SELECT) {
            auto* select_stmt = dynamic_cast<const SelectStatement*>(&stmt);
            if (select_stmt) {
                auto plan = BuildPlan(*select_stmt);
                if (!plan) {
                    return Status(StatusCode::INVALID_ARGUMENT, "Error al construir el plan de ejecucion");
                }

                status = plan->Open();

                if (status.ok()) {
                    Record record;
                    RecordID rid;

                    while (plan->Next(&record, &rid)) {
                        if (stats) {
                            stats->records_examined++;
                        }
                    }

                    plan->Close();
                }
            }
        }

        if (stats) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<
                std::chrono::microseconds
                >(end_time - start_time);
            stats->execution_time_ms = duration.count() / 1000.0;
        }

        if (stmt.GetType() == StatementType::CREATE_INDEX) {
            auto* create_idx_stmt = dynamic_cast<const CreateIndexStatement*>(&stmt);
            if (create_idx_stmt) {
                PageId first_page_id = INVALID_PAGE_ID;
                status = catalog_->GetTableFirstPageId(create_idx_stmt->table_name, &first_page_id);
                if (!status.ok()) return status;

                Schema schema({});
                status = catalog_->GetTableSchema(create_idx_stmt->table_name, &schema);
                if (!status.ok()) return status;

                int target_col_idx = -1;
                const auto& cols = schema.GetColumns();
                for (size_t i = 0; i < cols.size(); ++i) {
                    if (cols[i].name == create_idx_stmt->column_name) {
                        target_col_idx = static_cast<int>(i);
                        break;
                    }
                }

                if (target_col_idx == -1) {
                    return Status(StatusCode::INVALID_ARGUMENT, "Columna no encontrada para indice");
                }

                PageId index_header_page_id = INVALID_PAGE_ID;
                Page* header_page = bpm_->NewPage(&index_header_page_id);
                if (!header_page) {
                    return Status(StatusCode::OUT_OF_MEMORY, "No hay paginas para el indice");
                }
                bpm_->UnpinPage(index_header_page_id, true);

                auto index = new HashIndex(bpm_, index_header_page_id);

                HeapFile heap_file(bpm_, first_page_id);
                SeqScanOperator scan(&heap_file);

                if (scan.Open().ok()) {
                    Record record;
                    RecordID rid;
                    while (scan.Next(&record, &rid)) {
                        const char* data = record.GetData();
                        int32_t val = *reinterpret_cast<const int32_t*>(data + (target_col_idx * sizeof(int32_t)));
                        std::string key = std::to_string(val);
                        index->Insert(key, rid);
                    }
                    scan.Close();
                }

                status = catalog_->CreateIndex(
                        create_idx_stmt->index_name,
                        create_idx_stmt->table_name,
                        create_idx_stmt->column_name,
                        index
                        );
            }
        }
        return Status::OK();
    }

    std::unique_ptr<AbstractOperator> QueryExecutor::BuildPlan(
            const SelectStatement& select_stmt
            ) {
        Schema schema({});
        PageId first_page_id = INVALID_PAGE_ID;

        Status status = catalog_->GetTableSchema(
                select_stmt.table_name, 
                &schema
                );

        if (!status.ok()) {
            return nullptr;
        }

        status = catalog_->GetTableFirstPageId(
                select_stmt.table_name,
                &first_page_id
                );

        if (!status.ok()) {
            return nullptr;
        }

        auto heap_file = std::make_unique<HeapFile>(
                bpm_, 
                first_page_id
                );

        std::unique_ptr<AbstractOperator> plan = nullptr;
        int index_cond_idx = -1;
        std::string search_key = "";

        for (size_t i = 0; i < select_stmt.conditions.size(); ++i) {
            const auto& cond = select_stmt.conditions[i];
            if (cond.op == "=" || cond.op == "==") {
                if (catalog_->HasIndex(select_stmt.table_name, cond.column)) {
                    index_cond_idx = static_cast<int>(i);
                    search_key = cond.value;
                    break;
                }
            }
        }

        if (index_cond_idx != -1) {
            const auto& cond = select_stmt.conditions[index_cond_idx];
            HashIndex* index = catalog_->GetIndex(select_stmt.table_name, cond.column);

            plan = std::make_unique<IndexScanOperator>(
                    index,
                    heap_file.release(),
                    search_key
                    );

            for (size_t i = 0; i < select_stmt.conditions.size(); ++i) {
                if (static_cast<int>(i) == index_cond_idx) continue;
                plan = std::make_unique<FilterOperator>(
                        std::move(plan),
                        select_stmt.conditions[i]
                        );
            }
        } else {
            auto scan = std::make_unique<SeqScanOperator>(
                    heap_file.release()
                    );
            plan = std::move(scan);

            if (!select_stmt.conditions.empty()) {
                for (const auto& condition : select_stmt.conditions) {
                    auto filter = std::make_unique<FilterOperator>(
                            std::move(plan),
                            condition
                            );
                    plan = std::move(filter);
                }
            }
        }

        if (!select_stmt.fields.empty() && select_stmt.fields[0] != "*") {
            std::vector<uint32_t> col_indices;
            const auto& columns = schema.GetColumns();

            for (const auto& field_name : select_stmt.fields) {
                for (uint32_t i = 0; i < columns.size(); ++i) {
                    if (columns[i].name == field_name) {
                        col_indices.push_back(i);
                        break;
                    }
                }
            }

            if (!col_indices.empty()) {
                auto projection = std::make_unique<ProjectionOperator>(
                        std::move(plan),
                        col_indices
                        );
                plan = std::move(projection);
            }
        }

        return plan;
    }

} // namespace minidbms
