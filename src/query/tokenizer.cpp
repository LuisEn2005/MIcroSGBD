#include "../../include/query/tokenizer.h"

#include <cctype>
#include <algorithm>

namespace minidbms {
    char Tokenizer::Peek() const {
        if (cursor_ >= sql_.size()) return '\0';
        return sql_[cursor_];
    }

    char Tokenizer::Get() {
        if (cursor_ >= sql_.size()) return '\0';
        return sql_[cursor_++];
    }

    void Tokenizer::SkipWhitespace() {
        while (cursor_ < sql_.size() && std::isspace(static_cast<unsigned char>(sql_[cursor_]))) {
            cursor_++;
        }
    }

    std::vector<Token> Tokenizer::Tokenize() {
        std::vector<Token> tokens;

        while (cursor_ < sql_.size()) {
            SkipWhitespace();
            if (cursor_ >= sql_.size()) break;

            char c = Peek();

            // Caracteres individuales
            if (c == '*') { Get(); tokens.push_back({TokenType::ASTERISK, "*"}); continue; }
            if (c == ',') { Get(); tokens.push_back({TokenType::COMMA, ","}); continue; }
            if (c == '=') { Get(); tokens.push_back({TokenType::EQUAL, "="}); continue; }
            if (c == '>') { Get(); tokens.push_back({TokenType::GREATER, ">"}); continue; }
            if (c == '<') { Get(); tokens.push_back({TokenType::LESS, "<"}); continue; }
            if (c == ';') { Get(); tokens.push_back({TokenType::SEMICOLON, ";"}); continue; }
            if (c == '(') { Get(); tokens.push_back({TokenType::LPAREN, "("}); continue; }
            if (c == ')') { Get(); tokens.push_back({TokenType::RPAREN, ")"}); continue; }

            // Identificadores y Palabras Clave
            if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::string word;
                while (cursor_ < sql_.size() && (std::isalnum(static_cast<unsigned char>(Peek())) || Peek() == '_')) {
                    word += Get();
                }

                std::string upper_word = word;
                std::transform(upper_word.begin(), upper_word.end(), upper_word.begin(), ::toupper);

                if (upper_word == "SELECT") tokens.push_back({TokenType::KEYWORD_SELECT, word});
                else if (upper_word == "INSERT") tokens.push_back({TokenType::KEYWORD_INSERT, word});
                else if (upper_word == "UPDATE") tokens.push_back({TokenType::KEYWORD_UPDATE, word});
                else if (upper_word == "DELETE") tokens.push_back({TokenType::KEYWORD_DELETE, word});
                else if (upper_word == "INTO") tokens.push_back({TokenType::KEYWORD_INTO, word});
                else if (upper_word == "VALUES") tokens.push_back({TokenType::KEYWORD_VALUES, word});
                else if (upper_word == "FROM") tokens.push_back({TokenType::KEYWORD_FROM, word});
                else if (upper_word == "WHERE") tokens.push_back({TokenType::KEYWORD_WHERE, word});
                else if (upper_word == "SET") tokens.push_back({TokenType::KEYWORD_SET, word});
                else if (upper_word == "EXPLAIN") tokens.push_back({TokenType::KEYWORD_EXPLAIN, word});
                else if (upper_word == "ANALYZE") tokens.push_back({TokenType::KEYWORD_ANALYZE, word});
                else tokens.push_back({TokenType::IDENTIFIER, word});

                continue;
            }

            if (std::isdigit(static_cast<unsigned char>(c))) {
                std::string num;
                while (cursor_ < sql_.size() && std::isdigit(static_cast<unsigned char>(Peek()))) {
                    num += Get();
                }
                tokens.push_back({TokenType::NUMBER, num});
                continue;
            }

            Get();
        }

        tokens.push_back({TokenType::END_OF_FILE, ""});
        return tokens;
    }

} // namespace minidbms
