#include "query/executor.h"

#include "query/operators/filter_operator.h"
#include "query/operators/projection_operator.h"
#include "query/operators/seq_scan_operator.h"
#include "storage/heap_file.h"

#include <chrono>
#include <vector>

namespace minidbms {

Status QueryExecutor::Execute(
    const SQLStatement& stmt,
    QueryStats* stats
) {
    QueryStats local_stats;
    QueryStats* active_stats = stats != nullptr ? stats : &local_stats;
    active_stats->Reset();

    if (catalog_ == nullptr || bpm_ == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Executor dependencies cannot be null"
        );
    }

    bpm_->ResetStats();
    const auto start_time = std::chrono::steady_clock::now();

    Status status = Status::OK();

    if (stmt.GetType() != StatementType::SELECT) {
        status = Status(
            StatusCode::INVALID_ARGUMENT,
            "Only SELECT is implemented in this sprint"
        );
    } else {
        const auto* select_stmt =
            dynamic_cast<const SelectStatement*>(&stmt);

        if (select_stmt == nullptr) {
            status = Status(
                StatusCode::INVALID_ARGUMENT,
                "Invalid SELECT statement"
            );
        } else {
            std::unique_ptr<AbstractOperator> plan;
            status = BuildPlan(*select_stmt, active_stats, &plan);

            if (status.ok()) {
                status = plan->Open();
            }

            if (status.ok()) {
                Record record;
                RecordID rid;

                while (plan->Next(&record, &rid)) {
                    ++active_stats->rows_returned;
                }

                const Status close_status = plan->Close();
                if (!close_status.ok()) {
                    status = close_status;
                }
            }
        }
    }

    active_stats->buffer_hits = bpm_->GetBufferHits();
    active_stats->buffer_misses = bpm_->GetBufferMisses();
    active_stats->disk_reads = bpm_->GetDiskReads();
    active_stats->disk_writes = bpm_->GetDiskWrites();

    const auto end_time = std::chrono::steady_clock::now();
    active_stats->execution_time_ms =
        std::chrono::duration<double, std::milli>(end_time - start_time).count();

    return status;
}

Status QueryExecutor::BuildPlan(
    const SelectStatement& select_stmt,
    QueryStats* stats,
    std::unique_ptr<AbstractOperator>* plan
) {
    if (plan == nullptr) {
        return Status(
            StatusCode::INVALID_ARGUMENT,
            "Plan output cannot be null"
        );
    }

    Schema schema({});
    PageId first_page_id = INVALID_PAGE_ID;

    Status status = catalog_->GetTableSchema(
        select_stmt.table_name,
        &schema
    );
    if (!status.ok()) {
        return status;
    }

    status = catalog_->GetTableFirstPageId(
        select_stmt.table_name,
        &first_page_id
    );
    if (!status.ok()) {
        return status;
    }

    auto heap_file = std::make_unique<HeapFile>(
        bpm_,
        first_page_id
    );

    std::unique_ptr<AbstractOperator> current_plan =
        std::make_unique<SeqScanOperator>(
            std::move(heap_file),
            stats
        );

    for (const Condition& condition : select_stmt.conditions) {
        current_plan = std::make_unique<FilterOperator>(
            std::move(current_plan),
            schema,
            condition
        );
    }

    if (!select_stmt.fields.empty() &&
        select_stmt.fields.front() != "*") {
        std::vector<uint32_t> column_indices;
        const auto& columns = schema.GetColumns();

        for (const std::string& field_name : select_stmt.fields) {
            bool found = false;

            for (uint32_t index = 0; index < columns.size(); ++index) {
                if (columns[index].name == field_name) {
                    column_indices.push_back(index);
                    found = true;
                    break;
                }
            }

            if (!found) {
                return Status(
                    StatusCode::INVALID_ARGUMENT,
                    "Projection column does not exist: " + field_name
                );
            }
        }

        current_plan = std::make_unique<ProjectionOperator>(
            std::move(current_plan),
            schema,
            std::move(column_indices)
        );
    }

    *plan = std::move(current_plan);
    return Status::OK();
}

} // namespace minidbms
