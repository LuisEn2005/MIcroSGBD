#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "catalog/schema.h"
#include "metrics/query_stats.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"
#include "storage/slotted_page.h"
#include "storage/table_storage.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

struct BenchmarkCase {
    std::string name;
    std::string sql;
    ScanType expected_plan;
};

struct RunRecord {
    std::string experiment;
    std::size_t records{0};
    std::size_t pool_size{0};
    std::string query_type;
    std::string cache_state;
    int iteration{0};
    QueryStats stats;
};

struct Summary {
    double mean_ms{0.0};
    double median_ms{0.0};
    double min_ms{0.0};
    double max_ms{0.0};
    double stddev_ms{0.0};
    double avg_reads{0.0};
    double avg_writes{0.0};
    double avg_hits{0.0};
    double avg_misses{0.0};
    double avg_pages{0.0};
    double avg_examined{0.0};
    double avg_rows{0.0};
};

Status ParseSQL(
    const std::string& sql,
    std::unique_ptr<SQLStatement>* statement
) {
    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());
    return parser.Parse(statement);
}

Summary ComputeSummary(const std::vector<QueryStats>& runs) {
    assert(!runs.empty());

    std::vector<double> times;
    times.reserve(runs.size());

    Summary summary;
    for (const QueryStats& stats : runs) {
        times.push_back(stats.execution_time_ms);
        summary.avg_reads += stats.disk_reads;
        summary.avg_writes += stats.disk_writes;
        summary.avg_hits += stats.buffer_hits;
        summary.avg_misses += stats.buffer_misses;
        summary.avg_pages += stats.pages_scanned;
        summary.avg_examined += stats.records_examined;
        summary.avg_rows += stats.rows_returned;
    }

    const double count = static_cast<double>(runs.size());
    summary.mean_ms = std::accumulate(
        times.begin(), times.end(), 0.0
    ) / count;

    std::sort(times.begin(), times.end());
    const std::size_t middle = times.size() / 2;
    summary.median_ms = times.size() % 2 == 0
        ? (times[middle - 1] + times[middle]) / 2.0
        : times[middle];
    summary.min_ms = times.front();
    summary.max_ms = times.back();

    double squared_sum = 0.0;
    for (double value : times) {
        const double delta = value - summary.mean_ms;
        squared_sum += delta * delta;
    }
    summary.stddev_ms = std::sqrt(squared_sum / count);

    summary.avg_reads /= count;
    summary.avg_writes /= count;
    summary.avg_hits /= count;
    summary.avg_misses /= count;
    summary.avg_pages /= count;
    summary.avg_examined /= count;
    summary.avg_rows /= count;

    return summary;
}

void CreateDatabase(
    const std::filesystem::path& db_path,
    std::size_t record_count,
    std::size_t pool_size,
    bool with_index
) {
    std::filesystem::remove(db_path);

    DiskManager disk_manager(db_path.string());
    ClockReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());

    Schema schema({
        {"id", TypeId::INTEGER, 4},
        {"name", TypeId::VARCHAR, 30},
        {"age", TypeId::INTEGER, 4},
        {"active", TypeId::BOOLEAN, 1}
    });

    PageId first_page_id = INVALID_PAGE_ID;
    Page* page = bpm.NewPage(&first_page_id);
    assert(page != nullptr);

    SlottedPage slotted(page);
    assert(slotted.Init().ok());
    assert(bpm.UnpinPage(first_page_id, true));
    assert(catalog.CreateTable("users", schema, first_page_id).ok());

    if (with_index) {
        QueryExecutor executor(&catalog, &bpm);
        std::unique_ptr<SQLStatement> statement;
        assert(ParseSQL(
            "CREATE INDEX idx_users_id ON users(id);",
            &statement
        ).ok());
        assert(executor.Execute(*statement).ok());
    }

    TableStorage storage(&bpm, &catalog);
    for (std::size_t index = 1;
         index <= record_count;
         ++index) {
        RecordID rid;
        assert(storage.InsertRecord(
            "users",
            {
                static_cast<int32_t>(index),
                std::string{"User_"} + std::to_string(index),
                static_cast<int32_t>(18 + (index % 60)),
                index % 2 == 0
            },
            &rid
        ).ok());
    }

    assert(catalog.Flush().ok());
    bpm.FlushAllPages();
    assert(bpm.GetPinnedPageCount() == 0);
}

QueryStats RunColdQuery(
    const std::filesystem::path& db_path,
    std::size_t pool_size,
    const BenchmarkCase& benchmark_case
) {
    DiskManager disk_manager(db_path.string());
    ClockReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());
    QueryExecutor executor(&catalog, &bpm);

    std::unique_ptr<SQLStatement> statement;
    assert(ParseSQL(benchmark_case.sql, &statement).ok());

    QueryStats stats;
    assert(executor.Execute(*statement, &stats).ok());
    assert(stats.scan_type == benchmark_case.expected_plan);
    assert(bpm.GetPinnedPageCount() == 0);
    return stats;
}

std::vector<QueryStats> RunHotQueries(
    const std::filesystem::path& db_path,
    std::size_t pool_size,
    const BenchmarkCase& benchmark_case,
    int repetitions
) {
    DiskManager disk_manager(db_path.string());
    ClockReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);
    assert(catalog.GetInitializationStatus().ok());
    QueryExecutor executor(&catalog, &bpm);

    std::unique_ptr<SQLStatement> statement;
    assert(ParseSQL(benchmark_case.sql, &statement).ok());

    QueryStats warmup;
    assert(executor.Execute(*statement, &warmup).ok());

    std::vector<QueryStats> runs;
    runs.reserve(static_cast<std::size_t>(repetitions));
    for (int iteration = 1;
         iteration <= repetitions;
         ++iteration) {
        QueryStats stats;
        assert(executor.Execute(*statement, &stats).ok());
        assert(stats.scan_type == benchmark_case.expected_plan);
        runs.push_back(stats);
    }

    assert(bpm.GetPinnedPageCount() == 0);
    return runs;
}

void WriteRunHeader(std::ofstream& csv) {
    csv
        << "experiment,records,pool_size,query_type,cache_state,"
        << "iteration,plan,rows_returned,records_examined,"
        << "pages_scanned,buffer_hits,buffer_misses,disk_reads,"
        << "disk_writes,time_ms\n";
}

void WriteRun(std::ofstream& csv, const RunRecord& run) {
    csv
        << run.experiment << ','
        << run.records << ','
        << run.pool_size << ','
        << run.query_type << ','
        << run.cache_state << ','
        << run.iteration << ','
        << ScanTypeToString(run.stats.scan_type) << ','
        << run.stats.rows_returned << ','
        << run.stats.records_examined << ','
        << run.stats.pages_scanned << ','
        << run.stats.buffer_hits << ','
        << run.stats.buffer_misses << ','
        << run.stats.disk_reads << ','
        << run.stats.disk_writes << ','
        << std::fixed << std::setprecision(6)
        << run.stats.execution_time_ms << '\n';
}

void WriteSummaryHeader(std::ofstream& csv) {
    csv
        << "experiment,records,pool_size,query_type,cache_state,"
        << "plan,runs,mean_ms,median_ms,min_ms,max_ms,stddev_ms,"
        << "avg_rows_returned,avg_records_examined,avg_pages_scanned,"
        << "avg_buffer_hits,avg_buffer_misses,avg_disk_reads,"
        << "avg_disk_writes\n";
}

void WriteSummary(
    std::ofstream& csv,
    const std::string& experiment,
    std::size_t records,
    std::size_t pool_size,
    const std::string& query_type,
    const std::string& cache_state,
    const std::vector<QueryStats>& runs
) {
    const Summary summary = ComputeSummary(runs);
    csv
        << experiment << ','
        << records << ','
        << pool_size << ','
        << query_type << ','
        << cache_state << ','
        << ScanTypeToString(runs.front().scan_type) << ','
        << runs.size() << ','
        << std::fixed << std::setprecision(6)
        << summary.mean_ms << ','
        << summary.median_ms << ','
        << summary.min_ms << ','
        << summary.max_ms << ','
        << summary.stddev_ms << ','
        << summary.avg_rows << ','
        << summary.avg_examined << ','
        << summary.avg_pages << ','
        << summary.avg_hits << ','
        << summary.avg_misses << ','
        << summary.avg_reads << ','
        << summary.avg_writes << '\n';
}

} // namespace

int main(int argc, char** argv) {
    bool quick = false;
    for (int index = 1; index < argc; ++index) {
        if (std::string(argv[index]) == "--quick") {
            quick = true;
        }
    }

    const std::vector<std::size_t> volumes = quick
        ? std::vector<std::size_t>{1000, 5000}
        : std::vector<std::size_t>{1000, 10000, 50000};
    const std::vector<std::size_t> pool_sizes = {3, 10, 50};
    const int query_repetitions = quick ? 2 : 5;
    const int insert_repetitions = quick ? 1 : 3;

    const std::filesystem::path data_dir = "data";
    const std::filesystem::path results_dir = "results";
    std::filesystem::create_directories(data_dir);
    std::filesystem::create_directories(results_dir);

    std::ofstream run_csv(results_dir / "sprint6_final_results.csv");
    std::ofstream summary_csv(results_dir / "sprint6_final_summary.csv");
    assert(run_csv.is_open());
    assert(summary_csv.is_open());
    WriteRunHeader(run_csv);
    WriteSummaryHeader(summary_csv);

    std::cout
        << "Sprint 6 final benchmark suite\n"
        << "Mode: " << (quick ? "quick" : "full") << "\n";

    for (std::size_t record_count : volumes) {
        const std::filesystem::path db_path =
            data_dir /
            ("sprint6_benchmark_" +
             std::to_string(record_count) + ".db");

        std::cout
            << "Building indexed dataset: "
            << record_count << " records...\n";
        CreateDatabase(db_path, record_count, 50, true);

        const int32_t target = static_cast<int32_t>(
            std::max<std::size_t>(1, record_count * 3 / 4)
        );

        const std::vector<BenchmarkCase> cases = {
            {
                "equality_indexed",
                "SELECT * FROM users WHERE id = " +
                    std::to_string(target) + ";",
                ScanType::HASH_INDEX
            },
            {
                "equality_unindexed",
                "SELECT * FROM users WHERE name = 'User_" +
                    std::to_string(target) + "';",
                ScanType::SEQUENTIAL
            },
            {
                "range_unindexed",
                "SELECT * FROM users WHERE age > 40;",
                ScanType::SEQUENTIAL
            },
            {
                "low_selectivity",
                "SELECT * FROM users WHERE active = true;",
                ScanType::SEQUENTIAL
            },
            {
                "indexed_not_found",
                "SELECT * FROM users WHERE id = " +
                    std::to_string(record_count + 123) + ";",
                ScanType::HASH_INDEX
            }
        };

        for (std::size_t pool_size : pool_sizes) {
            for (const BenchmarkCase& benchmark_case : cases) {
                std::vector<QueryStats> cold_runs;
                cold_runs.reserve(
                    static_cast<std::size_t>(query_repetitions)
                );

                for (int iteration = 1;
                     iteration <= query_repetitions;
                     ++iteration) {
                    QueryStats stats = RunColdQuery(
                        db_path,
                        pool_size,
                        benchmark_case
                    );
                    cold_runs.push_back(stats);
                    WriteRun(run_csv, {
                        "query_comparison",
                        record_count,
                        pool_size,
                        benchmark_case.name,
                        "cold",
                        iteration,
                        stats
                    });
                }

                WriteSummary(
                    summary_csv,
                    "query_comparison",
                    record_count,
                    pool_size,
                    benchmark_case.name,
                    "cold",
                    cold_runs
                );

                std::vector<QueryStats> hot_runs = RunHotQueries(
                    db_path,
                    pool_size,
                    benchmark_case,
                    query_repetitions
                );

                for (int iteration = 1;
                     iteration <= query_repetitions;
                     ++iteration) {
                    WriteRun(run_csv, {
                        "query_comparison",
                        record_count,
                        pool_size,
                        benchmark_case.name,
                        "hot",
                        iteration,
                        hot_runs[static_cast<std::size_t>(iteration - 1)]
                    });
                }

                WriteSummary(
                    summary_csv,
                    "query_comparison",
                    record_count,
                    pool_size,
                    benchmark_case.name,
                    "hot",
                    hot_runs
                );
            }
        }
    }

    // Costo de inserción con y sin mantenimiento de índice.
    for (std::size_t record_count : volumes) {
        for (bool with_index : {false, true}) {
            std::vector<QueryStats> synthetic_runs;

            for (int iteration = 1;
                 iteration <= insert_repetitions;
                 ++iteration) {
                const std::filesystem::path db_path =
                    data_dir /
                    (std::string{"sprint6_insert_"} +
                     (with_index ? "index_" : "noindex_") +
                     std::to_string(record_count) + "_" +
                     std::to_string(iteration) + ".db");

                const auto start = std::chrono::steady_clock::now();
                CreateDatabase(db_path, record_count, 50, with_index);
                const auto finish = std::chrono::steady_clock::now();

                QueryStats stats;
                stats.execution_time_ms =
                    std::chrono::duration<double, std::milli>(
                        finish - start
                    ).count();
                stats.rows_affected = record_count;
                synthetic_runs.push_back(stats);

                WriteRun(run_csv, {
                    "bulk_insert",
                    record_count,
                    50,
                    with_index
                        ? "insert_with_index"
                        : "insert_without_index",
                    "cold",
                    iteration,
                    stats
                });
            }

            WriteSummary(
                summary_csv,
                "bulk_insert",
                record_count,
                50,
                with_index
                    ? "insert_with_index"
                    : "insert_without_index",
                "cold",
                synthetic_runs
            );
        }
    }

    run_csv.close();
    summary_csv.close();

    std::cout
        << "Benchmark completed.\n"
        << "Per-run CSV: results/sprint6_final_results.csv\n"
        << "Summary CSV: results/sprint6_final_summary.csv\n";

    return 0;
}
