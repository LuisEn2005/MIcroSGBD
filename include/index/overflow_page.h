#ifndef MINI_DBMS_OVERFLOW_PAGE_H
#define MINI_DBMS_OVERFLOW_PAGE_H

#include "index/bucket_page.h"

namespace minidbms {

// Las páginas de desbordamiento utilizan exactamente el mismo formato físico
// que una página bucket. Se mantiene este alias para expresar su función.
using HashIndexOverflowPage = HashIndexBucketPage;

} // namespace minidbms

#endif // MINI_DBMS_OVERFLOW_PAGE_H
