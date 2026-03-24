// Part of src/zephyr.cpp — included by zephyr.cpp
class Lexer {
public:
    Lexer(std::string source, std::string module_name, std::size_t line = 1, std::size_t column = 1)
        : source_(std::move(source)), module_name_(std::move(module_name)), line_(line), column_(column) {}

    RuntimeResult<std::vector<Token>> scan_tokens() {
        std::vector<Token> tokens;
        while (!is_at_end()) {
            skip_whitespace_and_comments();
            if (is_at_end()) {
                break;
            }

            const Span start{line_, column_};
            const char ch = advance();
            switch (ch) {
                case '(':
                    tokens.push_back(make_token(TokenType::LeftParen, "(", start));
                    break;
                case ')':
                    tokens.push_back(make_token(TokenType::RightParen, ")", start));
                    break;
                case '{':
                    tokens.push_back(make_token(TokenType::LeftBrace, "{", start));
                    break;
                case '}':
                    tokens.push_back(make_token(TokenType::RightBrace, "}", start));
                    break;
                case '[':
                    tokens.push_back(make_token(TokenType::LeftBracket, "[", start));
                    break;
                case ']':
                    tokens.push_back(make_token(TokenType::RightBracket, "]", start));
                    break;
                case ',':
                    tokens.push_back(make_token(TokenType::Comma, ",", start));
                    break;
                case '.':
                    tokens.push_back(make_token(TokenType::Dot, ".", start));
                    break;
                case ';':
                    tokens.push_back(make_token(TokenType::Semicolon, ";", start));
                    break;
                case ':':
                    if (match(':')) {
                        tokens.push_back(make_token(TokenType::DoubleColon, "::", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Colon, ":", start));
                    }
                    break;
                case '+':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::PlusEqual, "+=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Plus, "+", start));
                    }
                    break;
                case '-':
                    if (match('>')) {
                        tokens.push_back(make_token(TokenType::Arrow, "->", start));
                    } else if (match('=')) {
                        tokens.push_back(make_token(TokenType::MinusEqual, "-=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Minus, "-", start));
                    }
                    break;
                case '*':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::StarEqual, "*=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Star, "*", start));
                    }
                    break;
                case '/':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::SlashEqual, "/=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Slash, "/", start));
                    }
                    break;
                case '%':
                    tokens.push_back(make_token(TokenType::Percent, "%", start));
                    break;
                case '=':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::EqualEqual, "==", start));
                    } else if (match('>')) {
                        tokens.push_back(make_token(TokenType::FatArrow, "=>", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Equal, "=", start));
                    }
                    break;
                case '!':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::BangEqual, "!=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Bang, "!", start));
                    }
                    break;
                case '<':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::LessEqual, "<=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Less, "<", start));
                    }
                    break;
                case '>':
                    if (match('=')) {
                        tokens.push_back(make_token(TokenType::GreaterEqual, ">=", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Greater, ">", start));
                    }
                    break;
                case '?':
                    if (match('.')) {
                        tokens.push_back(make_token(TokenType::QuestionDot, "?.", start));
                    } else {
                        return make_loc_error<std::vector<Token>>(module_name_, start, "Unexpected character '?'.");
                    }
                    break;
                case '&':
                    if (!match('&')) {
                        return make_loc_error<std::vector<Token>>(module_name_, start, "Expected '&' after '&'.");
                    }
                    tokens.push_back(make_token(TokenType::AndAnd, "&&", start));
                    break;
                case '|':
                    if (match('|')) {
                        tokens.push_back(make_token(TokenType::OrOr, "||", start));
                    } else {
                        tokens.push_back(make_token(TokenType::Pipe, "|", start));
                    }
                    break;
                case '"': {
                    ZEPHYR_TRY_ASSIGN(string_token, scan_string(start));
                    tokens.push_back(std::move(string_token));
                    break;
                }
                default:
                    if (std::isdigit(static_cast<unsigned char>(ch))) {
                        tokens.push_back(scan_number(start, ch));
                    } else if (ch == 'f' && peek() == '"') {
                        advance();
                        ZEPHYR_TRY(scan_fstring(start, tokens));
                    } else if (is_identifier_start(ch)) {
                        tokens.push_back(scan_identifier(start, ch));
                    } else {
                        return make_loc_error<std::vector<Token>>(module_name_, start, std::string("Unexpected character '") + ch + "'.");
                    }
                    break;
            }
        }

        tokens.push_back(Token{TokenType::EndOfFile, "", Span{line_, column_}});
        return tokens;
    }

private:
    bool is_at_end() const { return index_ >= source_.size(); }
    char peek() const { return is_at_end() ? '\0' : source_[index_]; }
    char peek_next() const { return index_ + 1 >= source_.size() ? '\0' : source_[index_ + 1]; }

    char advance() {
        const char ch = source_[index_++];
        if (ch == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return ch;
    }

    bool match(char expected) {
        if (is_at_end() || source_[index_] != expected) {
            return false;
        }
        advance();
        return true;
    }

    void skip_whitespace_and_comments() {
        while (!is_at_end()) {
            const char ch = peek();
            if (ch == ' ' || ch == '\r' || ch == '\t' || ch == '\n') {
                advance();
                continue;
            }
            if (ch == '/' && peek_next() == '/') {
                while (!is_at_end() && peek() != '\n') {
                    advance();
                }
                continue;
            }
            break;
        }
    }

    static bool is_identifier_start(char ch) {
        return std::isalpha(static_cast<unsigned char>(ch)) || ch == '_';
    }

    static bool is_identifier_part(char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
    }

    Token make_token(TokenType type, std::string lexeme, const Span& span) const {
        return Token{type, std::move(lexeme), span};
    }

    RuntimeResult<Token> scan_string(const Span& start) {
        std::string value;
        while (!is_at_end() && peek() != '"') {
            const char ch = advance();
            if (ch == '\\') {
                if (is_at_end()) {
                    return make_loc_error<Token>(module_name_, start, "Unterminated escape sequence.");
                }
                const char escaped = advance();
                switch (escaped) {
                    case 'n':
                        value.push_back('\n');
                        break;
                    case 't':
                        value.push_back('\t');
                        break;
                    case '"':
                        value.push_back('"');
                        break;
                    case '\\':
                        value.push_back('\\');
                        break;
                    default:
                        return make_loc_error<Token>(module_name_, start, "Unsupported escape sequence.");
                }
            } else {
                value.push_back(ch);
            }
        }

        if (is_at_end()) {
            return make_loc_error<Token>(module_name_, start, "Unterminated string literal.");
        }

        advance();
        return make_token(TokenType::String, value, start);
    }

    RuntimeResult<std::string> consume_interpolation_source(const Span& start) {
        std::string expression_source;
        int brace_depth = 1;
        bool in_string = false;
        bool escaped = false;

        while (!is_at_end()) {
            const char ch = advance();
            if (in_string) {
                expression_source.push_back(ch);
                if (escaped) {
                    escaped = false;
                } else if (ch == '\\') {
                    escaped = true;
                } else if (ch == '"') {
                    in_string = false;
                }
                continue;
            }

            if (ch == '"') {
                in_string = true;
                expression_source.push_back(ch);
                continue;
            }
            if (ch == '{') {
                ++brace_depth;
                expression_source.push_back(ch);
                continue;
            }
            if (ch == '}') {
                --brace_depth;
                if (brace_depth == 0) {
                    return expression_source;
                }
                expression_source.push_back(ch);
                continue;
            }

            expression_source.push_back(ch);
        }

        return make_loc_error<std::string>(module_name_, start, "Unterminated interpolation expression.");
    }

    VoidResult scan_fstring(const Span& start, std::vector<Token>& tokens) {
        tokens.push_back(make_token(TokenType::FStringStart, "f\"", start));
        std::string literal;
        Span literal_start = start;

        auto flush_literal = [&]() {
            if (!literal.empty()) {
                tokens.push_back(make_token(TokenType::String, literal, literal_start));
                literal.clear();
            }
        };

        while (!is_at_end()) {
            const Span current{line_, column_};
            const char ch = advance();
            if (ch == '"') {
                flush_literal();
                tokens.push_back(make_token(TokenType::FStringEnd, "\"", current));
                return ok_result();
            }
            if (ch == '{') {
                flush_literal();
                tokens.push_back(make_token(TokenType::LeftBrace, "{", current));
                const Span expression_start{line_, column_};
                ZEPHYR_TRY_ASSIGN(expression_source, consume_interpolation_source(current));
                Lexer nested_lexer(std::move(expression_source), module_name_, expression_start.line, expression_start.column);
                ZEPHYR_TRY_ASSIGN(nested_tokens, nested_lexer.scan_tokens());
                if (!nested_tokens.empty() && nested_tokens.back().type == TokenType::EndOfFile) {
                    nested_tokens.pop_back();
                }
                tokens.insert(tokens.end(), std::make_move_iterator(nested_tokens.begin()), std::make_move_iterator(nested_tokens.end()));
                tokens.push_back(make_token(TokenType::RightBrace, "}", Span{line_, column_ - 1}));
                literal_start = Span{line_, column_};
                continue;
            }
            if (ch == '\\') {
                if (is_at_end()) {
                    return make_loc_error<std::monostate>(module_name_, start, "Unterminated escape sequence.");
                }
                const char escaped = advance();
                switch (escaped) {
                    case 'n':
                        literal.push_back('\n');
                        break;
                    case 't':
                        literal.push_back('\t');
                        break;
                    case '"':
                        literal.push_back('"');
                        break;
                    case '\\':
                        literal.push_back('\\');
                        break;
                    case '{':
                        literal.push_back('{');
                        break;
                    case '}':
                        literal.push_back('}');
                        break;
                    default:
                        return make_loc_error<std::monostate>(module_name_, start, "Unsupported escape sequence.");
                }
                continue;
            }
            if (literal.empty()) {
                literal_start = current;
            }
            literal.push_back(ch);
        }

        return make_loc_error<std::monostate>(module_name_, start, "Unterminated formatted string literal.");
    }

    Token scan_number(const Span& start, char first) {
        std::string value(1, first);
        while (std::isdigit(static_cast<unsigned char>(peek()))) {
            value.push_back(advance());
        }
        if (peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
            value.push_back(advance());
            while (std::isdigit(static_cast<unsigned char>(peek()))) {
                value.push_back(advance());
            }
            return make_token(TokenType::Float, value, start);
        }
        return make_token(TokenType::Integer, value, start);
    }

    Token scan_identifier(const Span& start, char first) {
        std::string value(1, first);
        while (is_identifier_part(peek())) {
            value.push_back(advance());
        }

        static const std::unordered_map<std::string, TokenType> keywords = {
            {"fn", TokenType::KeywordFn},               {"coroutine", TokenType::KeywordCoroutine}, {"yield", TokenType::KeywordYield},
            {"resume", TokenType::KeywordResume},       {"let", TokenType::KeywordLet},             {"mut", TokenType::KeywordMut},
            {"if", TokenType::KeywordIf},               {"else", TokenType::KeywordElse},           {"while", TokenType::KeywordWhile},
            {"for", TokenType::KeywordFor},             {"trait", TokenType::KeywordTrait},         {"impl", TokenType::KeywordImpl},
            {"in", TokenType::KeywordIn},               {"break", TokenType::KeywordBreak},
            {"continue", TokenType::KeywordContinue},   {"return", TokenType::KeywordReturn},
            {"struct", TokenType::KeywordStruct},       {"enum", TokenType::KeywordEnum},           {"match", TokenType::KeywordMatch},
            {"import", TokenType::KeywordImport},       {"export", TokenType::KeywordExport},       {"as", TokenType::KeywordAs},
            {"true", TokenType::KeywordTrue},           {"false", TokenType::KeywordFalse},         {"nil", TokenType::KeywordNil},
        };

        const auto it = keywords.find(value);
        if (it != keywords.end()) {
            return make_token(it->second, value, start);
        }
        return make_token(TokenType::Identifier, value, start);
    }

    std::string source_;
    std::string module_name_;
    std::size_t index_ = 0;
    std::size_t line_ = 1;
    std::size_t column_ = 1;
};

struct TypeRef {
    std::vector<std::string> parts;
    Span span;

    std::string display_name() const {
        std::ostringstream out;
        for (std::size_t i = 0; i < parts.size(); ++i) {
            if (i > 0) {
                out << "::";
            }
            out << parts[i];
        }
        return out.str();
    }
};

struct Expr {
    Span span;
    virtual ~Expr() = default;
};

using ExprPtr = std::unique_ptr<Expr>;

struct Pattern {
    Span span;
    virtual ~Pattern() = default;
};

using PatternPtr = std::unique_ptr<Pattern>;

struct Stmt {
    Span span;
    bool exported = false;
    virtual ~Stmt() = default;
};

using StmtPtr = std::unique_ptr<Stmt>;

struct LiteralExpr final : Expr {
    std::variant<std::monostate, bool, std::int64_t, double, std::string> value;
};

struct InterpolatedStringSegment {
    std::string literal;
    ExprPtr expression;
    bool is_literal = false;
};

struct InterpolatedStringExpr final : Expr {
    std::vector<InterpolatedStringSegment> segments;
};

struct VariableExpr final : Expr {
    std::string name;
};

struct ArrayExpr final : Expr {
    std::vector<ExprPtr> elements;
};

struct GroupExpr final : Expr {
    ExprPtr inner;
};

struct UnaryExpr final : Expr {
    TokenType op = TokenType::EndOfFile;
    ExprPtr right;
};

struct BinaryExpr final : Expr {
    ExprPtr left;
    TokenType op = TokenType::EndOfFile;
    ExprPtr right;
};

struct AssignExpr final : Expr {
    ExprPtr target;
    ExprPtr value;
    TokenType assignment_op = TokenType::Equal;
};

struct MemberExpr final : Expr {
    ExprPtr object;
    std::string member;
};

struct OptionalMemberExpr final : Expr {
    ExprPtr object;
    std::string member;
};

struct IndexExpr final : Expr {
    ExprPtr object;
    ExprPtr index;
};

struct CallExpr final : Expr {
    ExprPtr callee;
    std::vector<ExprPtr> arguments;
};

struct OptionalCallExpr final : Expr {
    ExprPtr object;
    std::string member;
    std::vector<ExprPtr> arguments;
};

struct ResumeExpr final : Expr {
    ExprPtr target;
};

struct StructFieldInit {
    std::string name;
    ExprPtr value;
    Span span;
};

struct StructInitExpr final : Expr {
    TypeRef type_name;
    std::vector<StructFieldInit> fields;
};

struct EnumInitExpr final : Expr {
    TypeRef enum_name;
    std::string variant_name;
    std::vector<ExprPtr> arguments;
};

struct Param {
    std::string name;
    std::optional<TypeRef> type;
    Span span;
};

inline std::vector<std::string> encode_param_metadata(const std::vector<Param>& params) {
    std::vector<std::string> encoded;
    encoded.reserve(params.size() * 2);
    for (const auto& param : params) {
        encoded.push_back(param.name);
        encoded.push_back(param.type.has_value() ? param.type->display_name() : std::string{});
    }
    return encoded;
}

struct BlockStmt;

struct FunctionExpr final : Expr {
    std::vector<Param> params;
    std::optional<TypeRef> return_type;
    std::unique_ptr<BlockStmt> body;
};

struct CoroutineExpr final : Expr {
    std::vector<Param> params;
    std::optional<TypeRef> return_type;
    std::unique_ptr<BlockStmt> body;
};

struct WildcardPattern final : Pattern {};

struct LiteralPattern final : Pattern {
    std::variant<std::monostate, bool, std::int64_t, double, std::string> value;
};

struct BindingPattern final : Pattern {
    std::string name;
};

struct EnumPattern final : Pattern {
    TypeRef enum_name;
    std::string variant_name;
    std::vector<PatternPtr> payload;
};

struct OrPattern final : Pattern {
    std::vector<PatternPtr> alternatives;
};

struct MatchArm {
    PatternPtr pattern;
    ExprPtr guard_expr;
    ExprPtr expression;
};

struct MatchExpr final : Expr {
    ExprPtr subject;
    std::vector<MatchArm> arms;
};

struct ExprStmt final : Stmt {
    ExprPtr expression;
};

struct LetStmt final : Stmt {
    std::string name;
    bool mutable_value = false;
    std::optional<TypeRef> type;
    ExprPtr initializer;
};

struct BlockStmt final : Stmt {
    std::vector<StmtPtr> statements;
};

struct IfStmt final : Stmt {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> then_branch;
    StmtPtr else_branch;
};

struct WhileStmt final : Stmt {
    ExprPtr condition;
    std::unique_ptr<BlockStmt> body;
};

struct ForStmt final : Stmt {
    std::string name;
    ExprPtr iterable;
    std::unique_ptr<BlockStmt> body;
};

struct BreakStmt final : Stmt {};

struct ContinueStmt final : Stmt {};

struct ReturnStmt final : Stmt {
    ExprPtr value;
};

struct YieldStmt final : Stmt {
    ExprPtr value;
};

struct FunctionDecl final : Stmt {
    std::string name;
    std::vector<Param> params;
    std::optional<TypeRef> return_type;
    std::unique_ptr<BlockStmt> body;
};

struct TraitMethodDecl {
    Span span;
    std::string name;
    std::vector<Param> params;
    std::optional<TypeRef> return_type;
};

struct StructFieldDecl {
    std::string name;
    TypeRef type;
};

struct StructDecl final : Stmt {
    std::string name;
    std::vector<StructFieldDecl> fields;
};

struct EnumVariantDecl {
    std::string name;
    std::vector<TypeRef> payload_types;
};

struct EnumDecl final : Stmt {
    std::string name;
    std::vector<EnumVariantDecl> variants;
};

struct TraitDecl final : Stmt {
    std::string name;
    std::vector<TraitMethodDecl> methods;
};

struct ImplDecl final : Stmt {
    TypeRef trait_name;
    TypeRef for_type;
    std::vector<std::unique_ptr<FunctionDecl>> methods;
};

struct ImportStmt final : Stmt {
    std::string path;
    std::optional<std::string> alias;
};

struct Program {
    std::vector<StmtPtr> statements;
};
