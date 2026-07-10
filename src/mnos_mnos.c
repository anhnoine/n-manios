#include "mnos.h"

static void emit_byte(MnosCodeObj *co, unsigned char b) {
    if(co->code_size>=MAX_CODE_SIZE) return;
    co->code[co->code_size++]=b;
}

static void emit_u16(MnosCodeObj *co, uint16_t v) {
    emit_byte(co,(v>>8)&0xFF);
    emit_byte(co,v&0xFF);
}

static void emit_u32(MnosCodeObj *co, uint32_t v) {
    emit_byte(co,(v>>24)&0xFF);
    emit_byte(co,(v>>16)&0xFF);
    emit_byte(co,(v>>8)&0xFF);
    emit_byte(co,v&0xFF);
}

static int add_const(MnosCodeObj *co, Val v) {
    for(int i=0;i<co->nconsts;i++) if(val_eq(co->consts[i],v)) return i;
    if(co->nconsts>=MAX_CONST_POOL) return 0;
    co->consts[co->nconsts]=val_copy(v);
    return co->nconsts++;
}

static int add_name(MnosCodeObj *co, const char *name) {
    for(int i=0;i<co->nnames;i++) if(strcmp(co->names[i],name)==0) return i;
    if(co->nnames>=MAX_NAME_POOL) return 0;
    co->names[co->nnames++]=strdup(name);
    return co->nnames-1;
}

static void compile_node(Node *nd, MnosCodeObj *co) {
    if(!nd) return;
    switch(nd->type) {
    case N_INT_LIT: { emit_byte(co,MANI_LOAD_CONST); int ci=add_const(co,nd->lit_val); emit_u16(co,(uint16_t)ci); break; }
    case N_FLT_LIT: { emit_byte(co,MANI_LOAD_CONST); int ci=add_const(co,nd->lit_val); emit_u16(co,(uint16_t)ci); break; }
    case N_STR_LIT: { emit_byte(co,MANI_LOAD_CONST); int ci=add_const(co,nd->lit_val); emit_u16(co,(uint16_t)ci); break; }
    case N_BOOL_LIT: { emit_byte(co,MANI_LOAD_CONST); int ci=add_const(co,nd->lit_val); emit_u16(co,(uint16_t)ci); break; }
    case N_NONE_LIT: { emit_byte(co,MANI_LOAD_CONST); Val nv=val_none(); int ci=add_const(co,nv); emit_u16(co,(uint16_t)ci); break; }
    case N_IDENT: { int ni=add_name(co,nd->name); emit_byte(co,MANI_PULL); emit_u16(co,(uint16_t)ni); break; }
    case N_BINOP: {
        compile_node(nd->left,co); compile_node(nd->right,co);
        switch(nd->op) {
            case T_PLUS: emit_byte(co,MANI_FUSION); break;
            case T_MINUS: emit_byte(co,MANI_DRIFT); break;
            case T_STAR: emit_byte(co,MANI_SCALE); break;
            case T_SLASH: emit_byte(co,MANI_SPLIT); break;
            case T_PERCENT: emit_byte(co,MANI_REMAINDER); break;
            case T_CARET: emit_byte(co,MANI_BOOST); break;
            default: { emit_byte(co,MANI_WEIGH); emit_byte(co,(unsigned char)nd->op); break; }
        }
        break;
    }
    case N_UNOP: { compile_node(nd->left,co); emit_byte(co,nd->op==T_MINUS?MANI_FLIP:MANI_INVERT); break; }
    case N_NOT: { compile_node(nd->left,co); emit_byte(co,MANI_INVERT); break; }
    case N_HOLD: { compile_node(nd->left,co); int ni=add_name(co,nd->name); emit_byte(co,MANI_STASH); emit_u16(co,(uint16_t)ni); break; }
    case N_SET: case N_ASSIGN: { compile_node(nd->left,co); int ni=add_name(co,nd->name); emit_byte(co,MANI_STASH); emit_u16(co,(uint16_t)ni); break; }
    case N_YELL: { compile_node(nd->left,co); emit_byte(co,MANI_YELL); break; }
    case N_YELLN: { compile_node(nd->left,co); emit_byte(co,MANI_YELLN); break; }
    case N_CHECK: {
        compile_node(nd->cond,co);
        emit_byte(co,MANI_SKIP_IF_LIE);
        int patch_pos=co->code_size;
        emit_u16(co,0);
        for(Node *s=nd->body;s;s=s->next) compile_node(s,co);
        if(nd->elif_body) {
            emit_byte(co,MANI_LEAP);
            int end_patch=co->code_size;
            emit_u16(co,0);
            co->code[patch_pos]=(co->code_size-patch_pos-2)>>8&0xFF;
            co->code[patch_pos+1]=(co->code_size-patch_pos-2)&0xFF;
            compile_node(nd->elif_body,co);
            co->code[end_patch]=(co->code_size-end_patch-2)>>8&0xFF;
            co->code[end_patch+1]=(co->code_size-end_patch-2)&0xFF;
        } else {
            co->code[patch_pos]=(co->code_size-patch_pos-2)>>8&0xFF;
            co->code[patch_pos+1]=(co->code_size-patch_pos-2)&0xFF;
        }
        break;
    }
    case N_SPIN: {
        compile_node(nd->cond,co);
        emit_byte(co,MANI_LOAD_CONST);
        int ci=add_const(co,val_int(0));
        emit_u16(co,(uint16_t)ci);
        int loop_start=co->code_size;
        emit_byte(co,MANI_CLONE);
        emit_byte(co,MANI_WEIGH);
        emit_byte(co,T_GE);
        emit_byte(co,MANI_SKIP_IF_LIE);
        int patch=co->code_size;
        emit_u16(co,0);
        emit_byte(co,MANI_DISCARD);
        for(Node *s=nd->body;s;s=s->next) compile_node(s,co);
        emit_byte(co,MANI_LOAD_CONST);
        emit_u16(co,(uint16_t)ci);
        emit_byte(co,MANI_FUSION);
        emit_byte(co,MANI_TELEPORT);
        int bs_bytes[2]={loop_start>>8&0xFF,loop_start&0xFF};
        emit_byte(co,(unsigned char)bs_bytes[0]);
        emit_byte(co,(unsigned char)bs_bytes[1]);
        co->code[patch]=(co->code_size-patch-2)>>8&0xFF;
        co->code[patch+1]=(co->code_size-patch-2)&0xFF;
        emit_byte(co,MANI_DISCARD);
        break;
    }
    case N_CRAFT: { for(Node *s=nd->body;s;s=s->next) compile_node(s,co); break; }
    case N_GIVE: { compile_node(nd->left,co); emit_byte(co,MANI_YIELD_BACK); break; }
    case N_FORM: { for(Node *s=nd->body;s;s=s->next) compile_node(s,co); break; }
    case N_CALL: { compile_node(nd->left,co); for(int i=0;i<nd->nitems;i++) compile_node(nd->items[i],co); emit_byte(co,MANI_INVOKE); emit_u16(co,(uint16_t)nd->nitems); break; }
    case N_INDEX: { compile_node(nd->left,co); compile_node(nd->right,co); emit_byte(co,MANI_EXTRACT); break; }
    case N_LIST_LIT: { emit_byte(co,MANI_ASSEMBLE_LIST); emit_u16(co,(uint16_t)nd->nitems); for(int i=0;i<nd->nitems;i++) compile_node(nd->items[i],co); break; }
    case N_HALT: { emit_byte(co,MANI_HALT_OP); break; }
    default: { if(nd->left) compile_node(nd->left,co); if(nd->right) compile_node(nd->right,co); if(nd->cond) compile_node(nd->cond,co); if(nd->body) compile_node(nd->body,co); if(nd->elif_body) compile_node(nd->elif_body,co); break; }
    }
}

int mnos_compile(Node *ast, MnosCodeObj *out) {
    memset(out,0,sizeof(MnosCodeObj));
    for(Node *n=ast;n;n=n->next) compile_node(n,out);
    emit_byte(out,MANI_HALT_OP);
    return out->code_size;
}

static uint32_t simple_hash(const char *data, int len) {
    uint32_t h=5381;
    for(int i=0;i<len;i++) h=((h<<5)+h)+(unsigned char)data[i];
    return h;
}

int mnos_save_cache(const char *path, MnosCodeObj *code, const char *src_hash) {
    char cachepath[4096];
    int plen=(int)strlen(path);
    if(plen>4&&strcmp(path+plen-4,".mno")==0) plen-=4;
    snprintf(cachepath,sizeof cachepath,"%.*s%s",plen,path,MNOS_CACHE_EXT);
    FILE *f=fopen(cachepath,"wb");
    if(!f) return -1;
    uint32_t magic=MNOS_MAGIC;
    fwrite(&magic,4,1,f);
    uint16_t ver=1;
    fwrite(&ver,2,1,f);
    uint32_t flags=0;
    fwrite(&flags,4,1,f);
    uint32_t hash=0;
    if(src_hash) hash=simple_hash(src_hash,strlen(src_hash));
    fwrite(&hash,4,1,f);
    uint32_t nc=(uint32_t)code->nconsts;
    fwrite(&nc,4,1,f);
    uint32_t nn=(uint32_t)code->nnames;
    fwrite(&nn,4,1,f);
    uint32_t cs=(uint32_t)code->code_size;
    fwrite(&cs,4,1,f);
    for(uint32_t i=0;i<nc;i++) {
        Val v=code->consts[i];
        uint16_t tag;
        switch(v.type) {
            case V_NONE: tag=MNOS_TAG_NONE; fwrite(&tag,2,1,f); break;
            case V_BOOL: tag=MNOS_TAG_BOOL; fwrite(&tag,2,1,f); {uint8_t b=v.bval?1:0;fwrite(&b,1,1,f);} break;
            case V_INT: tag=MNOS_TAG_INT; fwrite(&tag,2,1,f); fwrite(&v.ival,8,1,f); break;
            case V_FLOAT: tag=MNOS_TAG_FLOAT; fwrite(&tag,2,1,f); fwrite(&v.fval,8,1,f); break;
            case V_STR: tag=MNOS_TAG_TEXT; fwrite(&tag,2,1,f); {uint32_t len=(uint32_t)v.slen;fwrite(&len,4,1,f);fwrite(v.sval,1,len,f);} break;
            default: tag=MNOS_TAG_NONE; fwrite(&tag,2,1,f); break;
        }
    }
    for(uint32_t i=0;i<nn;i++) {
        uint16_t len=(uint16_t)strlen(code->names[i]);
        fwrite(&len,2,1,f);
        fwrite(code->names[i],1,len,f);
    }
    if(code->code_size>0) fwrite(code->code,1,code->code_size,f);
    fclose(f);
    return 0;
}

int mnos_load_cache(const char *path, MnosCodeObj *code, const char *src_hash) {
    char cachepath[4096];
    int plen2=(int)strlen(path);
    if(plen2>4&&strcmp(path+plen2-4,".mno")==0) plen2-=4;
    snprintf(cachepath,sizeof cachepath,"%.*s%s",plen2,path,MNOS_CACHE_EXT);
    FILE *f=fopen(cachepath,"rb");
    if(!f) return -1;
    memset(code,0,sizeof(MnosCodeObj));
    uint32_t magic; fread(&magic,4,1,f);
    if(magic!=MNOS_MAGIC){fclose(f);return -1;}
    uint16_t ver; fread(&ver,2,1,f);
    if(ver!=1){fclose(f);return -1;}
    uint32_t flags; fread(&flags,4,1,f);
    uint32_t hash; fread(&hash,4,1,f);
    if(src_hash&&hash!=simple_hash(src_hash,strlen(src_hash))){fclose(f);return -1;}
    uint32_t nc; fread(&nc,4,1,f);
    uint32_t nn; fread(&nn,4,1,f);
    uint32_t cs; fread(&cs,4,1,f);
    for(uint32_t i=0;i<nc;i++){
        uint16_t tag; fread(&tag,2,1,f);
        switch(tag) {
            case MNOS_TAG_NONE: code->consts[code->nconsts++]=val_none(); break;
            case MNOS_TAG_BOOL: {uint8_t b;fread(&b,1,1,f);code->consts[code->nconsts++]=val_bool(b);break;}
            case MNOS_TAG_INT: {long long v;fread(&v,8,1,f);code->consts[code->nconsts++]=val_int(v);break;}
            case MNOS_TAG_FLOAT: {double v;fread(&v,8,1,f);code->consts[code->nconsts++]=val_flt(v);break;}
            case MNOS_TAG_TEXT: {uint32_t len;fread(&len,4,1,f);char *s=malloc(len+1);fread(s,1,len,f);s[len]=0;Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=s;v.slen=(int)len;code->consts[code->nconsts++]=v;break;}
            default: code->consts[code->nconsts++]=val_none(); break;
        }
    }
    for(uint32_t i=0;i<nn;i++){
        uint16_t len; fread(&len,2,1,f);
        char *name=malloc(len+1); fread(name,1,len,f); name[len]=0;
        code->names[code->nnames++]=name;
    }
    if(cs>0&&cs<=MAX_CODE_SIZE) {
        code->code_size=(int)fread(code->code,1,cs,f);
    }
    fclose(f);
    return 0;
}