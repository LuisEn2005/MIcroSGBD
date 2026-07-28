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
        if (tokens_.empty()) {
            return Status(StatusCode::INVALID_ARGUMENT, "Lista de tokens vacia");
        }
        
        const auto& first_token = Peek();

        if (Match(TokenType::KEYWORD_SELECT)) {
            return ParseSelect(stmt);
        } else if (Match(TokenType::KEYWORD_INSERT)) {
            return ParseInsert(stmt);
        } else if (first_token.type == TokenType::IDENTIFIER && first_token.text == "CREATE") {
            Advance();
            return ParseCreateIndex(stmt);
        }

        return Status(StatusCode::INVALID_ARGUMENT, "Comando SQL no reconocido");
    }

    Status Parser::ParseCreateIndex(std::unique_ptr<SQLStatement>* stmt) {
        auto create_stmt = std::make_unique<CreateIndexStatement>();

        if (cursor_ >= tokens_.size() || Peek().text != "INDEX") {
            return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba 'INDEX'");
        }
        Advance();

        if (cursor_ >= tokens_.size()) return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba nombre del indice");
        create_stmt->index_name = Advance().text;

        if (cursor_ >= tokens_.size() || Peek().text != "ON") {
            return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba 'ON'");
        }
        Advance();

        if (cursor_ >= tokens_.size()) return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba nombre de la tabla");
        create_stmt->table_name = Advance().text;

        if (!Match(TokenType::LPAREN)) {
            return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba '('");
        }

        if (cursor_ >= tokens_.size()) return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba nombre de la columna");
        create_stmt->column_name = Advance().text;

        if (!Match(TokenType::RPAREN)) {
            return Status(StatusCode::INVALID_ARGUMENT, "Se esperaba ')'");
        }

        *stmt = std::move(create_stmt);
        return Status::OK();
    }
} // namespace minidbms
