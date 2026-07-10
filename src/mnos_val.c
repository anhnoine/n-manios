#include "mnos.h"

Val val_none(void) { Val v; memset(&v,0,sizeof v); v.type=V_NONE; return v; }
Val val_int(long long i) { Val v; memset(&v,0,sizeof v); v.type=V_INT; v.ival=i; return v; }
Val val_flt(double f) { Val v; memset(&v,0,sizeof v); v.type=V_FLOAT; v.fval=f; return v; }
Val val_str(const char *s) { Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=strdup(s); v.slen=strlen(s); return v; }
Val val_bool(int b) { Val v; memset(&v,0,sizeof v); v.type=V_BOOL; v.bval=b; return v; }
Val val_list(void) { Val v; memset(&v,0,sizeof v); v.type=V_LIST; v.lcap=8; v.li=malloc(sizeof(Val)*8); v.llen=0; return v; }
Val val_dict(void) { Val v; memset(&v,0,sizeof v); v.type=V_DICT; v.dcap=8; v.dkeys=malloc(sizeof(char*)*8); v.dvals=malloc(sizeof(Val)*8); v.dlen=0; return v; }
Val val_tuple(Val *items, int n) { Val v; memset(&v,0,sizeof v); v.type=V_TUPLE; v.tlen=n; v.tu=malloc(sizeof(Val)*n); for(int i=0;i<n;i++) v.tu[i]=val_copy(items[i]); return v; }
Val val_bytes(const char *data, int len) { Val v; memset(&v,0,sizeof v); v.type=V_BYTES; v.sval=malloc(len); memcpy(v.sval,data,len); v.slen=len; return v; }

int val_truthy(Val v) {
    switch(v.type) { case V_NONE: return 0; case V_INT: return v.ival!=0; case V_FLOAT: return v.fval!=0; case V_BOOL: return v.bval; case V_STR: return v.sval&&v.sval[0]; case V_LIST: return v.llen>0; case V_DICT: return v.dlen>0; case V_TUPLE: return v.tlen>0; case V_CLASS: return 1; case V_OBJ: return 1; default: return 0; }
}

int val_eq(Val a, Val b) {
    if (a.type!=b.type) return 0;
    switch(a.type) {
        case V_NONE: return 1;
        case V_INT: return a.ival==b.ival;
        case V_FLOAT: return a.fval==b.fval;
        case V_BOOL: return a.bval==b.bval;
        case V_STR: return strcmp(a.sval,b.sval)==0;
        case V_LIST: if(a.llen!=b.llen) return 0; for(int i=0;i<a.llen;i++) if(!val_eq(a.li[i],b.li[i])) return 0; return 1;
        case V_DICT: if(a.dlen!=b.dlen) return 0; for(int i=0;i<a.dlen;i++) { int found=0; for(int j=0;j<b.dlen;j++) if(strcmp(a.dkeys[i],b.dkeys[j])==0&&val_eq(a.dvals[i],b.dvals[j])){found=1;break;} if(!found) return 0; } return 1;
        default: return 0;
    }
}

void val_free(Val *v) {
    if(!v) return;
    if(v->type==V_STR||v->type==V_BYTES) { free(v->sval); v->sval=NULL; }
    else if(v->type==V_LIST) { for(int i=0;i<v->llen;i++) val_free(&v->li[i]); free(v->li); v->li=NULL; }
    else if(v->type==V_DICT) { for(int i=0;i<v->dlen;i++) { free(v->dkeys[i]); val_free(&v->dvals[i]); } free(v->dkeys); free(v->dvals); v->dkeys=NULL; v->dvals=NULL; }
    else if(v->type==V_TUPLE) { for(int i=0;i<v->tlen;i++) val_free(&v->tu[i]); free(v->tu); v->tu=NULL; }
    v->type=V_NONE;
}

Val val_copy(Val v) {
    Val r; memset(&r,0,sizeof r);
    r.type=v.type; r.ival=v.ival; r.fval=v.fval; r.bval=v.bval;
    if(v.type==V_STR||v.type==V_BYTES) { r.sval=strdup(v.sval); r.slen=v.slen; }
    else if(v.type==V_LIST) { r.lcap=v.llen>0?v.llen:8; r.li=malloc(sizeof(Val)*r.lcap); r.llen=v.llen; for(int i=0;i<v.llen;i++) r.li[i]=val_copy(v.li[i]); }
    else if(v.type==V_DICT) { r.dcap=v.dlen>0?v.dlen:8; r.dkeys=malloc(sizeof(char*)*r.dcap); r.dvals=malloc(sizeof(Val)*r.dcap); r.dlen=v.dlen; for(int i=0;i<v.dlen;i++){r.dkeys[i]=strdup(v.dkeys[i]);r.dvals[i]=val_copy(v.dvals[i]);} }
    else if(v.type==V_TUPLE) { r.tlen=v.tlen; r.tu=malloc(sizeof(Val)*v.tlen); for(int i=0;i<v.tlen;i++) r.tu[i]=val_copy(v.tu[i]); }
    r.cls=v.cls; r.obj=v.obj;
    return r;
}

char *val_to_str(Val v) {
    char buf[65536]; buf[0]=0;
    switch(v.type) {
        case V_NONE: strcpy(buf,"none"); break;
        case V_INT: snprintf(buf,sizeof buf,"%lld",v.ival); break;
        case V_FLOAT: { char tmp[64]; snprintf(tmp,sizeof tmp,"%g",v.fval); if(!strchr(tmp,'.')&&!strchr(tmp,'e')&&!strchr(tmp,'E')) strcat(tmp,".0"); strcpy(buf,tmp); break; }
        case V_BOOL: strcpy(buf,v.bval?"true":"false"); break;
        case V_STR: snprintf(buf,sizeof buf,"%s",v.sval); break;
        case V_BYTES: snprintf(buf,sizeof buf,"<bytes len=%d>",v.slen); break;
        case V_LIST: {
            int pos=0; buf[pos++]='[';
            for(int i=0;i<v.llen;i++) {
                if(i>0){buf[pos++]=',';buf[pos++]=' ';}
                char *s=val_to_str(v.li[i]); int sl=strlen(s);
                if(pos+sl<65000){memcpy(buf+pos,s,sl);pos+=sl;} free(s);
            }
            buf[pos++]=']'; buf[pos]=0; break;
        }
        case V_DICT: {
            int pos=0; buf[pos++]='{';
            for(int i=0;i<v.dlen;i++){
                if(i>0){buf[pos++]=',';buf[pos++]=' ';}
                buf[pos++]='"'; int kl=strlen(v.dkeys[i]); if(pos+kl<65000){memcpy(buf+pos,v.dkeys[i],kl);pos+=kl;} buf[pos++]='"'; buf[pos++]=':'; buf[pos++]=' ';
                char *s=val_to_str(v.dvals[i]); int sl=strlen(s); if(pos+sl<65000){memcpy(buf+pos,s,sl);pos+=sl;} free(s);
            }
            buf[pos++]='}'; buf[pos]=0; break;
        }
        case V_TUPLE: {
            int pos=0; buf[pos++]='(';
            for(int i=0;i<v.tlen;i++){
                if(i>0){buf[pos++]=',';buf[pos++]=' ';}
                char *s=val_to_str(v.tu[i]); int sl=strlen(s); if(pos+sl<65000){memcpy(buf+pos,s,sl);pos+=sl;} free(s);
            }
            if(v.tlen==1){buf[pos++]=',';}
            buf[pos++]=')'; buf[pos]=0; break;
        }
        case V_CLASS: { ClassDef *cd=(ClassDef*)v.cls; snprintf(buf,sizeof buf,"<class %s>",cd->name); break; }
        case V_OBJ: { ObjInst *o=(ObjInst*)v.obj; snprintf(buf,sizeof buf,"<%s instance>",o->cls->name); break; }
    }
    return strdup(buf);
}

char *val_type_name(Val v) {
    switch(v.type){
        case V_NONE: return strdup("none"); case V_INT: return strdup("int"); case V_FLOAT: return strdup("float");
        case V_STR: return strdup("str"); case V_BOOL: return strdup("bool"); case V_LIST: return strdup("list");
        case V_DICT: return strdup("dict"); case V_TUPLE: return strdup("tuple"); case V_BYTES: return strdup("bytes");
        case V_CLASS: return strdup("class"); case V_OBJ: return strdup("object");
        default: return strdup("unknown");
    }
}

Val val_cmp(Val a, Val b, int op) {
    int r=0;
    if(a.type==V_INT&&b.type==V_INT) { switch(op){ case T_LT: r=a.ival<b.ival; break; case T_GT: r=a.ival>b.ival; break; case T_LE: r=a.ival<=b.ival; break; case T_GE: r=a.ival>=b.ival; break; case T_EQEQ: r=a.ival==b.ival; break; case T_NEQ: r=a.ival!=b.ival; break; } }
    else if(a.type==V_FLOAT&&b.type==V_FLOAT) { switch(op){ case T_LT: r=a.fval<b.fval; break; case T_GT: r=a.fval>b.fval; break; case T_LE: r=a.fval<=b.fval; break; case T_GE: r=a.fval>=b.fval; break; case T_EQEQ: r=a.fval==b.fval; break; case T_NEQ: r=a.fval!=b.fval; break; } }
    else if(a.type==V_INT&&b.type==V_FLOAT) { double da=(double)a.ival; switch(op){ case T_LT: r=da<b.fval; break; case T_GT: r=da>b.fval; break; case T_LE: r=da<=b.fval; break; case T_GE: r=da>=b.fval; break; case T_EQEQ: r=da==b.fval; break; case T_NEQ: r=da!=b.fval; break; } }
    else if(a.type==V_FLOAT&&b.type==V_INT) { double db=(double)b.ival; switch(op){ case T_LT: r=a.fval<db; break; case T_GT: r=a.fval>db; break; case T_LE: r=a.fval<=db; break; case T_GE: r=a.fval>=db; break; case T_EQEQ: r=a.fval==db; break; case T_NEQ: r=a.fval!=db; break; } }
    else if(a.type==V_STR&&b.type==V_STR) { int c=strcmp(a.sval,b.sval); switch(op){ case T_LT: r=c<0; break; case T_GT: r=c>0; break; case T_LE: r=c<=0; break; case T_GE: r=c>=0; break; case T_EQEQ: r=c==0; break; case T_NEQ: r=c!=0; break; } }
    else if(op==T_EQEQ) { r=val_eq(a,b); }
    else if(op==T_NEQ) { r=!val_eq(a,b); }
    return val_bool(r);
}

Val val_add(Val a, Val b) {
    if(a.type==V_STR||b.type==V_STR) { char *sa=val_to_str(a); char *sb=val_to_str(b); char buf[65536]; snprintf(buf,sizeof buf,"%s%s",sa,sb); free(sa); free(sb); return val_str(buf); }
    if(a.type==V_LIST&&b.type==V_LIST) { Val r=val_list(); for(int i=0;i<a.llen;i++) r.li[r.llen++]=val_copy(a.li[i]); for(int i=0;i<b.llen;i++) r.li[r.llen++]=val_copy(b.li[i]); return r; }
    if(a.type==V_INT&&b.type==V_INT) return val_int(a.ival+b.ival);
    if((a.type==V_INT||a.type==V_FLOAT)&&(b.type==V_INT||b.type==V_FLOAT)) { double da=a.type==V_INT?(double)a.ival:a.fval; double db=b.type==V_INT?(double)b.ival:b.fval; return val_flt(da+db); }
    return val_none();
}
Val val_sub(Val a, Val b) { if(a.type==V_INT&&b.type==V_INT) return val_int(a.ival-b.ival); if((a.type==V_INT||a.type==V_FLOAT)&&(b.type==V_INT||b.type==V_FLOAT)){double da=a.type==V_INT?(double)a.ival:a.fval;double db=b.type==V_INT?(double)b.ival:b.fval;return val_flt(da-db);} return val_none(); }
Val val_mul(Val a, Val b) { if(a.type==V_STR&&b.type==V_INT) { char *s=val_to_str(a); int slen=strlen(s); long long total=slen*b.ival; if(total>1000000) total=1000000; char *r=malloc(total+1); int pos=0; for(long long i=0;i<b.ival&&pos<total;i++){memcpy(r+pos,s,slen);pos+=slen;} r[pos]=0; free(s); Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=pos; return v; } if(a.type==V_INT&&b.type==V_INT) return val_int(a.ival*b.ival); if((a.type==V_INT||a.type==V_FLOAT)&&(b.type==V_INT||b.type==V_FLOAT)){double da=a.type==V_INT?(double)a.ival:a.fval;double db=b.type==V_INT?(double)b.ival:b.fval;return val_flt(da*db);} return val_none(); }
Val val_div(Val a, Val b) { if((a.type==V_INT||a.type==V_FLOAT)&&(b.type==V_INT||b.type==V_FLOAT)){double da=a.type==V_INT?(double)a.ival:a.fval;double db=b.type==V_INT?(double)b.ival:b.fval;if(db==0){fprintf(stderr,"Loi: chia cho 0\n");exit(1);}return val_flt(da/db);} return val_none(); }
Val val_mod(Val a, Val b) { if(a.type==V_INT&&b.type==V_INT) { if(b.ival==0){fprintf(stderr,"Loi: modulo 0\n");exit(1);} return val_int(a.ival%b.ival); } return val_none(); }
Val val_pow(Val a, Val b) { if(a.type==V_INT&&b.type==V_INT) { long long r=1; for(long long i=0;i<b.ival;i++) r*=a.ival; return val_int(r); } if((a.type==V_INT||a.type==V_FLOAT)&&(b.type==V_INT||b.type==V_FLOAT)){double da=a.type==V_INT?(double)a.ival:a.fval;double db=b.type==V_INT?(double)b.ival:b.fval;return val_flt(pow(da,db));} return val_none(); }
Val val_neg(Val a) { if(a.type==V_INT) return val_int(-a.ival); if(a.type==V_FLOAT) return val_flt(-a.fval); return val_none(); }
Val val_not(Val a) { return val_bool(!val_truthy(a)); }