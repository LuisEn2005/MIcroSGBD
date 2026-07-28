#ifndef MINI_DBMS_QUERY_STATS_H
#define MINI_DBMS_QUERY_STATS_H

#include <cstdint>

namespace minidbms {

struct QueryStats {
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
