#ifndef MINI_DBMS_QUERY_STATS_H
#define MINI_DBMS_QUERY_STATS_H

#include <cstdint>

namespace minidbms {
  struct QueryStats {
    uint64_t pages_read{0};
    uint64_t pages_written{0};
    uint64_t buffer_hits{0};
    uint64_t buffer_misses{0};
    uint64_t records_scanned{0};
    double execution_time_ms{0.0};

    void Reset() { *this = QueryStats(); }
  };

}

#endif // MINI_DBMS_QUERY_STATS_H
