#include "../../include/query/executor.h"
#include "../../include/query/operators/seq_scan_operator.h"
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

    return status;
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

    auto scan = std::make_unique<SeqScanOperator>(
        heap_file.release()
    );

    std::unique_ptr<AbstractOperator> plan = std::move(scan);

    if (!select_stmt.conditions.empty()) {
        for (const auto& condition : select_stmt.conditions) {
            auto filter = std::make_unique<FilterOperator>(
                std::move(plan),
                condition
            );
            plan = std::move(filter);
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
