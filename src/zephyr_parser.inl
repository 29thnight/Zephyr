// Part of src/zephyr.cpp — included by zephyr.cpp
class Parser {
public:
    Parser(std::vector<Token> tokens, std::string module_name) : tokens_(std::move(tokens)), module_name_(std::move(module_name)) {}

    RuntimeResult<std::unique_ptr<Program>> parse_program() {
        auto program = std::make_unique<Program>();
        while (!is_at_end()) {
            ZEPHYR_TRY_ASSIGN(statement, parse_declaration());
            program->statements.push_back(std::move(statement));
        }
        return program;
    }

private:
    const Token& peek() const { return tokens_[current_]; }
    const Token& previous() const { return tokens_[current_ - 1]; }
    bool is_at_end() const { return peek().type == TokenType::EndOfFile; }
    bool check(TokenType type) const { return !is_at_end() && peek().type == type; }

    const Token& advance() {
        if (!is_at_end()) {
            ++current_;
        }
        return previous();
    }

    bool match(std::initializer_list<TokenType> types) {
        for (TokenType type : types) {
            if (check(type)) {
                advance();
                return true;
            }
        }
        return false;
    }

    const Token* peek_offset(std::size_t offset) const {
        const std::size_t index = current_ + offset;
        if (index >= tokens_.size()) {
            return nullptr;
        }
        return &tokens_[index];
    }

    bool lookahead_type_ref(std::size_t& offset) const {
        const Token* token = peek_offset(offset);
        if (token == nullptr || token->type != TokenType::Identifier) {
            return false;
        }
        ++offset;

        while (true) {
            token = peek_offset(offset);
            if (token == nullptr || token->type != TokenType::DoubleColon) {
                break;
            }
            ++offset;
            token = peek_offset(offset);
            if (token == nullptr || token->type != TokenType::Identifier) {
                return false;
            }
            ++offset;
        }

        return true;
    }

    bool lookahead_generic_call_type_arguments() const {
        if (!check(TokenType::Less)) {
            return false;
        }

        std::size_t offset = 1;
        if (!lookahead_type_ref(offset)) {
            return false;
        }

        while (true) {
            const Token* token = peek_offset(offset);
            if (token == nullptr) {
                return false;
            }
            if (token->type == TokenType::Comma) {
                ++offset;
                if (!lookahead_type_ref(offset)) {
                    return false;
                }
                continue;
            }
            if (token->type == TokenType::Greater) {
                ++offset;
                token = peek_offset(offset);
                return token != nullptr && token->type == TokenType::LeftParen;
            }
            return false;
        }
    }

    RuntimeResult<Token> consume(TokenType type, const std::string& message) {
        if (check(type)) {
            return advance();
        }
        return make_loc_error<Token>(module_name_, peek().span, message);
    }

    RuntimeResult<std::unique_ptr<Stmt>> parse_declaration();
    RuntimeResult<std::unique_ptr<Stmt>> parse_import();
    RuntimeResult<std::unique_ptr<Stmt>> parse_function_decl();
    RuntimeResult<std::unique_ptr<Stmt>> parse_struct_decl();
    RuntimeResult<std::unique_ptr<Stmt>> parse_enum_decl();
    RuntimeResult<std::unique_ptr<Stmt>> parse_trait_decl();
    RuntimeResult<std::unique_ptr<Stmt>> parse_impl_decl();
    RuntimeResult<std::unique_ptr<Stmt>> parse_statement_or_let();
    RuntimeResult<std::unique_ptr<Stmt>> parse_let_stmt();
    RuntimeResult<std::unique_ptr<BlockStmt>> parse_block_stmt(const std::string& message);
    RuntimeResult<std::unique_ptr<BlockStmt>> parse_block_from_open(const Span& span);
    RuntimeResult<std::unique_ptr<Stmt>> parse_if_stmt();
    RuntimeResult<std::unique_ptr<Stmt>> parse_while_stmt();
    RuntimeResult<std::unique_ptr<Stmt>> parse_for_stmt();
    RuntimeResult<std::unique_ptr<Stmt>> parse_break_stmt();
    RuntimeResult<std::unique_ptr<Stmt>> parse_continue_stmt();
    RuntimeResult<std::unique_ptr<Stmt>> parse_return_stmt();
    RuntimeResult<std::unique_ptr<Stmt>> parse_yield_stmt();
    VoidResult parse_function_signature(std::vector<Param>& params, std::optional<TypeRef>& return_type);
    VoidResult parse_generic_type_params(std::vector<std::string>& out_params);
    RuntimeResult<TypeRef> parse_type_ref();
    RuntimeResult<std::vector<TypeRef>> parse_type_arguments();
    RuntimeResult<ExprPtr> parse_expression();
    RuntimeResult<ExprPtr> parse_assignment();
    RuntimeResult<ExprPtr> parse_or();
    RuntimeResult<ExprPtr> parse_and();
    RuntimeResult<ExprPtr> parse_equality();
    RuntimeResult<ExprPtr> parse_comparison();
    RuntimeResult<ExprPtr> parse_term();
    RuntimeResult<ExprPtr> parse_factor();
    RuntimeResult<ExprPtr> parse_unary();
    RuntimeResult<ExprPtr> parse_call();
    RuntimeResult<ExprPtr> parse_primary();
    RuntimeResult<ExprPtr> parse_interpolated_string();
    RuntimeResult<ExprPtr> parse_identifier_led_expression();
    RuntimeResult<ExprPtr> parse_match_expr();
    RuntimeResult<PatternPtr> parse_pattern();
    RuntimeResult<PatternPtr> parse_pattern_primary();

    std::vector<Token> tokens_;
    std::string module_name_;
    std::size_t current_ = 0;
};

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_declaration() {
    bool exported = false;
    if (match({TokenType::KeywordExport})) {
        exported = true;
    }

    std::unique_ptr<Stmt> stmt;
    if (match({TokenType::KeywordImport})) {
        ZEPHYR_TRY_ASSIGN(import_stmt, parse_import());
        stmt = std::move(import_stmt);
    } else if (match({TokenType::KeywordFn})) {
        ZEPHYR_TRY_ASSIGN(function_stmt, parse_function_decl());
        stmt = std::move(function_stmt);
    } else if (match({TokenType::KeywordStruct})) {
        ZEPHYR_TRY_ASSIGN(struct_stmt, parse_struct_decl());
        stmt = std::move(struct_stmt);
    } else if (match({TokenType::KeywordEnum})) {
        ZEPHYR_TRY_ASSIGN(enum_stmt, parse_enum_decl());
        stmt = std::move(enum_stmt);
    } else if (match({TokenType::KeywordTrait})) {
        ZEPHYR_TRY_ASSIGN(trait_stmt, parse_trait_decl());
        stmt = std::move(trait_stmt);
    } else if (match({TokenType::KeywordImpl})) {
        ZEPHYR_TRY_ASSIGN(impl_stmt, parse_impl_decl());
        stmt = std::move(impl_stmt);
    } else {
        ZEPHYR_TRY_ASSIGN(statement, parse_statement_or_let());
        stmt = std::move(statement);
    }
    stmt->exported = exported;
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_import() {
    ZEPHYR_TRY_ASSIGN(path_token, consume(TokenType::String, "Expected string literal after import."));
    auto stmt = std::make_unique<ImportStmt>();
    stmt->span = path_token.span;
    stmt->path = path_token.lexeme;
    if (match({TokenType::KeywordAs})) {
        ZEPHYR_TRY_ASSIGN(alias_token, consume(TokenType::Identifier, "Expected import alias."));
        stmt->alias = alias_token.lexeme;
    }
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after import."));
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_function_decl() {
    ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected function name."));
    auto function = std::make_unique<FunctionDecl>();
    function->span = name.span;
    function->name = name.lexeme;
    ZEPHYR_TRY(parse_generic_type_params(function->generic_params));
    ZEPHYR_TRY(parse_function_signature(function->params, function->return_type));
    ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected function body."));
    function->body = std::move(body);
    return function;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_struct_decl() {
    ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected struct name."));
    auto decl = std::make_unique<StructDecl>();
    decl->span = name.span;
    decl->name = name.lexeme;
    ZEPHYR_TRY(parse_generic_type_params(decl->generic_params));
    ZEPHYR_TRY(consume(TokenType::LeftBrace, "Expected '{' after struct name."));
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        StructFieldDecl field;
        ZEPHYR_TRY_ASSIGN(field_name, consume(TokenType::Identifier, "Expected field name."));
        field.name = field_name.lexeme;
        ZEPHYR_TRY(consume(TokenType::Colon, "Expected ':' after field name."));
        ZEPHYR_TRY_ASSIGN(field_type, parse_type_ref());
        field.type = std::move(field_type);
        decl->fields.push_back(std::move(field));
        if (!match({TokenType::Comma})) {
            break;
        }
    }
    ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after struct fields."));
    return decl;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_enum_decl() {
    ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected enum name."));
    auto decl = std::make_unique<EnumDecl>();
    decl->span = name.span;
    decl->name = name.lexeme;
    ZEPHYR_TRY(consume(TokenType::LeftBrace, "Expected '{' after enum name."));
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        EnumVariantDecl variant;
        ZEPHYR_TRY_ASSIGN(variant_name, consume(TokenType::Identifier, "Expected variant name."));
        variant.name = variant_name.lexeme;
        if (match({TokenType::LeftParen})) {
            if (!check(TokenType::RightParen)) {
                do {
                    ZEPHYR_TRY_ASSIGN(payload_type, parse_type_ref());
                    variant.payload_types.push_back(std::move(payload_type));
                } while (match({TokenType::Comma}));
            }
            ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after enum payload types."));
        }
        decl->variants.push_back(std::move(variant));
        if (!match({TokenType::Comma})) {
            break;
        }
    }
    ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after enum variants."));
    return decl;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_trait_decl() {
    ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected trait name."));
    auto decl = std::make_unique<TraitDecl>();
    decl->span = name.span;
    decl->name = name.lexeme;
    ZEPHYR_TRY(parse_generic_type_params(decl->generic_params));
    ZEPHYR_TRY(consume(TokenType::LeftBrace, "Expected '{' after trait name."));
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        ZEPHYR_TRY(consume(TokenType::KeywordFn, "Expected 'fn' in trait body."));
        TraitMethodDecl method;
        method.span = previous().span;
        ZEPHYR_TRY_ASSIGN(method_name, consume(TokenType::Identifier, "Expected trait method name."));
        method.span = method_name.span;
        method.name = method_name.lexeme;
        ZEPHYR_TRY(parse_function_signature(method.params, method.return_type));
        ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after trait method signature."));
        decl->methods.push_back(std::move(method));
    }
    ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after trait body."));
    return decl;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_impl_decl() {
    auto decl = std::make_unique<ImplDecl>();
    decl->span = previous().span;
    ZEPHYR_TRY(parse_generic_type_params(decl->generic_params));
    ZEPHYR_TRY_ASSIGN(trait_name, parse_type_ref());
    decl->trait_name = std::move(trait_name);
    ZEPHYR_TRY(consume(TokenType::KeywordFor, "Expected 'for' in impl declaration."));
    ZEPHYR_TRY_ASSIGN(for_type, parse_type_ref());
    decl->for_type = std::move(for_type);
    ZEPHYR_TRY(consume(TokenType::LeftBrace, "Expected '{' after impl target type."));
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        ZEPHYR_TRY(consume(TokenType::KeywordFn, "Expected 'fn' in impl body."));
        ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected method name."));
        auto method = std::make_unique<FunctionDecl>();
        method->span = name.span;
        method->name = name.lexeme;
        ZEPHYR_TRY(parse_generic_type_params(method->generic_params));
        ZEPHYR_TRY(parse_function_signature(method->params, method->return_type));
        ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected method body."));
        method->body = std::move(body);
        decl->methods.push_back(std::move(method));
    }
    ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after impl body."));
    return decl;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_statement_or_let() {
    if (match({TokenType::KeywordLet})) {
        return parse_let_stmt();
    }
    if (match({TokenType::LeftBrace})) {
        return parse_block_from_open(previous().span);
    }
    if (match({TokenType::KeywordIf})) {
        return parse_if_stmt();
    }
    if (match({TokenType::KeywordWhile})) {
        return parse_while_stmt();
    }
    if (match({TokenType::KeywordFor})) {
        return parse_for_stmt();
    }
    if (match({TokenType::KeywordBreak})) {
        return parse_break_stmt();
    }
    if (match({TokenType::KeywordContinue})) {
        return parse_continue_stmt();
    }
    if (match({TokenType::KeywordReturn})) {
        return parse_return_stmt();
    }
    if (match({TokenType::KeywordYield})) {
        return parse_yield_stmt();
    }
    auto stmt = std::make_unique<ExprStmt>();
    stmt->span = peek().span;
    ZEPHYR_TRY_ASSIGN(expression, parse_expression());
    stmt->expression = std::move(expression);
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after expression."));
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_let_stmt() {
    auto stmt = std::make_unique<LetStmt>();
    stmt->span = previous().span;
    stmt->mutable_value = match({TokenType::KeywordMut});
    ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected variable name."));
    stmt->name = name.lexeme;
    if (match({TokenType::Colon})) {
        ZEPHYR_TRY_ASSIGN(type, parse_type_ref());
        stmt->type = std::move(type);
    }
    ZEPHYR_TRY(consume(TokenType::Equal, "Expected '=' in let binding."));
    ZEPHYR_TRY_ASSIGN(initializer, parse_expression());
    stmt->initializer = std::move(initializer);
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after let binding."));
    return stmt;
}

RuntimeResult<std::unique_ptr<BlockStmt>> Parser::parse_block_stmt(const std::string& message) {
    ZEPHYR_TRY(consume(TokenType::LeftBrace, message));
    return parse_block_from_open(previous().span);
}

RuntimeResult<std::unique_ptr<BlockStmt>> Parser::parse_block_from_open(const Span& span) {
    auto block = std::make_unique<BlockStmt>();
    block->span = span;
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        ZEPHYR_TRY_ASSIGN(statement, parse_declaration());
        block->statements.push_back(std::move(statement));
    }
    ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after block."));
    return block;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_if_stmt() {
    auto stmt = std::make_unique<IfStmt>();
    stmt->span = previous().span;
    ZEPHYR_TRY_ASSIGN(condition, parse_expression());
    stmt->condition = std::move(condition);
    ZEPHYR_TRY_ASSIGN(then_branch, parse_block_stmt("Expected '{' after if condition."));
    stmt->then_branch = std::move(then_branch);
    if (match({TokenType::KeywordElse})) {
        if (match({TokenType::KeywordIf})) {
            ZEPHYR_TRY_ASSIGN(else_if_branch, parse_if_stmt());
            stmt->else_branch = std::move(else_if_branch);
        } else if (match({TokenType::LeftBrace})) {
            ZEPHYR_TRY_ASSIGN(else_block, parse_block_from_open(previous().span));
            stmt->else_branch = std::move(else_block);
        } else {
            return make_loc_error<std::unique_ptr<Stmt>>(module_name_, peek().span, "Expected block or else-if after else.");
        }
    }
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_while_stmt() {
    auto stmt = std::make_unique<WhileStmt>();
    stmt->span = previous().span;
    ZEPHYR_TRY_ASSIGN(condition, parse_expression());
    stmt->condition = std::move(condition);
    ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected '{' after while condition."));
    stmt->body = std::move(body);
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_for_stmt() {
    auto stmt = std::make_unique<ForStmt>();
    stmt->span = previous().span;
    ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected loop variable name."));
    stmt->name = name.lexeme;
    ZEPHYR_TRY(consume(TokenType::KeywordIn, "Expected 'in' after loop variable."));
    ZEPHYR_TRY_ASSIGN(iterable, parse_expression());
    stmt->iterable = std::move(iterable);
    ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected '{' after for iterator."));
    stmt->body = std::move(body);
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_break_stmt() {
    auto stmt = std::make_unique<BreakStmt>();
    stmt->span = previous().span;
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after break."));
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_continue_stmt() {
    auto stmt = std::make_unique<ContinueStmt>();
    stmt->span = previous().span;
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after continue."));
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_return_stmt() {
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->span = previous().span;
    if (!check(TokenType::Semicolon)) {
        ZEPHYR_TRY_ASSIGN(value, parse_expression());
        stmt->value = std::move(value);
    }
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after return."));
    return stmt;
}

RuntimeResult<std::unique_ptr<Stmt>> Parser::parse_yield_stmt() {
    auto stmt = std::make_unique<YieldStmt>();
    stmt->span = previous().span;
    if (!check(TokenType::Semicolon)) {
        ZEPHYR_TRY_ASSIGN(value, parse_expression());
        stmt->value = std::move(value);
    }
    ZEPHYR_TRY(consume(TokenType::Semicolon, "Expected ';' after yield."));
    return stmt;
}

VoidResult Parser::parse_generic_type_params(std::vector<std::string>& out_params) {
    if (!check(TokenType::Less)) return ok_result();
    advance();  // consume '<'

    if (!check(TokenType::Identifier)) {
        return make_loc_error<std::monostate>(module_name_, peek().span, "Expected type parameter name");
    }
    out_params.push_back(peek().lexeme);
    advance();

    while (check(TokenType::Comma)) {
        advance();  // consume ','
        if (!check(TokenType::Identifier)) {
            return make_loc_error<std::monostate>(module_name_, peek().span, "Expected type parameter name after ','");
        }
        out_params.push_back(peek().lexeme);
        advance();
    }

    if (!check(TokenType::Greater)) {
        return make_loc_error<std::monostate>(module_name_, peek().span, "Expected '>' after type parameters");
    }
    advance();  // consume '>'
    return ok_result();
}

VoidResult Parser::parse_function_signature(std::vector<Param>& params, std::optional<TypeRef>& return_type) {
    ZEPHYR_TRY(consume(TokenType::LeftParen, "Expected '(' after function name."));
    if (!check(TokenType::RightParen)) {
        do {
            Param param;
            param.span = peek().span;
            ZEPHYR_TRY_ASSIGN(name, consume(TokenType::Identifier, "Expected parameter name."));
            param.name = name.lexeme;
            if (match({TokenType::Colon})) {
                ZEPHYR_TRY_ASSIGN(type, parse_type_ref());
                param.type = std::move(type);
            }
            params.push_back(std::move(param));
        } while (match({TokenType::Comma}));
    }
    ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after parameters."));
    if (match({TokenType::Arrow})) {
        ZEPHYR_TRY_ASSIGN(type, parse_type_ref());
        return_type = std::move(type);
    }
    return ok_result();
}

RuntimeResult<TypeRef> Parser::parse_type_ref() {
    TypeRef type;
    type.span = peek().span;
    ZEPHYR_TRY_ASSIGN(type_name, consume(TokenType::Identifier, "Expected type name."));
    type.parts.push_back(type_name.lexeme);
    while (match({TokenType::DoubleColon})) {
        ZEPHYR_TRY_ASSIGN(segment, consume(TokenType::Identifier, "Expected type segment after '::'."));
        type.parts.push_back(segment.lexeme);
    }
    return type;
}

RuntimeResult<std::vector<TypeRef>> Parser::parse_type_arguments() {
    std::vector<TypeRef> type_arguments;
    ZEPHYR_TRY(consume(TokenType::Less, "Expected '<' before type arguments."));
    do {
        ZEPHYR_TRY_ASSIGN(type_argument, parse_type_ref());
        type_arguments.push_back(std::move(type_argument));
    } while (match({TokenType::Comma}));
    ZEPHYR_TRY(consume(TokenType::Greater, "Expected '>' after type arguments."));
    return type_arguments;
}

RuntimeResult<ExprPtr> Parser::parse_expression() { return parse_assignment(); }
RuntimeResult<ExprPtr> Parser::parse_assignment() {
    ZEPHYR_TRY_ASSIGN(expr, parse_or());
    if (match({TokenType::Equal, TokenType::PlusEqual, TokenType::MinusEqual, TokenType::StarEqual, TokenType::SlashEqual})) {
        auto assignment = std::make_unique<AssignExpr>();
        assignment->span = previous().span;
        assignment->assignment_op = previous().type;
        assignment->target = std::move(expr);
        ZEPHYR_TRY_ASSIGN(value, parse_assignment());
        assignment->value = std::move(value);
        return assignment;
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_or() {
    ZEPHYR_TRY_ASSIGN(expr, parse_and());
    while (match({TokenType::OrOr})) {
        auto binary = std::make_unique<BinaryExpr>();
        binary->span = previous().span;
        binary->op = previous().type;
        binary->left = std::move(expr);
        ZEPHYR_TRY_ASSIGN(right, parse_and());
        binary->right = std::move(right);
        expr = std::move(binary);
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_and() {
    ZEPHYR_TRY_ASSIGN(expr, parse_equality());
    while (match({TokenType::AndAnd})) {
        auto binary = std::make_unique<BinaryExpr>();
        binary->span = previous().span;
        binary->op = previous().type;
        binary->left = std::move(expr);
        ZEPHYR_TRY_ASSIGN(right, parse_equality());
        binary->right = std::move(right);
        expr = std::move(binary);
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_equality() {
    ZEPHYR_TRY_ASSIGN(expr, parse_comparison());
    while (match({TokenType::EqualEqual, TokenType::BangEqual})) {
        auto binary = std::make_unique<BinaryExpr>();
        binary->span = previous().span;
        binary->op = previous().type;
        binary->left = std::move(expr);
        ZEPHYR_TRY_ASSIGN(right, parse_comparison());
        binary->right = std::move(right);
        expr = std::move(binary);
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_comparison() {
    ZEPHYR_TRY_ASSIGN(expr, parse_term());
    while (match({TokenType::Less, TokenType::LessEqual, TokenType::Greater, TokenType::GreaterEqual})) {
        auto binary = std::make_unique<BinaryExpr>();
        binary->span = previous().span;
        binary->op = previous().type;
        binary->left = std::move(expr);
        ZEPHYR_TRY_ASSIGN(right, parse_term());
        binary->right = std::move(right);
        expr = std::move(binary);
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_term() {
    ZEPHYR_TRY_ASSIGN(expr, parse_factor());
    while (match({TokenType::Plus, TokenType::Minus})) {
        auto binary = std::make_unique<BinaryExpr>();
        binary->span = previous().span;
        binary->op = previous().type;
        binary->left = std::move(expr);
        ZEPHYR_TRY_ASSIGN(right, parse_factor());
        binary->right = std::move(right);
        expr = std::move(binary);
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_factor() {
    ZEPHYR_TRY_ASSIGN(expr, parse_unary());
    while (match({TokenType::Star, TokenType::Slash, TokenType::Percent})) {
        auto binary = std::make_unique<BinaryExpr>();
        binary->span = previous().span;
        binary->op = previous().type;
        binary->left = std::move(expr);
        ZEPHYR_TRY_ASSIGN(right, parse_unary());
        binary->right = std::move(right);
        expr = std::move(binary);
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_unary() {
    if (match({TokenType::KeywordResume})) {
        auto expr = std::make_unique<ResumeExpr>();
        expr->span = previous().span;
        ZEPHYR_TRY_ASSIGN(target, parse_unary());
        expr->target = std::move(target);
        return expr;
    }
    if (match({TokenType::Bang, TokenType::Minus})) {
        auto unary = std::make_unique<UnaryExpr>();
        unary->span = previous().span;
        unary->op = previous().type;
        ZEPHYR_TRY_ASSIGN(right, parse_unary());
        unary->right = std::move(right);
        return unary;
    }
    return parse_call();
}

RuntimeResult<ExprPtr> Parser::parse_call() {
    ZEPHYR_TRY_ASSIGN(expr, parse_primary());
    while (true) {
        if (check(TokenType::Less) && lookahead_generic_call_type_arguments()) {
            ZEPHYR_TRY_ASSIGN(type_arguments, parse_type_arguments());
            auto call = std::make_unique<CallExpr>();
            call->span = peek().span;
            call->callee = std::move(expr);
            call->type_arguments = std::move(type_arguments);
            ZEPHYR_TRY(consume(TokenType::LeftParen, "Expected '(' after type arguments."));
            if (!check(TokenType::RightParen)) {
                do {
                    ZEPHYR_TRY_ASSIGN(argument, parse_expression());
                    call->arguments.push_back(std::move(argument));
                } while (match({TokenType::Comma}));
            }
            ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after arguments."));
            expr = std::move(call);
        } else if (match({TokenType::LeftParen})) {
            auto call = std::make_unique<CallExpr>();
            call->span = previous().span;
            call->callee = std::move(expr);
            if (!check(TokenType::RightParen)) {
                do {
                    ZEPHYR_TRY_ASSIGN(argument, parse_expression());
                    call->arguments.push_back(std::move(argument));
                } while (match({TokenType::Comma}));
            }
            ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after arguments."));
            expr = std::move(call);
        } else if (match({TokenType::Dot})) {
            auto member = std::make_unique<MemberExpr>();
            member->span = previous().span;
            member->object = std::move(expr);
            ZEPHYR_TRY_ASSIGN(member_name, consume(TokenType::Identifier, "Expected member name after '.'."));
            member->member = member_name.lexeme;
            expr = std::move(member);
        } else if (match({TokenType::QuestionDot})) {
            const Span chain_span = previous().span;
            ZEPHYR_TRY_ASSIGN(member_name, consume(TokenType::Identifier, "Expected member name after '?.'."));
            if (check(TokenType::Less) && lookahead_generic_call_type_arguments()) {
                ZEPHYR_TRY_ASSIGN(type_arguments, parse_type_arguments());
                auto call = std::make_unique<OptionalCallExpr>();
                call->span = chain_span;
                call->object = std::move(expr);
                call->member = member_name.lexeme;
                call->type_arguments = std::move(type_arguments);
                ZEPHYR_TRY(consume(TokenType::LeftParen, "Expected '(' after type arguments."));
                if (!check(TokenType::RightParen)) {
                    do {
                        ZEPHYR_TRY_ASSIGN(argument, parse_expression());
                        call->arguments.push_back(std::move(argument));
                    } while (match({TokenType::Comma}));
                }
                ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after arguments."));
                expr = std::move(call);
            } else if (match({TokenType::LeftParen})) {
                auto call = std::make_unique<OptionalCallExpr>();
                call->span = chain_span;
                call->object = std::move(expr);
                call->member = member_name.lexeme;
                if (!check(TokenType::RightParen)) {
                    do {
                        ZEPHYR_TRY_ASSIGN(argument, parse_expression());
                        call->arguments.push_back(std::move(argument));
                    } while (match({TokenType::Comma}));
                }
                ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after arguments."));
                expr = std::move(call);
            } else {
                auto member = std::make_unique<OptionalMemberExpr>();
                member->span = chain_span;
                member->object = std::move(expr);
                member->member = member_name.lexeme;
                expr = std::move(member);
            }
        } else if (match({TokenType::LeftBracket})) {
            auto index = std::make_unique<IndexExpr>();
            index->span = previous().span;
            index->object = std::move(expr);
            ZEPHYR_TRY_ASSIGN(index_expr, parse_expression());
            index->index = std::move(index_expr);
            ZEPHYR_TRY(consume(TokenType::RightBracket, "Expected ']' after index expression."));
            expr = std::move(index);
        } else {
            break;
        }
    }
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_primary() {
    if (check(TokenType::FStringStart)) {
        return parse_interpolated_string();
    }
    if (match({TokenType::Integer})) {
        auto expr = std::make_unique<LiteralExpr>();
        expr->span = previous().span;
        ZEPHYR_TRY_ASSIGN(integer_value, parse_integer_literal(previous().lexeme, module_name_, previous().span));
        expr->value = integer_value;
        return expr;
    }
    if (match({TokenType::Float})) {
        auto expr = std::make_unique<LiteralExpr>();
        expr->span = previous().span;
        ZEPHYR_TRY_ASSIGN(float_value, parse_float_literal(previous().lexeme, module_name_, previous().span));
        expr->value = float_value;
        return expr;
    }
    if (match({TokenType::String})) {
        auto expr = std::make_unique<LiteralExpr>();
        expr->span = previous().span;
        expr->value = previous().lexeme;
        return expr;
    }
    if (match({TokenType::KeywordTrue})) {
        auto expr = std::make_unique<LiteralExpr>();
        expr->span = previous().span;
        expr->value = true;
        return expr;
    }
    if (match({TokenType::KeywordFalse})) {
        auto expr = std::make_unique<LiteralExpr>();
        expr->span = previous().span;
        expr->value = false;
        return expr;
    }
    if (match({TokenType::KeywordNil})) {
        auto expr = std::make_unique<LiteralExpr>();
        expr->span = previous().span;
        expr->value = std::monostate{};
        return expr;
    }
    if (match({TokenType::LeftParen})) {
        auto expr = std::make_unique<GroupExpr>();
        expr->span = previous().span;
        ZEPHYR_TRY_ASSIGN(inner, parse_expression());
        expr->inner = std::move(inner);
        ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after expression."));
        return expr;
    }
    if (match({TokenType::LeftBracket})) {
        auto expr = std::make_unique<ArrayExpr>();
        expr->span = previous().span;
        if (!check(TokenType::RightBracket)) {
            do {
                ZEPHYR_TRY_ASSIGN(element, parse_expression());
                expr->elements.push_back(std::move(element));
            } while (match({TokenType::Comma}));
        }
        ZEPHYR_TRY(consume(TokenType::RightBracket, "Expected ']' after array literal."));
        return expr;
    }
    if (match({TokenType::KeywordFn})) {
        auto expr = std::make_unique<FunctionExpr>();
        expr->span = previous().span;
        ZEPHYR_TRY(parse_function_signature(expr->params, expr->return_type));
        ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected '{' after function signature."));
        expr->body = std::move(body);
        return expr;
    }
    if (match({TokenType::KeywordCoroutine})) {
        auto expr = std::make_unique<CoroutineExpr>();
        expr->span = previous().span;
        ZEPHYR_TRY(consume(TokenType::KeywordFn, "Expected 'fn' after 'coroutine'."));
        ZEPHYR_TRY(parse_function_signature(expr->params, expr->return_type));
        ZEPHYR_TRY_ASSIGN(body, parse_block_stmt("Expected '{' after coroutine signature."));
        expr->body = std::move(body);
        return expr;
    }
    if (match({TokenType::KeywordMatch})) {
        return parse_match_expr();
    }
    if (check(TokenType::Identifier)) {
        return parse_identifier_led_expression();
    }
    return make_loc_error<ExprPtr>(module_name_, peek().span, "Expected expression.");
}

RuntimeResult<ExprPtr> Parser::parse_interpolated_string() {
    ZEPHYR_TRY_ASSIGN(start, consume(TokenType::FStringStart, "Expected formatted string start."));
    auto expr = std::make_unique<InterpolatedStringExpr>();
    expr->span = start.span;

    while (!check(TokenType::FStringEnd) && !is_at_end()) {
        if (match({TokenType::String})) {
            InterpolatedStringSegment segment;
            segment.is_literal = true;
            segment.literal = previous().lexeme;
            expr->segments.push_back(std::move(segment));
            continue;
        }
        if (match({TokenType::LeftBrace})) {
            InterpolatedStringSegment segment;
            segment.is_literal = false;
            ZEPHYR_TRY_ASSIGN(value, parse_expression());
            segment.expression = std::move(value);
            ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after interpolation expression."));
            expr->segments.push_back(std::move(segment));
            continue;
        }
        return make_loc_error<ExprPtr>(module_name_, peek().span, "Expected string text or interpolation in formatted string.");
    }

    ZEPHYR_TRY(consume(TokenType::FStringEnd, "Expected end of formatted string."));
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_identifier_led_expression() {
    const Span start = peek().span;
    TypeRef path;
    path.span = start;
    ZEPHYR_TRY_ASSIGN(identifier, consume(TokenType::Identifier, "Expected identifier."));
    path.parts.push_back(identifier.lexeme);
    while (match({TokenType::DoubleColon})) {
        ZEPHYR_TRY_ASSIGN(segment, consume(TokenType::Identifier, "Expected path segment after '::'."));
        path.parts.push_back(segment.lexeme);
    }

    const bool type_like = !path.parts.empty() && !path.parts.front().empty() &&
                           std::isupper(static_cast<unsigned char>(path.parts.front().front()));

    if (type_like && check(TokenType::LeftBrace)) {
        auto expr = std::make_unique<StructInitExpr>();
        expr->span = start;
        expr->type_name = path;
        ZEPHYR_TRY(consume(TokenType::LeftBrace, "Expected '{' after struct name."));
        if (!check(TokenType::RightBrace)) {
            do {
                StructFieldInit field;
                field.span = peek().span;
                ZEPHYR_TRY_ASSIGN(field_name, consume(TokenType::Identifier, "Expected field name."));
                field.name = field_name.lexeme;
                ZEPHYR_TRY(consume(TokenType::Colon, "Expected ':' after field name."));
                ZEPHYR_TRY_ASSIGN(field_value, parse_expression());
                field.value = std::move(field_value);
                expr->fields.push_back(std::move(field));
            } while (match({TokenType::Comma}));
        }
        ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after struct literal."));
        return expr;
    }

    if (path.parts.size() >= 2) {
        auto expr = std::make_unique<EnumInitExpr>();
        expr->span = start;
        expr->variant_name = path.parts.back();
        path.parts.pop_back();
        expr->enum_name = path;
        if (match({TokenType::LeftParen})) {
            if (!check(TokenType::RightParen)) {
                do {
                    ZEPHYR_TRY_ASSIGN(argument, parse_expression());
                    expr->arguments.push_back(std::move(argument));
                } while (match({TokenType::Comma}));
            }
            ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after enum constructor arguments."));
        }
        return expr;
    }

    auto expr = std::make_unique<VariableExpr>();
    expr->span = start;
    expr->name = path.parts.front();
    return expr;
}

RuntimeResult<ExprPtr> Parser::parse_match_expr() {
    auto expr = std::make_unique<MatchExpr>();
    expr->span = previous().span;
    ZEPHYR_TRY_ASSIGN(subject, parse_expression());
    expr->subject = std::move(subject);
    ZEPHYR_TRY(consume(TokenType::LeftBrace, "Expected '{' after match subject."));
    while (!check(TokenType::RightBrace) && !is_at_end()) {
        MatchArm arm;
        ZEPHYR_TRY_ASSIGN(pattern, parse_pattern());
        arm.pattern = std::move(pattern);
        if (match({TokenType::KeywordIf})) {
            ZEPHYR_TRY_ASSIGN(guard_expr, parse_expression());
            arm.guard_expr = std::move(guard_expr);
        }
        ZEPHYR_TRY(consume(TokenType::FatArrow, "Expected '=>' after match pattern."));
        ZEPHYR_TRY_ASSIGN(expression, parse_expression());
        arm.expression = std::move(expression);
        expr->arms.push_back(std::move(arm));
        if (!match({TokenType::Comma})) {
            break;
        }
    }
    ZEPHYR_TRY(consume(TokenType::RightBrace, "Expected '}' after match arms."));
    return expr;
}

RuntimeResult<PatternPtr> Parser::parse_pattern() {
    ZEPHYR_TRY_ASSIGN(pattern, parse_pattern_primary());
    if (!match({TokenType::Pipe})) {
        return pattern;
    }

    auto or_pattern = std::make_unique<OrPattern>();
    or_pattern->span = pattern->span;
    or_pattern->alternatives.push_back(std::move(pattern));
    do {
        ZEPHYR_TRY_ASSIGN(alternative, parse_pattern_primary());
        or_pattern->alternatives.push_back(std::move(alternative));
    } while (match({TokenType::Pipe}));
    return or_pattern;
}

RuntimeResult<PatternPtr> Parser::parse_pattern_primary() {
    if (match({TokenType::Identifier})) {
        const Token token = previous();
        if (token.lexeme == "_") {
            auto wildcard = std::make_unique<WildcardPattern>();
            wildcard->span = token.span;
            return wildcard;
        }

        TypeRef path;
        path.span = token.span;
        path.parts.push_back(token.lexeme);
        while (match({TokenType::DoubleColon})) {
            ZEPHYR_TRY_ASSIGN(segment, consume(TokenType::Identifier, "Expected pattern segment after '::'."));
            path.parts.push_back(segment.lexeme);
        }

        if (path.parts.size() >= 2) {
            auto pattern = std::make_unique<EnumPattern>();
            pattern->span = token.span;
            pattern->variant_name = path.parts.back();
            path.parts.pop_back();
            pattern->enum_name = path;
            if (match({TokenType::LeftParen})) {
                if (!check(TokenType::RightParen)) {
                    do {
                        ZEPHYR_TRY_ASSIGN(payload_pattern, parse_pattern());
                        pattern->payload.push_back(std::move(payload_pattern));
                    } while (match({TokenType::Comma}));
                }
                ZEPHYR_TRY(consume(TokenType::RightParen, "Expected ')' after enum pattern payload."));
            }
            return pattern;
        }

        auto binding = std::make_unique<BindingPattern>();
        binding->span = token.span;
        binding->name = token.lexeme;
        return binding;
    }

    if (match({TokenType::Integer})) {
        auto pattern = std::make_unique<LiteralPattern>();
        pattern->span = previous().span;
        ZEPHYR_TRY_ASSIGN(integer_value, parse_integer_literal(previous().lexeme, module_name_, previous().span));
        pattern->value = integer_value;
        return pattern;
    }
    if (match({TokenType::Float})) {
        auto pattern = std::make_unique<LiteralPattern>();
        pattern->span = previous().span;
        ZEPHYR_TRY_ASSIGN(float_value, parse_float_literal(previous().lexeme, module_name_, previous().span));
        pattern->value = float_value;
        return pattern;
    }
    if (match({TokenType::String})) {
        auto pattern = std::make_unique<LiteralPattern>();
        pattern->span = previous().span;
        pattern->value = previous().lexeme;
        return pattern;
    }
    if (match({TokenType::KeywordTrue})) {
        auto pattern = std::make_unique<LiteralPattern>();
        pattern->span = previous().span;
        pattern->value = true;
        return pattern;
    }
    if (match({TokenType::KeywordFalse})) {
        auto pattern = std::make_unique<LiteralPattern>();
        pattern->span = previous().span;
        pattern->value = false;
        return pattern;
    }
    if (match({TokenType::KeywordNil})) {
        auto pattern = std::make_unique<LiteralPattern>();
        pattern->span = previous().span;
        pattern->value = std::monostate{};
        return pattern;
    }

    return make_loc_error<PatternPtr>(module_name_, peek().span, "Expected match pattern.");
}

