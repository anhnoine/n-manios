#include "mnos.h"

static uint32_t simple_hash(const char *data, int len) {
    uint32_t h=5381;
    for(int i=0;i<len;i++) h=((h<<5)+h)+(unsigned char)data[i];
    return h;
}

static void print_mnos_tags(void) {
    printf("MNOS Serialization Tags (v1.0.0):\n");
    printf("  0xA100 = MNOS_TAG_NONE\n");
    printf("  0xA101 = MNOS_TAG_BOOL\n");
    printf("  0xA102 = MNOS_TAG_INT\n");
    printf("  0xA103 = MNOS_TAG_FLOAT\n");
    printf("  0xA104 = MNOS_TAG_TEXT\n");
    printf("  0xA105 = MNOS_TAG_LIST\n");
    printf("  0xA106 = MNOS_TAG_MAP\n");
    printf("  0xA107 = MNOS_TAG_TUPLE\n");
    printf("  0xA108 = MNOS_TAG_CODE\n");
    printf("  0xA109 = MNOS_TAG_REF\n");
    printf("  0xA10A = MNOS_TAG_BYTES\n");
    printf("  0xA1FF = MNOS_TAG_EOF\n");
}

static void print_mnos_opcodes(void) {
    printf("MNOS Bytecode Opcodes:\n");
    printf("  0x00 MANI_PAUSE        0x01 MANI_LOAD_CONST  0x02 MANI_PULL\n");
    printf("  0x03 MANI_STASH        0x04 MANI_FUSION      0x05 MANI_DRIFT\n");
    printf("  0x06 MANI_SCALE        0x07 MANI_SPLIT       0x08 MANI_REMAINDER\n");
    printf("  0x09 MANI_BOOST        0x0A MANI_FLIP        0x0B MANI_INVERT\n");
    printf("  0x0C MANI_WEIGH        0x0D MANI_SKIP_IF_LIE 0x0E MANI_SKIP_IF_TRUTH\n");
    printf("  0x0F MANI_LEAP         0x10 MANI_TELEPORT    0x11 MANI_INVOKE\n");
    printf("  0x12 MANI_YIELD_BACK   0x13 MANI_GRAB_LOCAL  0x14 MANI_DROP_LOCAL\n");
    printf("  0x15 MANI_GRAB_GLOBAL  0x16 MANI_DROP_GLOBAL 0x17 MANI_ASSEMBLE_LIST\n");
    printf("  0x18 MANI_ASSEMBLE_MAP 0x19 MANI_ASSEMBLE_PAIR 0x1A MANI_EXTRACT\n");
    printf("  0x1B MANI_INJECT       0x1C MANI_TRAVERSE    0x1D MANI_STEP_FWD\n");
    printf("  0x1E MANI_CLONE        0x1F MANI_SWAP        0x20 MANI_DISCARD\n");
    printf("  0x21 MANI_BRING        0x22 MANI_PEEK_FIELD  0x23 MANI_EDIT_FIELD\n");
    printf("  0x24 MANI_YELL         0x25 MANI_YELLN       0x26 MANI_GRAB_INPUT\n");
    printf("  0x27 MANI_HALT_OP      0x28 MANI_CAST_I      0x29 MANI_CAST_F\n");
    printf("  0x2A MANI_CAST_S       0x2B MANI_FUSE_STR    0x2C MANI_NEWOBJ\n");
    printf("  0x2D MANI_SHIELD_START 0x2E MANI_SHIELD_CATCH 0x2F MANI_SHIELD_END\n");
    printf("  0x30 MANI_RAISE_OP     0x31 MANI_CONCAT      0x32 MANI_LENGTH\n");
    printf("  0x33 MANI_TYPEOF       0x34 MANI_HAS         0x35 MANI_KEYS\n");
    printf("  0x36 MANI_VALS         0x37 MANI_SLICE       0x38 MANI_PUSH_OP\n");
    printf("  0x39 MANI_POP_OP\n");
}

static int tool_compile(const char *path) {
    FILE *f=fopen(path,"r");
    if(!f){fprintf(stderr,"Khong mo duoc: %s\n",path);return 1;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *src=malloc(sz+1); fread(src,1,sz,f); src[sz]=0; fclose(f);
    int nt; Token *tks=lex(src,&nt);
    Node *ast=parse(tks,nt);
    MnosCodeObj code;
    int cs=mnos_compile(ast,&code);
    char hashbuf[64];
    snprintf(hashbuf,sizeof hashbuf,"%u",simple_hash(src,(int)sz));
    char cachepath[4096];
    snprintf(cachepath,sizeof cachepath,"%s.mnocache",path);
    int r=mnos_save_cache(path,&code,hashbuf);
    if(r==0) printf("Da compile: %s -> %d bytes bytecode\n",cachepath,cs);
    else printf("Loi khi compile\n");
    node_free(ast); free(tks); free(src);
    return r;
}

static int tool_inspect(const char *path) {
    MnosCodeObj code;
    int r=mnos_load_cache(path,&code,NULL);
    if(r!=0){fprintf(stderr,"Khong doc duoc mnocache: %s\n",path);return 1;}
    printf("MNOS Cache: %s\n",path);
    printf("Constants: %d\n",code.nconsts);
    for(int i=0;i<code.nconsts;i++){
        char *s=val_to_str(code.consts[i]);
        printf("  [%d] %s (%s)\n",i,s,val_type_name(code.consts[i]));
        free(s);
    }
    printf("Names: %d\n",code.nnames);
    for(int i=0;i<code.nnames;i++) printf("  [%d] %s\n",i,code.names[i]);
    printf("Bytecode: %d bytes\n",code.code_size);
    for(int i=0;i<code.code_size;i++){
        printf("  %04d: 0x%02X",i,code.code[i]);
        if(code.code[i]==MANI_LOAD_CONST&&i+2<code.code_size){
            int idx=(code.code[i+1]<<8)|code.code[i+2];
            if(idx<code.nconsts){char *s=val_to_str(code.consts[idx]);printf("  LOAD_CONST [%d]=%s",idx,s);free(s);}
        } else if(code.code[i]==MANI_PULL&&i+2<code.code_size){
            int idx=(code.code[i+1]<<8)|code.code[i+2];
            if(idx<code.nnames) printf("  PULL \"%s\"",code.names[idx]);
        } else if(code.code[i]==MANI_STASH&&i+2<code.code_size){
            int idx=(code.code[i+1]<<8)|code.code[i+2];
            if(idx<code.nnames) printf("  STASH \"%s\"",code.names[idx]);
        } else if(code.code[i]==MANI_INVOKE&&i+2<code.code_size){
            int nargs=(code.code[i+1]<<8)|code.code[i+2];
            printf("  INVOKE (%d args)",nargs);
        } else if(code.code[i]==MANI_FUSION) printf("  FUSION (+)");
        else if(code.code[i]==MANI_DRIFT) printf("  DRIFT (-)");
        else if(code.code[i]==MANI_SCALE) printf("  SCALE (*)");
        else if(code.code[i]==MANI_SPLIT) printf("  SPLIT (/)");
        else if(code.code[i]==MANI_YELL) printf("  YELL");
        else if(code.code[i]==MANI_YIELD_BACK) printf("  YIELD_BACK");
        else if(code.code[i]==MANI_HALT_OP) printf("  HALT");
        printf("\n");
    }
    return 0;
}

static int tool_serialize(const char *path) {
    char outpath[4096];
    snprintf(outpath,sizeof outpath,"%s.mnos",path);
    FILE *f=fopen(path,"r");
    if(!f){fprintf(stderr,"Khong mo duoc: %s\n",path);return 1;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *src=malloc(sz+1); fread(src,1,sz,f); src[sz]=0; fclose(f);
    int nt; Token *tks=lex(src,&nt);
    Node *ast=parse(tks,nt);
    MnosCodeObj code;
    mnos_compile(ast,&code);
    mnos_save_cache(path,&code,NULL);
    printf("Da tao: %s\n",outpath);
    node_free(ast); free(tks); free(src);
    return 0;
}

int mnos_tool_main(int argc, char **argv) {
    if(argc<1){printf("MNOS Tool - Manios bytecode/serialization tool\n");printf("Cach dung: manios tool <lenh> [args]\n");printf("Lenh: tags, opcodes, compile <file>, inspect <file>, serialize <file>\n");return 0;}
    if(strcmp(argv[0],"tags")==0){print_mnos_tags();return 0;}
    if(strcmp(argv[0],"opcodes")==0){print_mnos_opcodes();return 0;}
    if(strcmp(argv[0],"compile")==0&&argc>1) return tool_compile(argv[1]);
    if(strcmp(argv[0],"inspect")==0&&argc>1) return tool_inspect(argv[1]);
    if(strcmp(argv[0],"serialize")==0&&argc>1) return tool_serialize(argv[1]);
    printf("MNOS Tool\n");
    printf("  manios tool tags          - Xem MNOS serialization tags\n");
    printf("  manios tool opcodes       - Xem MNOS bytecode opcodes\n");
    printf("  manios tool compile <f>   - Compile .mno -> .mnocache\n");
    printf("  manios tool inspect <f>   - Xem noi dung .mnocache\n");
    printf("  manios tool serialize <f> - Serialize file\n");
    return 0;
}