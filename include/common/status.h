#ifndef MINI_DBMS_STATUS_H
#define MINI_DBMS_STATUS_H

#include <string>
#include <utility>

namespace minidbms {
  enum class StatusCode {
    OK = 0,
    NOT_FOUND,
    INVALID_ARGUMENT,
    IO_ERROR,
    OUT_OF_MEMORY,
    PAGE_PINNED
  };

  class Status {
    public:
      Status() : code_(StatusCode::OK) {}
      Status(StatusCode code, std::string msg = "") : code_(code), msg_(std::move(msg)) {}

      static Status OK() { return Status(StatusCode::OK); }
      static Status NotFound(const std::string& msg) { return Status(StatusCode::NOT_FOUND, msg); }
      static Status IOError(const std::string& msg) { return Status(StatusCode::IO_ERROR, msg); }
      static Status OutOfMemory(const std::string& msg) { return Status(StatusCode::OUT_OF_MEMORY, msg); }

      bool ok() const { return code_ == StatusCode::OK; }
      StatusCode code() const { return code_; }
      const std::string& message() const { return msg_; }

    private:
      StatusCode code_;
      std::string msg_;
  };

}

#endif // MINI_DBMS_STATUS_H
