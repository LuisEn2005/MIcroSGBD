#include "../../include/query/parser.h"

namespace minidbms {
    const Token& Parser::Peek() const {
        static Token eof_token{TokenType::END_OF_FILE, ""};
        if (cursor_ >= tokens_.size()) return eof_token;
        return tokens_[cursor_];
    }

    Token Parser::Advance() {
        if (cursor_ >= tokens_.size()) return {TokenType::END_OF_FILE, ""};
        return tokens_[cursor_++];
    }

    bool Parser::Match(TokenType type) {
        if (Peek().type == type) {
            Advance();
            return true;
        }
        return false;
    }

    Status Parser::ParseSelect(std::unique_ptr<SQLStatement>* stmt) {
        auto select_stmt = std::make_unique<SelectStatement>();

        Advance();

        if (Match(TokenType::ASTERISK)) {
            select_stmt->fields.push_back("*");
        } else {
            while (true) {
                if (Peek().type == TokenType::IDENTIFIER) {
                    select_stmt->fields.push_back(Advance().text);
                } else {
                    return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba un nombre de columna en SELECT");
                }

                if (Match(TokenType::COMMA)) {
                    continue;
                } else {
                    break;
                }
            }
        }

        if (!Match(TokenType::KEYWORD_FROM)) {
            return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba la cláusula 'FROM'");
        }

        if (Peek().type == TokenType::IDENTIFIER) {
            select_stmt->table_name = Advance().text;
        } else {
            return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba un nombre de tabla después de 'FROM'");
        }

        if (Match(TokenType::KEYWORD_WHERE)) {
            Condition cond;

            if (Peek().type == TokenType::IDENTIFIER) {
                cond.column = Advance().text;
            } else {
                return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba un nombre de columna en WHERE");
            }

            if (Match(TokenType::EQUAL)) {
                cond.op = "=";
            } else if (Match(TokenType::GREATER)) {
                cond.op = ">";
            } else if (Match(TokenType::LESS)) {
                cond.op = "<";
            } else {
                return Status(StatusCode::INVALID_ARGUMENT, "Operador no soportado en WHERE");
            }

            if (Peek().type == TokenType::NUMBER || Peek().type == TokenType::IDENTIFIER || Peek().type == TokenType::STRING_LITERAL) {
                cond.value = Advance().text;
            } else {
                return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba un valor en WHERE");
            }

            select_stmt->conditions.push_back(cond);
        }

        Match(TokenType::SEMICOLON);

        *stmt = std::move(select_stmt);
        return Status::OK();
    }

    Status Parser::ParseInsert(std::unique_ptr<SQLStatement>* stmt) {
        auto insert_stmt = std::make_unique<InsertStatement>();
        *stmt = std::move(insert_stmt);
        return Status::OK();
    }

    Status Parser::Parse(std::unique_ptr<SQLStatement>* stmt) {
        if (tokens_.empty() || Peek().type == TokenType::END_OF_FILE) {
            return Status(StatusCode::INVALID_ARGUMENT, "No hay tokens para procesar");
        }

        if (Peek().type == TokenType::KEYWORD_SELECT) {
            return ParseSelect(stmt);
        } else if (Peek().type == TokenType::KEYWORD_INSERT) {
            return ParseInsert(stmt);
        }

        return Status(StatusCode::INVALID_ARGUMENT, "Comando SQL no soportado");
    }

} // namespace minidbms
