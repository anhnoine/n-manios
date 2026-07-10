#ifndef MNOS_H
#define MNOS_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdint.h>
#include <limits.h>

#define MNOS_VER "1.0.0"
#define MNOS_MAGIC 0x4D4E4F53
#define MNOS_CACHE_EXT ".mnocache"
#define MNOS_SRC_EXT ".mno"
#define MAX_LINE 65536
#define MAX_TOKEN 8192
#define MAX_NODES 65536
#define MAX_ENV 4096
#define MAX_FNS 512
#define MAX_CLS 128
#define MAX_BUILTIN 256
#define MAX_CONST_POOL 4096
#define MAX_NAME_POOL 1024
#define MAX_CODE_SIZE 262144
#define MAX_STACK 1024
#define MNOS_SER_VERSION 1

typedef enum {
    V_NONE, V_INT, V_FLOAT, V_STR, V_BOOL, V_LIST, V_DICT, V_CLASS, V_OBJ, V_TUPLE, V_BYTES
} VType;

typedef struct Val {
    VType type;
    long long ival;
    double fval;
    int bval;
    char *sval;
    int slen;
    struct Val *li;
    int llen;
    int lcap;
    char **dkeys;
    struct Val *dvals;
    int dlen;
    int dcap;
    struct Val *tu;
    int tlen;
    void *cls;
    void *obj;
} Val;

typedef struct ClassDef ClassDef;
typedef struct ObjInst ObjInst;
typedef struct Env Env;
typedef struct Node Node;

struct ClassDef {
    char name[128];
    char **prop_names;
    Val *prop_defaults;
    int nprop;
    int prop_cap;
    char **method_names;
    Node **method_bodies;
    char ***method_params;
    int *method_nparams;
    int nmethod;
    int method_cap;
};

struct ObjInst {
    ClassDef *cls;
    Val *props;
    int nprops;
};

struct Env {
    char **names;
    Val *vals;
    int count;
    int cap;
    Env *parent;
};

typedef enum {
    T_EOF, T_NUM_INT, T_NUM_FLT, T_STR, T_IDENT,
    T_HOLD, T_SET, T_YELL, T_YELLN, T_CHECK, T_OR, T_END,
    T_SPIN, T_FROM, T_TO, T_TIMES, T_WHILE, T_CRAFT, T_GIVE,
    T_FORM, T_INP, T_GRAB, T_INTO, T_PUSH, T_POP, T_HALT,
    T_TRUE, T_FALSE, T_NONE, T_AND, T_NOT,
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT, T_CARET,
    T_EQ, T_NEQ, T_LT, T_GT, T_LE, T_GE,
    T_ASSIGN, T_EQEQ, T_LPAREN, T_RPAREN,
    T_LBRACKET, T_RBRACKET, T_LBRACE, T_RBRACE,
    T_COMMA, T_DOT, T_COLON, T_SEMI,
    T_SHIELD, T_WHEN, T_RAISE, T_VAULT, T_PIPE,
    T_NEWLINE, T_UNKNOWN
} TType;

typedef struct {
    TType type;
    char text[MAX_TOKEN];
    int line;
    int col;
    double fval;
    long long ival;
} Token;

typedef enum {
    N_NONE, N_INT_LIT, N_FLT_LIT, N_STR_LIT, N_BOOL_LIT, N_NONE_LIT,
    N_IDENT, N_BINOP, N_UNOP, N_ASSIGN, N_INDEX, N_DOT,
    N_CALL, N_LIST_LIT, N_DICT_LIT, N_TUPLE_LIT,
    N_HOLD, N_SET, N_YELL, N_YELLN, N_CHECK, N_OR, N_SPIN,
    N_SPIN_RANGE, N_SPIN_WHILE, N_CRAFT, N_GIVE, N_FORM,
    N_INP, N_GRAB, N_PUSH, N_POP, N_HALT,
    N_BLOCK, N_IF_ELSE, N_AND, N_NOT, N_SHIELD, N_RAISE,
    N_THIS, N_CAST_INT, N_CAST_FLT, N_CAST_STR,
    N_INPLACE_ADD, N_INPLACE_SUB, N_INPLACE_MUL, N_INPLACE_DIV
} NType;

struct Node {
    NType type;
    char name[256];
    Val lit_val;
    int op;
    Node *left;
    Node *right;
    Node *cond;
    Node *body;
    Node *elif_body;
    Node *next;
    char **params;
    int nparams;
    Node **items;
    int nitems;
    char **str_items;
};

typedef enum {
    MANI_PAUSE = 0x00,
    MANI_LOAD_CONST = 0x01,
    MANI_PULL = 0x02,
    MANI_STASH = 0x03,
    MANI_FUSION = 0x04,
    MANI_DRIFT = 0x05,
    MANI_SCALE = 0x06,
    MANI_SPLIT = 0x07,
    MANI_REMAINDER = 0x08,
    MANI_BOOST = 0x09,
    MANI_FLIP = 0x0A,
    MANI_INVERT = 0x0B,
    MANI_WEIGH = 0x0C,
    MANI_SKIP_IF_LIE = 0x0D,
    MANI_SKIP_IF_TRUTH = 0x0E,
    MANI_LEAP = 0x0F,
    MANI_TELEPORT = 0x10,
    MANI_INVOKE = 0x11,
    MANI_YIELD_BACK = 0x12,
    MANI_GRAB_LOCAL = 0x13,
    MANI_DROP_LOCAL = 0x14,
    MANI_GRAB_GLOBAL = 0x15,
    MANI_DROP_GLOBAL = 0x16,
    MANI_ASSEMBLE_LIST = 0x17,
    MANI_ASSEMBLE_MAP = 0x18,
    MANI_ASSEMBLE_PAIR = 0x19,
    MANI_EXTRACT = 0x1A,
    MANI_INJECT = 0x1B,
    MANI_TRAVERSE = 0x1C,
    MANI_STEP_FWD = 0x1D,
    MANI_CLONE = 0x1E,
    MANI_SWAP = 0x1F,
    MANI_DISCARD = 0x20,
    MANI_BRING = 0x21,
    MANI_PEEK_FIELD = 0x22,
    MANI_EDIT_FIELD = 0x23,
    MANI_YELL = 0x24,
    MANI_YELLN = 0x25,
    MANI_GRAB_INPUT = 0x26,
    MANI_HALT_OP = 0x27,
    MANI_CAST_I = 0x28,
    MANI_CAST_F = 0x29,
    MANI_CAST_S = 0x2A,
    MANI_FUSE_STR = 0x2B,
    MANI_NEWOBJ = 0x2C,
    MANI_SHIELD_START = 0x2D,
    MANI_SHIELD_CATCH = 0x2E,
    MANI_SHIELD_END = 0x2F,
    MANI_RAISE_OP = 0x30,
    MANI_CONCAT = 0x31,
    MANI_LENGTH = 0x32,
    MANI_TYPEOF = 0x33,
    MANI_HAS = 0x34,
    MANI_KEYS = 0x35,
    MANI_VALS = 0x36,
    MANI_SLICE = 0x37,
    MANI_PUSH_OP = 0x38,
    MANI_POP_OP = 0x39
} MnosOpCode;

typedef enum {
    MNOS_TAG_NONE = 0xA100,
    MNOS_TAG_BOOL = 0xA101,
    MNOS_TAG_INT = 0xA102,
    MNOS_TAG_FLOAT = 0xA103,
    MNOS_TAG_TEXT = 0xA104,
    MNOS_TAG_LIST = 0xA105,
    MNOS_TAG_MAP = 0xA106,
    MNOS_TAG_TUPLE = 0xA107,
    MNOS_TAG_CODE = 0xA108,
    MNOS_TAG_REF = 0xA109,
    MNOS_TAG_BYTES = 0xA10A,
    MNOS_TAG_EOF = 0xA1FF
} MnosTag;

typedef struct {
    Val consts[MAX_CONST_POOL];
    int nconsts;
    char *names[MAX_NAME_POOL];
    int nnames;
    unsigned char code[MAX_CODE_SIZE];
    int code_size;
    int code_cap;
} MnosCodeObj;

typedef struct {
    MnosCodeObj *code_obj;
    Val consts[MAX_CONST_POOL];
    int nconsts;
    char *names[MAX_NAME_POOL];
    int nnames;
    unsigned char code[MAX_CODE_SIZE];
    int ip;
    Val stack[MAX_STACK];
    int sp;
    uint32_t flags;
} MnosVM;

Val val_none(void);
Val val_int(long long i);
Val val_flt(double f);
Val val_str(const char *s);
Val val_bool(int b);
Val val_list(void);
Val val_dict(void);
Val val_tuple(Val *items, int n);
Val val_bytes(const char *data, int len);

int val_truthy(Val v);
int val_eq(Val a, Val b);
void val_free(Val *v);
Val val_copy(Val v);
char *val_to_str(Val v);
char *val_type_name(Val v);
Val val_add(Val a, Val b);
Val val_sub(Val a, Val b);
Val val_mul(Val a, Val b);
Val val_div(Val a, Val b);
Val val_mod(Val a, Val b);
Val val_pow(Val a, Val b);
Val val_neg(Val a);
Val val_not(Val a);
Val val_cmp(Val a, Val b, int op);

void env_init(Env *e, Env *parent);
void env_set(Env *e, const char *name, Val v);
Val env_get(Env *e, const char *name);
Val *env_get_ref(Env *e, const char *name);
int env_has(Env *e, const char *name);
void env_free(Env *e);

Token *lex(const char *src, int *ntokens);
Node *parse(Token *tokens, int ntokens);
void node_free(Node *n);
Val eval_node(Node *n, Env *env);
void register_builtins(Env *global);
Val call_function(const char *name, Val *args, int nargs, Env *global);
ClassDef *register_class_def(Node *nd, Env *env);
Val create_instance(ClassDef *cls, Env *env);
Val call_method(ObjInst *obj, const char *method, Val *args, int nargs, Env *global);

int mnos_compile(Node *ast, MnosCodeObj *out);
Val mnos_vm_run(MnosCodeObj *code, Env *global);
int mnos_save_cache(const char *path, MnosCodeObj *code, const char *src_hash);
int mnos_load_cache(const char *path, MnosCodeObj *code, const char *src_hash);
int mnos_serialize(Val v, FILE *f);
Val mnos_deserialize(FILE *f);
Val mnos_pack(Val v);
Val mnos_unpack(const char *data, int len);

int mnos_tool_main(int argc, char **argv);
int mnos_run_file(const char *path);
int mnos_run_source(const char *src, const char *filename);

#endif