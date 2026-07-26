#ifndef MINI_DBMS_CONFIG_H
#define MINI_DBMS_CONFIG_H

#include "types.h"

namespace minidbms {
  constexpr std::size_t DEFAULT_BUFFER_POOL_SIZE = 10;
  constexpr PageId HEADER_PAGE_ID = 0;

}

#endif // MINI_DBMS_CONFIG_H
