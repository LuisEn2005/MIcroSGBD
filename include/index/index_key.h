#ifndef MINI_DBMS_INDEX_KEY_H
#define MINI_DBMS_INDEX_KEY_H

#include "common/status.h"
#include "storage/record_codec.h"

#include <string>

namespace minidbms {

class IndexKey {
public:
    static Status Encode(
        const FieldValue& value,
        std::string* encoded_key
    );
};

} // namespace minidbms

#endif // MINI_DBMS_INDEX_KEY_H
