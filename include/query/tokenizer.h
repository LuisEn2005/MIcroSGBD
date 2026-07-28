#ifndef MINI_DBMS_TOKENIZER_H
#define MINI_DBMS_TOKENIZER_H

#include <string>
#include <utility>
#include <vector>

namespace minidbms {

enum class TokenType {
    KEYWORD_SELECT,
    KEYWORD_INSERT,
    KEYWORD_UPDATE,
    KEYWORD_DELETE,
    KEYWORD_CREATE,
    KEYWORD_INDEX,
    KEYWORD_ON,
    KEYWORD_INTO,
    KEYWORD_VALUES,
    KEYWORD_FROM,
    KEYWORD_WHERE,
    KEYWORD_SET,
    KEYWORD_EXPLAIN,
    KEYWORD_ANALYZE,
    IDENTIFIER,
    NUMBER,
    STRING_LITERAL,
    ASTERISK,
    COMMA,
    EQUAL,
    GREATER,
    LESS,
    GREATER_EQUAL,
    LESS_EQUAL,
    SEMICOLON,
    LPAREN,
    RPAREN,
    END_OF_FILE
};

struct Token {
    TokenType type;
    std::string text;
};

class Tokenizer {
public:
    explicit Tokenizer(std::string sql)
        : sql_(std::move(sql)) {}

    std::vector<Token> Tokenize();

private:
    char Peek() const;
    char Get();
    void SkipWhitespace();

    std::string sql_;
    std::size_t cursor_{0};
};

} // namespace minidbms

#endif // MINI_DBMS_TOKENIZER_H
