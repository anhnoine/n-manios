#include "mnos.h"

typedef struct {
    const char *src;
    int pos;
    int line;
    int col;
} LexState;

static Token *token_buf = NULL;
static int token_buf_cap = 0;
static int token_buf_count = 0;

static void add_token(TType type, const char *text, int line, int col, double fval, long long ival) {
    if (token_buf_count >= token_buf_cap) {
        token_buf_cap = token_buf_cap ? token_buf_cap * 2 : 4096;
        token_buf = realloc(token_buf, sizeof(Token) * token_buf_cap);
    }
    Token *t = &token_buf[token_buf_count++];
    memset(t, 0, sizeof(Token));
    t->type = type;
    t->line = line;
    t->col = col;
    t->fval = fval;
    t->ival = ival;
    strncpy(t->text, text, MAX_TOKEN - 1);
}

static char peek(LexState *ls) {
    if (ls->src[ls->pos] == '\0') return '\0';
    return ls->src[ls->pos];
}

static char advance(LexState *ls) {
    char c = ls->src[ls->pos];
    if (c == '\0') return c;
    ls->pos++;
    if (c == '\n') { ls->line++; ls->col = 1; } else { ls->col++; }
    return c;
}

static void skip_line(LexState *ls) {
    while (peek(ls) != '\0' && peek(ls) != '\n') advance(ls);
}

static void skip_space(LexState *ls) {
    while (peek(ls) != '\0') {
        char c = peek(ls);
        if (c == ' ' || c == '\t' || c == '\r') { advance(ls); }
        else if (c == '#') { skip_line(ls); }
        else break;
    }
}

static int is_ident_start(char c) {
    return isalpha(c) || c == '_';
}

static int is_ident_char(char c) {
    return isalnum(c) || c == '_';
}

static void read_string(LexState *ls, char quote) {
    char buf[MAX_TOKEN];
    int bi = 0;
    advance(ls);
    while (peek(ls) != '\0' && peek(ls) != quote) {
        char c = advance(ls);
        if (c == '\\') {
            char nc = advance(ls);
            switch (nc) {
                case 'n': c = '\n'; break;
                case 't': c = '\t'; break;
                case 'r': c = '\r'; break;
                case '\\': c = '\\'; break;
                case '"': c = '"'; break;
                case '\'': c = '\''; break;
                case '0': c = '\0'; break;
                default: c = nc; break;
            }
        }
        if (bi < MAX_TOKEN - 1) buf[bi++] = c;
    }
    if (peek(ls) == quote) advance(ls);
    buf[bi] = '\0';
    add_token(T_STR, buf, ls->line, ls->col, 0, 0);
}

static void read_number(LexState *ls) {
    char buf[MAX_TOKEN];
    int bi = 0;
    int is_float = 0;
    while (peek(ls) != '\0' && (isdigit(peek(ls)) || peek(ls) == '.' || peek(ls) == '_')) {
        if (peek(ls) == '.') is_float = 1;
        if (peek(ls) != '_' && bi < MAX_TOKEN - 1) buf[bi++] = advance(ls);
        else advance(ls);
    }
    buf[bi] = '\0';
    if (is_float) {
        add_token(T_NUM_FLT, buf, ls->line, ls->col, atof(buf), 0);
    } else {
        add_token(T_NUM_INT, buf, ls->line, ls->col, 0, atoll(buf));
    }
}

static void read_ident(LexState *ls) {
    char buf[MAX_TOKEN];
    int bi = 0;
    while (peek(ls) != '\0' && is_ident_char(peek(ls))) {
        if (bi < MAX_TOKEN - 1) buf[bi++] = advance(ls);
        else advance(ls);
    }
    buf[bi] = '\0';

    static const struct { const char *kw; TType tt; } keywords[] = {
        {"hold", T_HOLD}, {"set", T_SET}, {"yell", T_YELL}, {"yell!", T_YELLN},
        {"check", T_CHECK}, {"or", T_OR}, {"end", T_END},
        {"spin", T_SPIN}, {"from", T_FROM}, {"to", T_TO}, {"times", T_TIMES}, {"while", T_WHILE},
        {"craft", T_CRAFT}, {"give", T_GIVE}, {"form", T_FORM},
        {"inp", T_INP}, {"grab", T_GRAB}, {"into", T_INTO},
        {"push", T_PUSH}, {"pop", T_POP}, {"halt", T_HALT},
        {"true", T_TRUE}, {"false", T_FALSE}, {"none", T_NONE},
        {"and", T_AND}, {"not", T_NOT},
        {"shield", T_SHIELD}, {"when", T_WHEN}, {"raise", T_RAISE},
        {"vault", T_VAULT}, {"pipe", T_PIPE},
        {NULL, T_UNKNOWN}
    };

    for (int i = 0; keywords[i].kw; i++) {
        if (strcmp(buf, keywords[i].kw) == 0) {
            add_token(keywords[i].tt, buf, ls->line, ls->col, 0, 0);
            return;
        }
    }
    add_token(T_IDENT, buf, ls->line, ls->col, 0, 0);
}

Token *lex(const char *src, int *ntokens) {
    LexState ls;
    ls.src = src;
    ls.pos = 0;
    ls.line = 1;
    ls.col = 1;
    token_buf_count = 0;

    while (peek(&ls) != '\0') {
        skip_space(&ls);
        if (peek(&ls) == '\0') break;
        char c = peek(&ls);

        if (c == '\n') {
            add_token(T_NEWLINE, "\\n", ls.line, ls.col, 0, 0);
            advance(&ls);
            skip_space(&ls);
        } else if (c == '"' || c == '\'') {
            read_string(&ls, c);
        } else if (isdigit(c)) {
            read_number(&ls);
        } else if (is_ident_start(c)) {
            read_ident(&ls);
        } else if (c == '+') { advance(&ls); add_token(T_PLUS, "+", ls.line, ls.col, 0, 0); }
        else if (c == '-') { advance(&ls); add_token(T_MINUS, "-", ls.line, ls.col, 0, 0); }
        else if (c == '*') { advance(&ls); add_token(T_STAR, "*", ls.line, ls.col, 0, 0); }
        else if (c == '/') { advance(&ls); add_token(T_SLASH, "/", ls.line, ls.col, 0, 0); }
        else if (c == '%') { advance(&ls); add_token(T_PERCENT, "%", ls.line, ls.col, 0, 0); }
        else if (c == '^') { advance(&ls); add_token(T_CARET, "^", ls.line, ls.col, 0, 0); }
        else if (c == '(') { advance(&ls); add_token(T_LPAREN, "(", ls.line, ls.col, 0, 0); }
        else if (c == ')') { advance(&ls); add_token(T_RPAREN, ")", ls.line, ls.col, 0, 0); }
        else if (c == '[') { advance(&ls); add_token(T_LBRACKET, "[", ls.line, ls.col, 0, 0); }
        else if (c == ']') { advance(&ls); add_token(T_RBRACKET, "]", ls.line, ls.col, 0, 0); }
        else if (c == '{') { advance(&ls); add_token(T_LBRACE, "{", ls.line, ls.col, 0, 0); }
        else if (c == '}') { advance(&ls); add_token(T_RBRACE, "}", ls.line, ls.col, 0, 0); }
        else if (c == ',') { advance(&ls); add_token(T_COMMA, ",", ls.line, ls.col, 0, 0); }
        else if (c == '.') { advance(&ls); add_token(T_DOT, ".", ls.line, ls.col, 0, 0); }
        else if (c == ':') { advance(&ls); add_token(T_COLON, ":", ls.line, ls.col, 0, 0); }
        else if (c == ';') { advance(&ls); add_token(T_SEMI, ";", ls.line, ls.col, 0, 0); }
        else if (c == '=' && ls.src[ls.pos + 1] == '=') {
            advance(&ls); advance(&ls);
            add_token(T_EQEQ, "==", ls.line, ls.col, 0, 0);
        } else if (c == '=') {
            advance(&ls); add_token(T_ASSIGN, "=", ls.line, ls.col, 0, 0);
        } else if (c == '!' && ls.src[ls.pos + 1] == '=') {
            advance(&ls); advance(&ls);
            add_token(T_NEQ, "!=", ls.line, ls.col, 0, 0);
        } else if (c == '<' && ls.src[ls.pos + 1] == '=') {
            advance(&ls); advance(&ls);
            add_token(T_LE, "<=", ls.line, ls.col, 0, 0);
        } else if (c == '>' && ls.src[ls.pos + 1] == '=') {
            advance(&ls); advance(&ls);
            add_token(T_GE, ">=", ls.line, ls.col, 0, 0);
        } else if (c == '<') { advance(&ls); add_token(T_LT, "<", ls.line, ls.col, 0, 0); }
        else if (c == '>') { advance(&ls); add_token(T_GT, ">", ls.line, ls.col, 0, 0); }
        else { advance(&ls); }
    }
    add_token(T_EOF, "", ls.line, ls.col, 0, 0);

    Token *result = malloc(sizeof(Token) * token_buf_count);
    memcpy(result, token_buf, sizeof(Token) * token_buf_count);
    *ntokens = token_buf_count;
    return result;
}