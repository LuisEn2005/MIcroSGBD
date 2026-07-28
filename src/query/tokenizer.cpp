#include "query/tokenizer.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace minidbms {

char Tokenizer::Peek() const {
    return cursor_ < sql_.size() ? sql_[cursor_] : '\0';
}

char Tokenizer::PeekNext() const {
    return cursor_ + 1 < sql_.size() ? sql_[cursor_ + 1] : '\0';
}

char Tokenizer::Get() {
    return cursor_ < sql_.size() ? sql_[cursor_++] : '\0';
}

void Tokenizer::SkipWhitespace() {
    while (cursor_ < sql_.size() &&
           std::isspace(static_cast<unsigned char>(sql_[cursor_]))) {
        ++cursor_;
    }
}

std::vector<Token> Tokenizer::Tokenize() {
    cursor_ = 0;
    std::vector<Token> tokens;

    while (cursor_ < sql_.size()) {
        SkipWhitespace();
        if (cursor_ >= sql_.size()) {
            break;
        }

        const char character = Peek();

        if (character == '*') {
            Get();
            tokens.push_back({TokenType::ASTERISK, "*"});
            continue;
        }
        if (character == ',') {
            Get();
            tokens.push_back({TokenType::COMMA, ","});
            continue;
        }
        if (character == '=') {
            Get();
            tokens.push_back({TokenType::EQUAL, "="});
            continue;
        }
        if (character == '>') {
            Get();
            if (Peek() == '=') {
                Get();
                tokens.push_back({TokenType::GREATER_EQUAL, ">="});
            } else {
                tokens.push_back({TokenType::GREATER, ">"});
            }
            continue;
        }
        if (character == '<') {
            Get();
            if (Peek() == '=') {
                Get();
                tokens.push_back({TokenType::LESS_EQUAL, "<="});
            } else {
                tokens.push_back({TokenType::LESS, "<"});
            }
            continue;
        }
        if (character == ';') {
            Get();
            tokens.push_back({TokenType::SEMICOLON, ";"});
            continue;
        }
        if (character == '(') {
            Get();
            tokens.push_back({TokenType::LPAREN, "("});
            continue;
        }
        if (character == ')') {
            Get();
            tokens.push_back({TokenType::RPAREN, ")"});
            continue;
        }

        if (character == '\'' || character == '"') {
            const char quote = Get();
            std::string value;
            bool terminated = false;

            while (cursor_ < sql_.size()) {
                if (Peek() == quote) {
                    if (PeekNext() == quote) {
                        Get();
                        Get();
                        value += quote;
                        continue;
                    }

                    Get();
                    terminated = true;
                    break;
                }

                value += Get();
            }

            if (!terminated) {
                tokens.push_back({
                    TokenType::INVALID,
                    "Unterminated string literal"
                });
                break;
            }

            tokens.push_back({TokenType::STRING_LITERAL, value});
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(character)) ||
            character == '_') {
            std::string word;

            while (cursor_ < sql_.size() &&
                   (std::isalnum(static_cast<unsigned char>(Peek())) ||
                    Peek() == '_')) {
                word += Get();
            }

            std::string upper_word = word;
            std::transform(
                upper_word.begin(),
                upper_word.end(),
                upper_word.begin(),
                [](unsigned char current) {
                    return static_cast<char>(std::toupper(current));
                }
            );

            if (upper_word == "SELECT") {
                tokens.push_back({TokenType::KEYWORD_SELECT, word});
            } else if (upper_word == "INSERT") {
                tokens.push_back({TokenType::KEYWORD_INSERT, word});
            } else if (upper_word == "UPDATE") {
                tokens.push_back({TokenType::KEYWORD_UPDATE, word});
            } else if (upper_word == "DELETE") {
                tokens.push_back({TokenType::KEYWORD_DELETE, word});
            } else if (upper_word == "CREATE") {
                tokens.push_back({TokenType::KEYWORD_CREATE, word});
            } else if (upper_word == "TABLE") {
                tokens.push_back({TokenType::KEYWORD_TABLE, word});
            } else if (upper_word == "INDEX") {
                tokens.push_back({TokenType::KEYWORD_INDEX, word});
            } else if (upper_word == "ON") {
                tokens.push_back({TokenType::KEYWORD_ON, word});
            } else if (upper_word == "INTO") {
                tokens.push_back({TokenType::KEYWORD_INTO, word});
            } else if (upper_word == "VALUES") {
                tokens.push_back({TokenType::KEYWORD_VALUES, word});
            } else if (upper_word == "FROM") {
                tokens.push_back({TokenType::KEYWORD_FROM, word});
            } else if (upper_word == "WHERE") {
                tokens.push_back({TokenType::KEYWORD_WHERE, word});
            } else if (upper_word == "SET") {
                tokens.push_back({TokenType::KEYWORD_SET, word});
            } else if (upper_word == "EXPLAIN") {
                tokens.push_back({TokenType::KEYWORD_EXPLAIN, word});
            } else if (upper_word == "ANALYZE") {
                tokens.push_back({TokenType::KEYWORD_ANALYZE, word});
            } else if (upper_word == "AND") {
                tokens.push_back({TokenType::KEYWORD_AND, word});
            } else if (upper_word == "NULL") {
                tokens.push_back({TokenType::KEYWORD_NULL, word});
            } else {
                tokens.push_back({TokenType::IDENTIFIER, word});
            }

            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(character)) ||
            (character == '-' &&
             std::isdigit(static_cast<unsigned char>(PeekNext())))) {
            std::string number;

            if (Peek() == '-') {
                number += Get();
            }

            while (cursor_ < sql_.size() &&
                   std::isdigit(static_cast<unsigned char>(Peek()))) {
                number += Get();
            }

            tokens.push_back({TokenType::NUMBER, number});
            continue;
        }

        std::string error = "Unexpected character: ";
        error += character;
        Get();
        tokens.push_back({TokenType::INVALID, error});
        break;
    }

    tokens.push_back({TokenType::END_OF_FILE, ""});
    return tokens;
}

} // namespace minidbms
