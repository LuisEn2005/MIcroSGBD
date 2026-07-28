#ifndef MINI_DBMS_PARSER_H
#define MINI_DBMS_PARSER_H

#include "../query/tokenizer.h"
#include "../common/status.h"
#include <string>
#include <vector>
#include <memory>
#include <utility>

namespace minidbms {
    enum class StatementType { SELECT, INSERT, UPDATE, DELETE, EXPLAIN };

    struct Condition {
        std::string column;
        std::string op;
        std::string value;
    };

    class SQLStatement {
        public:
            virtual ~SQLStatement() = default;
            virtual StatementType GetType() const = 0;
    };

    class SelectStatement : public SQLStatement {
        public:
            StatementType GetType() const override { return StatementType::SELECT; }

            std::string table_name;
            std::vector<std::string> fields;
            std::vector<Condition> conditions;
    };

    class InsertStatement : public SQLStatement {
        public:
            StatementType GetType() const override { return StatementType::INSERT; }

            std::string table_name;
            std::vector<std::string> values;
    };

    class Parser {
        public:
            explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
            Status Parse(std::unique_ptr<SQLStatement>* stmt);

        private:
            const Token& Peek() const;
            Token Advance();
            bool Match(TokenType type);

            Status ParseSelect(std::unique_ptr<SQLStatement>* stmt);
            Status ParseInsert(std::unique_ptr<SQLStatement>* stmt);

            std::vector<Token> tokens_;
            size_t cursor_{0};
    };

}

#endif // MINI_DBMS_PARSER_H
