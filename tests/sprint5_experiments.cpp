#include "buffer/buffer_pool_manager.h"
#include "buffer/clock_replacer.h"
#include "catalog/catalog_manager.h"
#include "metrics/query_stats.h"
#include "query/executor.h"
#include "query/parser.h"
#include "query/tokenizer.h"
#include "storage/disk_manager.h"
#include "storage/record_codec.h"
#include "storage/slotted_page.h"
#include "storage/table_storage.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <numeric>
#include <string>
#include <vector>

using namespace minidbms;

namespace {

struct StatsSummary {
    double mean_time_ms{0.0};
    double median_time_ms{0.0};
    double min_time_ms{0.0};
    double max_time_ms{0.0};
    double stddev_time_ms{0.0};

    double avg_disk_reads{0.0};
    double avg_disk_writes{0.0};
    double avg_buffer_hits{0.0};
    double avg_buffer_misses{0.0};
    double avg_pages_scanned{0.0};
    double avg_records_examined{0.0};
};

StatsSummary ComputeStats(const std::vector<QueryStats>& runs) {
    StatsSummary summary;
    if (runs.empty()) return summary;

    std::vector<double> times;
    times.reserve(runs.size());

    double sum_reads = 0, sum_writes = 0, sum_hits = 0, sum_misses = 0, sum_pages = 0, sum_recs = 0;

    for (const auto& r : runs) {
        times.push_back(r.execution_time_ms);
        sum_reads += static_cast<double>(r.disk_reads);
        sum_writes += static_cast<double>(r.disk_writes);
        sum_hits += static_cast<double>(r.buffer_hits);
        sum_misses += static_cast<double>(r.buffer_misses);
        sum_pages += static_cast<double>(r.pages_scanned);
        sum_recs += static_cast<double>(r.records_examined);
    }

    const std::size_t n = runs.size();
    summary.avg_disk_reads = sum_reads / static_cast<double>(n);
    summary.avg_disk_writes = sum_writes / static_cast<double>(n);
    summary.avg_buffer_hits = sum_hits / static_cast<double>(n);
    summary.avg_buffer_misses = sum_misses / static_cast<double>(n);
    summary.avg_pages_scanned = sum_pages / static_cast<double>(n);
    summary.avg_records_examined = sum_recs / static_cast<double>(n);

    // Mean
    const double sum = std::accumulate(times.begin(), times.end(), 0.0);
    summary.mean_time_ms = sum / static_cast<double>(n);

    // Min & Max
    auto minmax = std::minmax_element(times.begin(), times.end());
    summary.min_time_ms = *minmax.first;
    summary.max_time_ms = *minmax.second;

    // Median
    std::vector<double> sorted_times = times;
    std::sort(sorted_times.begin(), sorted_times.end());
    if (n % 2 == 0) {
        summary.median_time_ms = (sorted_times[n / 2 - 1] + sorted_times[n / 2]) / 2.0;
    } else {
        summary.median_time_ms = sorted_times[n / 2];
    }

    // Standard Deviation
    double sq_diff_sum = 0.0;
    for (double t : times) {
        sq_diff_sum += (t - summary.mean_time_ms) * (t - summary.mean_time_ms);
    }
    summary.stddev_time_ms = std::sqrt(sq_diff_sum / static_cast<double>(n));

    return summary;
}

void BuildDatabase(
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
    slotted.Init();
    bpm.UnpinPage(first_page_id, true);

    assert(catalog.CreateTable("users", schema, first_page_id).ok());

    TableStorage table_storage(&bpm, &catalog);

    for (std::size_t i = 1; i <= record_count; ++i) {
        std::string name = "User_" + std::to_string(i);
        int32_t age = static_cast<int32_t>(18 + (i % 60));
        bool active = (i % 2 == 0);

        std::vector<FieldValue> values = {
            static_cast<int32_t>(i),
            name,
            age,
            active
        };

        RecordID rid;
        assert(table_storage.InsertRecord("users", values, &rid).ok());
    }

    if (with_index) {
        QueryExecutor executor(&catalog, &bpm);
        Tokenizer tokenizer("CREATE INDEX idx_users_id ON users(id);");
        Parser parser(tokenizer.Tokenize());
        std::unique_ptr<SQLStatement> stmt;
        assert(parser.Parse(&stmt).ok());
        assert(executor.Execute(*stmt, nullptr).ok());
    }

    assert(catalog.Flush().ok());
    bpm.FlushAllPages();
}

QueryStats RunQuery(
    const std::filesystem::path& db_path,
    std::size_t pool_size,
    const std::string& sql
) {
    DiskManager disk_manager(db_path.string());
    ClockReplacer replacer(pool_size);
    BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
    CatalogManager catalog(&bpm);
    QueryExecutor executor(&catalog, &bpm);

    Tokenizer tokenizer(sql);
    Parser parser(tokenizer.Tokenize());
    std::unique_ptr<SQLStatement> stmt;
    assert(parser.Parse(&stmt).ok());

    QueryStats stats;
    assert(executor.Execute(*stmt, &stats).ok());
    return stats;
}

} // namespace

int main() {
    const std::filesystem::path data_dir = "data";
    const std::filesystem::path results_dir = "results";
    std::filesystem::create_directories(data_dir);
    std::filesystem::create_directories(results_dir);

    const std::filesystem::path db_path = data_dir / "sprint5_experiments.db";

    std::ofstream summary_csv(results_dir / "sprint5_experiments_summary.csv");
    summary_csv << "experiment,records,pool_size,query_type,cache_state,plan,runs,mean_ms,median_ms,min_ms,max_ms,stddev_ms,avg_reads,avg_writes,avg_hits,avg_misses,avg_pages_scanned,avg_recs_examined\n";

    std::cout << "=====================================================\n";
    std::cout << "  MINI-SGBD SPRINT 5: COMPREHENSIVE EXPERIMENTS SUITE\n";
    std::cout << "=====================================================\n\n";

    constexpr int RUN_REPETITIONS = 10;

    // -----------------------------------------------------------------
    // EXPERIMENTO 1 & 2 & 3: Comparación IndexScan vs SeqScan, Pool Sizes & Caché
    // -----------------------------------------------------------------
    const std::vector<std::size_t> test_volumes = {1000, 10000};
    const std::vector<std::size_t> pool_sizes = {4, 16, 64, 256};

    for (std::size_t vol : test_volumes) {
        std::cout << "--> Running Experiments for Volume: " << vol << " records...\n";
        BuildDatabase(db_path, vol, 16, true);

        for (std::size_t pool_size : pool_sizes) {
            // A) IndexScan - Cold Cache
            std::vector<QueryStats> index_cold_runs;
            for (int r = 0; r < RUN_REPETITIONS; ++r) {
                index_cold_runs.push_back(RunQuery(db_path, pool_size, "SELECT * FROM users WHERE id = 500;"));
            }
            StatsSummary s_idx_cold = ComputeStats(index_cold_runs);
            summary_csv << "exp1_index_vs_seqscan," << vol << "," << pool_size << ",equality_indexed,cold,"
                        << ScanTypeToString(index_cold_runs[0].scan_type) << "," << RUN_REPETITIONS << ","
                        << s_idx_cold.mean_time_ms << "," << s_idx_cold.median_time_ms << ","
                        << s_idx_cold.min_time_ms << "," << s_idx_cold.max_time_ms << ","
                        << s_idx_cold.stddev_time_ms << "," << s_idx_cold.avg_disk_reads << ","
                        << s_idx_cold.avg_disk_writes << "," << s_idx_cold.avg_buffer_hits << ","
                        << s_idx_cold.avg_buffer_misses << "," << s_idx_cold.avg_pages_scanned << ","
                        << s_idx_cold.avg_records_examined << "\n";

            // B) IndexScan - Hot Cache
            {
                DiskManager disk_manager(db_path.string());
                ClockReplacer replacer(pool_size);
                BufferPoolManager bpm(pool_size, &disk_manager, &replacer);
                CatalogManager catalog(&bpm);
                QueryExecutor executor(&catalog, &bpm);

                Tokenizer tokenizer("SELECT * FROM users WHERE id = 500;");
                Parser parser(tokenizer.Tokenize());
                std::unique_ptr<SQLStatement> stmt;
                assert(parser.Parse(&stmt).ok());

                // Warm up
                QueryStats dummy;
                executor.Execute(*stmt, &dummy);

                std::vector<QueryStats> index_hot_runs;
                for (int r = 0; r < RUN_REPETITIONS; ++r) {
                    QueryStats stats;
                    executor.Execute(*stmt, &stats);
                    index_hot_runs.push_back(stats);
                }
                StatsSummary s_idx_hot = ComputeStats(index_hot_runs);
                summary_csv << "exp3_cold_vs_hot," << vol << "," << pool_size << ",equality_indexed,hot,"
                            << ScanTypeToString(index_hot_runs[0].scan_type) << "," << RUN_REPETITIONS << ","
                            << s_idx_hot.mean_time_ms << "," << s_idx_hot.median_time_ms << ","
                            << s_idx_hot.min_time_ms << "," << s_idx_hot.max_time_ms << ","
                            << s_idx_hot.stddev_time_ms << "," << s_idx_hot.avg_disk_reads << ","
                            << s_idx_hot.avg_disk_writes << "," << s_idx_hot.avg_buffer_hits << ","
                            << s_idx_hot.avg_buffer_misses << "," << s_idx_hot.avg_pages_scanned << ","
                            << s_idx_hot.avg_records_examined << "\n";
            }

            // C) SeqScan - Cold Cache
            std::vector<QueryStats> seq_cold_runs;
            for (int r = 0; r < RUN_REPETITIONS; ++r) {
                seq_cold_runs.push_back(RunQuery(db_path, pool_size, "SELECT * FROM users WHERE name = 'User_500';"));
            }
            StatsSummary s_seq_cold = ComputeStats(seq_cold_runs);
            summary_csv << "exp1_index_vs_seqscan," << vol << "," << pool_size << ",equality_unindexed,cold,"
                        << ScanTypeToString(seq_cold_runs[0].scan_type) << "," << RUN_REPETITIONS << ","
                        << s_seq_cold.mean_time_ms << "," << s_seq_cold.median_time_ms << ","
                        << s_seq_cold.min_time_ms << "," << s_seq_cold.max_time_ms << ","
                        << s_seq_cold.stddev_time_ms << "," << s_seq_cold.avg_disk_reads << ","
                        << s_seq_cold.avg_disk_writes << "," << s_seq_cold.avg_buffer_hits << ","
                        << s_seq_cold.avg_buffer_misses << "," << s_seq_cold.avg_pages_scanned << ","
                        << s_seq_cold.avg_records_examined << "\n";
        }
    }

    // -----------------------------------------------------------------
    // EXPERIMENTO 4: Inserción Masiva (Con vs Sin Índice)
    // -----------------------------------------------------------------
    std::cout << "--> Running Experiment 4: Bulk Insertion Comparison...\n";
    const std::vector<std::size_t> insert_volumes = {1000, 5000};

    for (std::size_t vol : insert_volumes) {
        // Sin Índice
        {
            const std::filesystem::path no_idx_path = data_dir / "bulk_no_index.db";
            const auto start_time = std::chrono::steady_clock::now();
            BuildDatabase(no_idx_path, vol, 64, false);
            const auto end_time = std::chrono::steady_clock::now();
            double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

            summary_csv << "exp4_bulk_insert," << vol << ",64,bulk_insert_no_index,cold,None,1,"
                        << duration_ms << "," << duration_ms << "," << duration_ms << "," << duration_ms << ",0.0,0,0,0,0,0," << vol << "\n";
        }

        // Con Índice
        {
            const std::filesystem::path idx_path = data_dir / "bulk_with_index.db";
            const auto start_time = std::chrono::steady_clock::now();
            BuildDatabase(idx_path, vol, 64, true);
            const auto end_time = std::chrono::steady_clock::now();
            double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

            summary_csv << "exp4_bulk_insert," << vol << ",64,bulk_insert_with_index,cold,None,1,"
                        << duration_ms << "," << duration_ms << "," << duration_ms << "," << duration_ms << ",0.0,0,0,0,0,0," << vol << "\n";
        }
    }

    summary_csv.close();
    std::cout << "\n=====================================================\n";
    std::cout << "  SPRINT 5 EXPERIMENTAL SUITE COMPLETED SUCCESSFULLY!\n";
    std::cout << "  Summary Report: results/sprint5_experiments_summary.csv\n";
    std::cout << "=====================================================\n";

    return 0;
}
