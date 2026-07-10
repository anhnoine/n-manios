#include "mnos.h"

static Token *tk;
static int tpos;
static int ttotal;

static Node *new_node(NType type) {
    Node *n = calloc(1, sizeof(Node));
    n->type = type;
    return n;
}

static Token cur(void) { return tpos < ttotal ? tk[tpos] : tk[ttotal - 1]; }
static Token peek_tk(int off) { int p = tpos + off; return p < ttotal ? tk[p] : tk[ttotal - 1]; }
static Token eat(void) { Token t = cur(); if (tpos < ttotal) tpos++; return t; }
static int at(TType type) { return cur().type == type; }
static int at_kw(const char *kw) { return cur().type == T_IDENT && strcmp(cur().text, kw) == 0; }
static void expect(TType type) { if (!at(type)) { fprintf(stderr, "Loi dong %d: ky tu khong mong đoi '%s', can type %d\n", cur().line, cur().text, type); exit(1); } }
static Token expect_eat(TType type) { expect(type); return eat(); }
static void skip_newlines(void) { while (at(T_NEWLINE)) eat(); }

static int op_prec(int tt) {
    switch (tt) {
        case T_OR: return 1;
        case T_AND: return 2;
        case T_NOT: return 3;
        case T_EQEQ: case T_NEQ: return 4;
        case T_LT: case T_GT: case T_LE: case T_GE: return 5;
        case T_PLUS: case T_MINUS: return 6;
        case T_STAR: case T_SLASH: case T_PERCENT: return 7;
        case T_CARET: return 8;
        default: return 0;
    }
}

static int is_binop(TType tt) {
    return tt == T_PLUS || tt == T_MINUS || tt == T_STAR || tt == T_SLASH ||
           tt == T_PERCENT || tt == T_CARET || tt == T_EQEQ || tt == T_NEQ ||
           tt == T_LT || tt == T_GT || tt == T_LE || tt == T_GE ||
           tt == T_AND || tt == T_OR;
}

static Node *parse_expr(int min_prec);
static Node *parse_stmt(void);
static Node *parse_block(void);

static Node *parse_atom(void) {
    if (at(T_NUM_INT)) {
        Token t = eat();
        Node *n = new_node(N_INT_LIT);
        n->lit_val = val_int(t.ival);
        return n;
    }
    if (at(T_NUM_FLT)) {
        Token t = eat();
        Node *n = new_node(N_FLT_LIT);
        n->lit_val = val_flt(t.fval);
        return n;
    }
    if (at(T_STR)) {
        Token t = eat();
        Node *n = new_node(N_STR_LIT);
        n->lit_val = val_str(t.text);
        return n;
    }
    if (at(T_TRUE)) { eat(); Node *n = new_node(N_BOOL_LIT); n->lit_val = val_bool(1); return n; }
    if (at(T_FALSE)) { eat(); Node *n = new_node(N_BOOL_LIT); n->lit_val = val_bool(0); return n; }
    if (at(T_NONE)) { eat(); Node *n = new_node(N_NONE_LIT); n->lit_val = val_none(); return n; }
    if (at(T_IDENT) && strcmp(cur().text, "this") == 0) {
        eat(); return new_node(N_THIS);
    }
    if (at(T_VAULT)) {
        eat();
        expect(T_LBRACE); eat();
        Node *n = new_node(N_DICT_LIT);
        int cap = 0;
        n->items = NULL; n->nitems = 0;
        skip_newlines();
        while (!at(T_RBRACE) && !at(T_EOF)) {
            if (n->nitems > 0) { expect(T_COMMA); eat(); skip_newlines(); }
            if (n->nitems >= cap) { cap = cap ? cap * 2 : 8; n->items = realloc(n->items, sizeof(Node*) * cap); }
            n->items[n->nitems++] = parse_expr(0);
            expect(T_COLON); eat();
            n->items[n->nitems++] = parse_expr(0);
            skip_newlines();
        }
        expect(T_RBRACE); eat();
        return n;
    }
    if (at(T_LBRACKET)) {
        eat();
        Node *n = new_node(N_LIST_LIT);
        n->items = NULL; n->nitems = 0;
        int cap = 0;
        skip_newlines();
        while (!at(T_RBRACKET) && !at(T_EOF)) {
            if (n->nitems > 0) { expect(T_COMMA); eat(); skip_newlines(); }
            if (n->nitems >= cap) { cap = cap ? cap * 2 : 8; n->items = realloc(n->items, sizeof(Node*) * cap); }
            n->items[n->nitems++] = parse_expr(0);
            skip_newlines();
        }
        expect(T_RBRACKET); eat();
        return n;
    }
    if (at(T_LPAREN)) {
        eat();
        if (at(T_RPAREN)) {
            eat();
            Node *n = new_node(N_LIST_LIT);
            n->items = NULL; n->nitems = 0;
            return n;
        }
        Node *first = parse_expr(0);
        if (at(T_COMMA)) {
            eat();
            Node *n = new_node(N_TUPLE_LIT);
            n->items = malloc(sizeof(Node*) * 16);
            n->nitems = 1; n->items[0] = first;
            skip_newlines();
            while (!at(T_RPAREN) && !at(T_EOF)) {
                if (n->nitems >= 16) break;
                n->items[n->nitems++] = parse_expr(0);
                skip_newlines();
                if (at(T_COMMA)) { eat(); skip_newlines(); } else break;
            }
            expect(T_RPAREN); eat();
            return n;
        }
        expect(T_RPAREN); eat();
        return first;
    }
    if (at(T_MINUS)) {
        eat();
        Node *operand = parse_atom();
        Node *n = new_node(N_UNOP);
        n->op = T_MINUS;
        n->left = operand;
        return n;
    }
    if (at(T_NOT)) {
        eat();
        Node *operand = parse_atom();
        Node *n = new_node(N_NOT);
        n->left = operand;
        return n;
    }
    if (at(T_IDENT)) {
        Token t = eat();
        Node *n = new_node(N_IDENT);
        strncpy(n->name, t.text, 255);
        return n;
    }
    {
        Token t = eat();
        fprintf(stderr, "Loi dong %d: khong the doc bieu thuc tai '%s'\n", t.line, t.text);
        exit(1);
    }
}

static Node *parse_postfix(Node *base) {
    while (1) {
        if (at(T_LPAREN)) {
            eat();
            Node *n = new_node(N_CALL);
            n->left = base;
            n->items = NULL; n->nitems = 0;
            int cap = 0;
            skip_newlines();
            while (!at(T_RPAREN) && !at(T_EOF)) {
                if (n->nitems > 0) { expect(T_COMMA); eat(); skip_newlines(); }
                if (n->nitems >= cap) { cap = cap ? cap * 2 : 8; n->items = realloc(n->items, sizeof(Node*) * cap); }
                n->items[n->nitems++] = parse_expr(0);
                skip_newlines();
            }
            expect(T_RPAREN); eat();
            base = n;
        } else if (at(T_LBRACKET)) {
            eat();
            Node *idx = parse_expr(0);
            expect(T_RBRACKET); eat();
            Node *n = new_node(N_INDEX);
            n->left = base;
            n->right = idx;
            base = n;
        } else if (at(T_DOT)) {
            eat();
            Token t = expect_eat(T_IDENT);
            Node *n = new_node(N_DOT);
            strncpy(n->name, t.text, 255);
            n->left = base;
            base = n;
        } else {
            break;
        }
    }
    return base;
}

static Node *parse_expr(int min_prec) {
    Node *left = parse_postfix(parse_atom());
    while (is_binop(cur().type) && op_prec(cur().type) >= min_prec) {
        TType op = eat().type;
        if (op == T_AND) {
            Node *right = parse_expr(op_prec(op) + 1);
            Node *n = new_node(N_AND);
            n->left = left;
            n->right = right;
            left = n;
        } else {
            int next_min = op_prec(op) + 1;
            Node *right = parse_expr(next_min);
            Node *n = new_node(N_BINOP);
            n->op = op;
            n->left = left;
            n->right = right;
            left = n;
        }
    }
    return left;
}

static Node *parse_stmt(void) {
    skip_newlines();

    if (at(T_HOLD)) {
        eat();
        Token name = expect_eat(T_IDENT);
        skip_newlines();
        if (at(T_ASSIGN)) { eat(); skip_newlines(); }
        Node *val = parse_expr(0);
        Node *n = new_node(N_HOLD);
        strncpy(n->name, name.text, 255);
        n->left = val;
        return n;
    }

    if (at(T_SET)) {
        eat();
        Token name = expect_eat(T_IDENT);
        Node *n;
        if (at(T_LBRACKET)) {
            eat();
            Node *idx = parse_expr(0);
            expect(T_RBRACKET); eat();
            expect(T_ASSIGN); eat();
            skip_newlines();
            Node *val = parse_expr(0);
            n = new_node(N_SET);
            strncpy(n->name, name.text, 255);
            n->right = idx;
            n->left = val;
        } else if (at(T_DOT)) {
            eat();
            Token field = expect_eat(T_IDENT);
            expect(T_ASSIGN); eat();
            skip_newlines();
            Node *val = parse_expr(0);
            n = new_node(N_SET);
            snprintf(n->name, 256, "%s.%s", name.text, field.text);
            n->left = val;
            n->right = NULL;
        } else {
            expect(T_ASSIGN); eat();
            skip_newlines();
            Node *val = parse_expr(0);
            n = new_node(N_SET);
            strncpy(n->name, name.text, 255);
            n->left = val;
        }
        return n;
    }

    if (at(T_IDENT)) {
        Token name = eat();
        if (at(T_ASSIGN)) {
            eat(); skip_newlines();
            Node *val = parse_expr(0);
            Node *n = new_node(N_ASSIGN);
            strncpy(n->name, name.text, 255);
            n->left = val;
            return n;
        }
        if (at(T_PLUS) && peek_tk(1).type == T_ASSIGN) {
            eat(); eat(); skip_newlines();
            Node *val = parse_expr(0);
            Node *n = new_node(N_INPLACE_ADD);
            strncpy(n->name, name.text, 255);
            n->left = val;
            return n;
        }
        if (at(T_MINUS) && peek_tk(1).type == T_ASSIGN) {
            eat(); eat(); skip_newlines();
            Node *val = parse_expr(0);
            Node *n = new_node(N_INPLACE_SUB);
            strncpy(n->name, name.text, 255);
            n->left = val;
            return n;
        }
        if (at(T_STAR) && peek_tk(1).type == T_ASSIGN) {
            eat(); eat(); skip_newlines();
            Node *val = parse_expr(0);
            Node *n = new_node(N_INPLACE_MUL);
            strncpy(n->name, name.text, 255);
            n->left = val;
            return n;
        }
        if (at(T_SLASH) && peek_tk(1).type == T_ASSIGN) {
            eat(); eat(); skip_newlines();
            Node *val = parse_expr(0);
            Node *n = new_node(N_INPLACE_DIV);
            strncpy(n->name, name.text, 255);
            n->left = val;
            return n;
        }
        tpos--;
        Node *expr = parse_expr(0);
        return expr;
    }

    if (at(T_YELL) || at(T_YELLN)) {
        int nl = (cur().type == T_YELL);
        eat();
        skip_newlines();
        Node *val = parse_expr(0);
        Node *n = nl ? new_node(N_YELL) : new_node(N_YELLN);
        n->left = val;
        return n;
    }

    if (at(T_GIVE)) {
        eat();
        skip_newlines();
        Node *val = parse_expr(0);
        Node *n = new_node(N_GIVE);
        n->left = val;
        return n;
    }

    if (at(T_CHECK)) {
        eat();
        skip_newlines();
        Node *cond = parse_expr(0);
        skip_newlines();
        Node *body = parse_block();
        if (at(T_END)) eat();
        Node *elif_body = NULL;
        if (at(T_OR)) {
            eat(); skip_newlines();
            elif_body = parse_stmt();
        }
        Node *n = new_node(N_CHECK);
        n->cond = cond;
        n->body = body;
        n->elif_body = elif_body;
        return n;
    }

    if (at(T_SPIN)) {
        eat();
        if (at(T_WHILE)) {
            eat(); skip_newlines();
            Node *cond = parse_expr(0);
            Node *body = parse_block();
            if (at(T_END)) eat();
            Node *n = new_node(N_SPIN_WHILE);
            n->cond = cond;
            n->body = body;
            return n;
        }
        if (at(T_IDENT) && peek_tk(1).type == T_FROM) {
            Token var = eat();
            eat();
            skip_newlines();
            Node *start = parse_expr(0);
            expect(T_TO); eat();
            skip_newlines();
            Node *end = parse_expr(0);
            Node *body = parse_block();
            if (at(T_END)) eat();
            Node *n = new_node(N_SPIN_RANGE);
            strncpy(n->name, var.text, 255);
            n->cond = start;
            n->right = end;
            n->body = body;
            return n;
        }
        if (at(T_NUM_INT) || at(T_IDENT)) {
            Node *cnt = parse_atom();
            if (at(T_TIMES)) {
                eat();
                Node *body = parse_block();
                if (at(T_END)) eat();
                Node *n = new_node(N_SPIN);
                n->cond = cnt;
                n->body = body;
                return n;
            }
        }
        fprintf(stderr, "Loi: cu phap spin khong hop le\n");
        exit(1);
    }

    if (at(T_CRAFT)) {
        eat();
        Token name = expect_eat(T_IDENT);
        Node *n = new_node(N_CRAFT);
        strncpy(n->name, name.text, 255);
        n->params = NULL; n->nparams = 0;
        int cap = 0;
        skip_newlines();
        while (at(T_IDENT) && !at(T_END) && !at(T_NEWLINE)) {
            if (n->nparams >= cap) { cap = cap ? cap * 2 : 8; n->params = realloc(n->params, sizeof(char*) * cap); }
            n->params[n->nparams++] = strdup(eat().text);
        }
        skip_newlines();
        n->body = parse_block();
        if (at(T_END)) eat();
        return n;
    }

    if (at(T_FORM)) {
        eat();
        Token name = expect_eat(T_IDENT);
        skip_newlines();
        Node *body = parse_block();
        if (at(T_END)) eat();
        Node *n = new_node(N_FORM);
        strncpy(n->name, name.text, 255);
        n->body = body;
        return n;
    }

    if (at(T_INP)) {
        eat();
        Token name = expect_eat(T_IDENT);
        Node *n = new_node(N_INP);
        strncpy(n->name, name.text, 255);
        return n;
    }

    if (at(T_GRAB)) {
        eat();
        if (at(T_STR)) {
            eat();
            expect(T_INTO); eat();
            Token name = expect_eat(T_IDENT);
            Node *n = new_node(N_GRAB);
            strncpy(n->name, name.text, 255);
            return n;
        }
        expect(T_INTO); eat();
        Token name = expect_eat(T_IDENT);
        Node *n = new_node(N_GRAB);
        strncpy(n->name, name.text, 255);
        return n;
    }

    if (at(T_PUSH)) {
        eat();
        skip_newlines();
        Node *val = parse_expr(0);
        expect(T_INTO); eat();
        Token name = expect_eat(T_IDENT);
        Node *n = new_node(N_PUSH);
        strncpy(n->name, name.text, 255);
        n->left = val;
        return n;
    }

    if (at(T_POP)) {
        eat();
        expect(T_FROM); eat();
        Token name = expect_eat(T_IDENT);
        Node *n = new_node(N_POP);
        strncpy(n->name, name.text, 255);
        return n;
    }

    if (at(T_HALT)) {
        eat();
        return new_node(N_HALT);
    }

    if (at(T_PIPE)) {
        eat();
        skip_newlines();
        Node *n = new_node(N_INP);
        if (at(T_STR)) {
            Token t = eat();
            strncpy(n->name, t.text, 255);
        } else {
            Token name = expect_eat(T_IDENT);
            strncpy(n->name, name.text, 255);
        }
        return n;
    }

    if (at(T_SHIELD)) {
        eat();
        skip_newlines();
        Node *body = parse_block();
        Node *catch_body = NULL;
        char ex_name[256] = "";
        if (at(T_WHEN)) {
            eat();
            if (at(T_IDENT)) {
                Token t = eat();
                strncpy(ex_name, t.text, 255);
            }
            skip_newlines();
            catch_body = parse_block();
        }
        Node *n = new_node(N_SHIELD);
        n->body = body;
        n->elif_body = catch_body;
        strncpy(n->name, ex_name, 255);
        return n;
    }

    if (at(T_RAISE)) {
        eat();
        skip_newlines();
        Node *msg = parse_expr(0);
        Node *n = new_node(N_RAISE);
        n->left = msg;
        return n;
    }

    return parse_expr(0);
}

static Node *parse_block(void) {
    skip_newlines();
    if (at(T_END)) return NULL;
    if (at(T_EOF)) return NULL;
    if (at(T_OR)) return NULL;
    if (at(T_WHEN)) return NULL;

    Node head;
    memset(&head, 0, sizeof(Node));
    Node *tail = &head;

    while (1) {
        skip_newlines();
        if (at(T_END) || at(T_EOF) || at(T_OR) || at(T_WHEN)) break;
        Node *s = parse_stmt();
        if (!s) break;
        tail->next = s;
        tail = s;
        skip_newlines();
    }

    return head.next;
}

Node *parse(Token *tokens, int ntokens) {
    tk = tokens;
    tpos = 0;
    ttotal = ntokens;
    skip_newlines();
    return parse_block();
}

void node_free(Node *n) {
    if (!n) return;
    node_free(n->left);
    node_free(n->right);
    node_free(n->cond);
    node_free(n->body);
    node_free(n->elif_body);
    node_free(n->next);
    for (int i = 0; i < n->nitems; i++) node_free(n->items[i]);
    free(n->items);
    for (int i = 0; i < n->nparams; i++) free(n->params[i]);
    free(n->params);
    free(n->str_items);
    if (n->lit_val.type == V_STR) free(n->lit_val.sval);
}