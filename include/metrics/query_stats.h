#ifndef MINI_DBMS_QUERY_STATS_H
#define MINI_DBMS_QUERY_STATS_H

#include <cstdint>
#include <string>

namespace minidbms {

enum class ScanType {
    NONE,
    SEQUENTIAL,
    HASH_INDEX
};

inline std::string ScanTypeToString(ScanType scan_type) {
    switch (scan_type) {
        case ScanType::SEQUENTIAL:
            return "SeqScan";
        case ScanType::HASH_INDEX:
            return "IndexScan";
        case ScanType::NONE:
        default:
            return "None";
    }
}

struct QueryStats {
    ScanType scan_type{ScanType::NONE};
    uint64_t buffer_hits{0};
    uint64_t buffer_misses{0};
    uint64_t disk_reads{0};
    uint64_t disk_writes{0};
    uint64_t pages_scanned{0};
    uint64_t records_examined{0};
    uint64_t rows_returned{0};
    double execution_time_ms{0.0};

    void Reset() {
        *this = QueryStats();
    }
};

} // namespace minidbms

#endif // MINI_DBMS_QUERY_STATS_H
