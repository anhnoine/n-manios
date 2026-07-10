#include "mnos.h"

static char *fn_names[MAX_FNS];
static Node *fn_bodies[MAX_FNS];
static char **fn_params[MAX_FNS];
static int fn_nparams[MAX_FNS];
static int fn_count=0;
static ClassDef *cls_defs[MAX_CLS];
static int cls_count=0;
static int ret_flag=0;
static Val ret_val;
static char current_file_dir[4096]="";
static char mnos_error_msg[4096]="";
static int mnos_error_flag=0;
static Env *global_env=NULL;
static Val g_args_val;

typedef Val (*BuiltinFn)(Val*,int);
typedef struct { const char *name; BuiltinFn func; } BuiltinEntry;
static BuiltinEntry builtins[MAX_BUILTIN];
static int nbuiltins=0;

static void reg_builtin(const char *name, BuiltinFn fn) {
    if(nbuiltins>=MAX_BUILTIN) return;
    builtins[nbuiltins].name=strdup(name);
    builtins[nbuiltins].func=fn;
    nbuiltins++;
}
static BuiltinFn find_builtin(const char *name) {
    for(int i=0;i<nbuiltins;i++) if(strcmp(builtins[i].name,name)==0) return builtins[i].func;
    return NULL;
}

static Val bi_str(Val *a, int n) { char *s=val_to_str(a[0]); Val r=val_str(s); free(s); return r; }
static Val bi_int_fn(Val *a, int n) { if(a[0].type==V_INT) return val_int(a[0].ival); if(a[0].type==V_FLOAT) return val_int((long long)a[0].fval); if(a[0].type==V_STR) return val_int(atoll(a[0].sval)); if(a[0].type==V_BOOL) return val_int(a[0].bval?1:0); return val_int(0); }
static Val bi_float_fn(Val *a, int n) { if(a[0].type==V_FLOAT) return val_flt(a[0].fval); if(a[0].type==V_INT) return val_flt((double)a[0].ival); if(a[0].type==V_STR) return val_flt(atof(a[0].sval)); return val_flt(0.0); }
static Val bi_type_of(Val *a, int n) { return val_str(val_type_name(a[0])); }
static Val bi_len(Val *a, int n) { if(a[0].type==V_LIST) return val_int(a[0].llen); if(a[0].type==V_STR) return val_int(a[0].slen); if(a[0].type==V_DICT) return val_int(a[0].dlen); if(a[0].type==V_TUPLE) return val_int(a[0].tlen); return val_int(0); }
static Val bi_upper(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl+1); for(int i=0;i<sl;i++) r[i]=toupper((unsigned char)s[i]); r[sl]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=sl; return v; }
static Val bi_lower(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl+1); for(int i=0;i<sl;i++) r[i]=tolower((unsigned char)s[i]); r[sl]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=sl; return v; }
static Val bi_trim(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); int st=0,en=sl-1; while(st<=en&&(s[st]==' '||s[st]=='\t'||s[st]=='\n'||s[st]=='\r')) st++; while(en>=st&&(s[en]==' '||s[en]=='\t'||s[en]=='\n'||s[en]=='\r')) en--; int len=en-st+1; char *r=malloc(len+1); memcpy(r,s+st,len); r[len]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=len; return v; }
static Val bi_lstrip(Val *a, int n) { char *s=a[0].sval; int st=0; while(s[st]&&(s[st]==' '||s[st]=='\t'||s[st]=='\n')) st++; int len=strlen(s)-st; char *r=malloc(len+1); memcpy(r,s+st,len); r[len]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=len; return v; }
static Val bi_rstrip(Val *a, int n) { char *s=a[0].sval; int en=strlen(s)-1; while(en>=0&&(s[en]==' '||s[en]=='\t'||s[en]=='\n')) en--; char *r=malloc(en+2); memcpy(r,s,en+1); r[en+1]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=en+1; return v; }
static Val bi_strip(Val *a, int n) { return bi_trim(a,n); }
static Val bi_substr(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); int start=a[1].type==V_INT?(int)a[1].ival:0; int len=n>2&&a[2].type==V_INT?(int)a[2].ival:sl-start; if(start<0) start=sl+start; if(start<0) start=0; if(len<0) len=0; if(start+len>sl) len=sl-start; char *r=malloc(len+1); memcpy(r,s+start,len); r[len]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=len; return v; }
static Val bi_replace(Val *a, int n) { char *s=a[0].sval; char *old=a[1].sval; char *nw=a[2].sval; int sl=strlen(s); int olen=strlen(old); int nlen=strlen(nw); int cap=sl*2+64; char *r=malloc(cap); int rp=0,sp=0; while(sp<=sl-olen) { if(memcmp(s+sp,old,olen)==0) { if(rp+nlen>=cap){cap*=2;r=realloc(r,cap);} memcpy(r+rp,nw,nlen);rp+=nlen;sp+=olen; } else { if(rp+1>=cap){cap*=2;r=realloc(r,cap);} r[rp++]=s[sp++]; } } while(sp<sl) { if(rp+1>=cap){cap*=2;r=realloc(r,cap);} r[rp++]=s[sp++]; } r[rp]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=rp; return v; }
static Val bi_contains(Val *a, int n) { return val_bool(strstr(a[0].sval,a[1].sval)!=NULL); }
static Val bi_starts_with(Val *a, int n) { return val_bool(strncmp(a[0].sval,a[1].sval,strlen(a[1].sval))==0); }
static Val bi_ends_with(Val *a, int n) { int sl=strlen(a[0].sval); int pl=strlen(a[1].sval); if(pl>sl) return val_bool(0); return val_bool(strcmp(a[0].sval+sl-pl,a[1].sval)==0); }
static Val bi_char_at(Val *a, int n) { int idx=(int)a[1].ival; int sl=strlen(a[0].sval); if(idx<0) idx=sl+idx; if(idx<0||idx>=sl) return val_str(""); char c[2]={a[0].sval[idx],0}; return val_str(c); }
static Val bi_repeat(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); int cnt=(int)a[1].ival; if(cnt<0) cnt=0; long long total=(long long)sl*cnt; if(total>1000000) total=1000000; char *r=malloc((int)total+1); int p=0; for(int i=0;i<cnt&&(long long)p<total;i++){memcpy(r+p,s,sl);p+=sl;} r[p]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=p; return v; }
static Val bi_join(Val *a, int n) { Val lst=a[0]; char *sep=a[1].sval; int slen=strlen(sep); long long total=0; for(int i=0;i<lst.llen;i++){char *s=val_to_str(lst.li[i]);total+=strlen(s);free(s);if(i<lst.llen-1) total+=slen;} char *r=malloc((int)total+1); int p=0; for(int i=0;i<lst.llen;i++){char *s=val_to_str(lst.li[i]);int l=strlen(s);memcpy(r+p,s,l);p+=l;free(s);if(i<lst.llen-1){memcpy(r+p,sep,slen);p+=slen;}} r[p]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=p; return v; }
static Val bi_split(Val *a, int n) { char *s=a[0].sval; char *sep=a[1].sval; Val r=val_list(); int seplen=strlen(sep); if(seplen==0) { for(int i=0;s[i];i++) { char c[2]={s[i],0}; if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);} r.li[r.llen++]=val_str(c); } return r; } char *p=s; while(1) { char *found=strstr(p,sep); if(!found) { if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);} r.li[r.llen++]=val_str(p); break; } int len=found-p; char *tok=malloc(len+1); memcpy(tok,p,len); tok[len]=0; if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);} r.li[r.llen++]=val_str(tok); free(tok); p=found+seplen; } return r; }
static Val bi_count_sub(Val *a, int n) { char *s=a[0].sval; char *sub=a[1].sval; int sl=strlen(sub); if(sl==0) return val_int(0); int c=0; char *p=s; while((p=strstr(p,sub))){c++;p+=sl;} return val_int(c); }
static Val bi_find(Val *a, int n) { char *p=strstr(a[0].sval,a[1].sval); return val_int(p?(int)(p-a[0].sval):-1); }
static Val bi_title(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl+1); int up=1; for(int i=0;i<sl;i++){if(up){r[i]=toupper((unsigned char)s[i]);up=0;}else r[i]=tolower((unsigned char)s[i]);if(s[i]==' ')up=1;} r[sl]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=sl; return v; }
static Val bi_capitalize(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl+1); if(sl>0) r[0]=toupper((unsigned char)s[0]); for(int i=1;i<sl;i++) r[i]=tolower((unsigned char)s[i]); r[sl]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=sl; return v; }
static Val bi_isdigit(Val *a, int n) { char *s=a[0].sval; for(int i=0;s[i];i++) if(!isdigit((unsigned char)s[i])) return val_bool(0); return val_bool(s[0]!=0); }
static Val bi_isalpha(Val *a, int n) { char *s=a[0].sval; for(int i=0;s[i];i++) if(!isalpha((unsigned char)s[i])) return val_bool(0); return val_bool(s[0]!=0); }
static Val bi_isalnum(Val *a, int n) { char *s=a[0].sval; for(int i=0;s[i];i++) if(!isalnum((unsigned char)s[i])) return val_bool(0); return val_bool(s[0]!=0); }
static Val bi_ord(Val *a, int n) { return val_int((unsigned char)a[0].sval[0]); }
static Val bi_chr(Val *a, int n) { char c[2]={(char)a[0].ival,0}; return val_str(c); }
static Val bi_format(Val *a, int n) { char *fmt=a[0].sval; int cap=strlen(fmt)*4+4096; char *r=malloc(cap); int rp=0,ai=1; for(int i=0;fmt[i]&&rp<cap-1;i++){if(fmt[i]=='{'&&fmt[i+1]=='}'){if(ai<n){char *s=val_to_str(a[ai++]);int sl=strlen(s);if(rp+sl>=cap){cap*=2;r=realloc(r,cap);}memcpy(r+rp,s,sl);rp+=sl;free(s);}i++;}else r[rp++]=fmt[i];} r[rp]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=rp; return v; }
static Val bi_range(Val *a, int n) { long long start=0,end=0,step=1; if(n==1) end=a[0].ival; else if(n>=2){start=a[0].ival;end=a[1].ival;} if(n>=3) step=a[2].ival; Val r=val_list(); if(step>0) for(long long i=start;i<end;i+=step){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_int(i);} else if(step<0) for(long long i=start;i>end;i+=step){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_int(i);} return r; }

static int val_sort_cmp(const void *a, const void *b) {
    Val *va=(Val*)a; Val *vb=(Val*)b;
    if(va->type==V_INT&&vb->type==V_INT) return (va->ival>vb->ival)-(va->ival<vb->ival);
    if(va->type==V_FLOAT&&vb->type==V_FLOAT) return (va->fval>vb->fval)-(va->fval<vb->fval);
    if(va->type==V_STR&&vb->type==V_STR) return strcmp(va->sval,vb->sval);
    return 0;
}
static Val bi_sort(Val *a, int n) { Val r=val_copy(a[0]); qsort(r.li,r.llen,sizeof(Val),val_sort_cmp); return r; }
static Val bi_reverse(Val *a, int n) { Val r=val_copy(a[0]); for(int i=0;i<r.llen/2;i++){Val t=r.li[i];r.li[i]=r.li[r.llen-1-i];r.li[r.llen-1-i]=t;} return r; }
static Val bi_slice(Val *a, int n) {
    Val r; memset(&r,0,sizeof r); r.type=V_LIST; r.lcap=16; r.li=malloc(sizeof(Val)*16); r.llen=0;
    if(a[0].type==V_STR) {
        char *s=a[0].sval; int sl=a[0].slen;
        int start=n>1&&a[1].type==V_INT?(int)a[1].ival:0;
        int end=n>2&&a[2].type==V_INT?(int)a[2].ival:sl;
        if(start<0) start=sl+start; if(end<0) end=sl+end;
        if(start<0) start=0; if(end>sl) end=sl; if(start>end) start=end;
        int len=end-start;
        char *rs=malloc(len+1); memcpy(rs,s+start,len); rs[len]=0;
        r.type=V_STR; r.sval=rs; r.slen=len; free(r.li); return r;
    }
    if(a[0].type==V_LIST) {
        Val *src=a[0].li; int slen=a[0].llen;
        int start=n>1&&a[1].type==V_INT?(int)a[1].ival:0;
        int end=n>2&&a[2].type==V_INT?(int)a[2].ival:slen;
        if(start<0) start=slen+start; if(end<0) end=slen+end;
        if(start<0) start=0; if(end>slen) end=slen; if(start>end) start=end;
        for(int i=start;i<end;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_copy(src[i]);}
        return r;
    }
    return r;
}
static Val bi_append(Val *a, int n) { Val r=val_copy(a[0]); if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);} r.li[r.llen++]=val_copy(a[1]); return r; }
static Val bi_extend(Val *a, int n) { Val r=val_copy(a[0]); for(int i=0;i<a[1].llen;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_copy(a[1].li[i]);} return r; }
static Val bi_index_of(Val *a, int n) { for(int i=0;i<a[0].llen;i++) if(val_eq(a[0].li[i],a[1])) return val_int(i); return val_int(-1); }
static Val bi_has(Val *a, int n) { for(int i=0;i<a[0].llen;i++) if(val_eq(a[0].li[i],a[1])) return val_bool(1); return val_bool(0); }

static Val bi_cos(Val *a, int n) { return val_flt(cos(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_sin(Val *a, int n) { return val_flt(sin(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_tan(Val *a, int n) { return val_flt(tan(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_pi(Val *a, int n) { return val_flt(3.14159265358979323846); }
static Val bi_sqrt(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(sqrt(v)); }
static Val bi_floor(Val *a, int n) { return val_int((long long)floor(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_ceil(Val *a, int n) { return val_int((long long)ceil(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_abs(Val *a, int n) { if(a[0].type==V_INT) return val_int(llabs(a[0].ival)); if(a[0].type==V_FLOAT) return val_flt(fabs(a[0].fval)); return val_int(0); }
static Val bi_round(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(round(v)); }
static Val bi_log(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(log(v)); }
static Val bi_log10(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(log10(v)); }
static Val bi_pow_fn(Val *a, int n) { double x=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double y=a[1].type==V_INT?(double)a[1].ival:a[1].fval; return val_flt(pow(x,y)); }
static Val bi_atan(Val *a, int n) { return val_flt(atan(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_asin(Val *a, int n) { return val_flt(asin(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_acos(Val *a, int n) { return val_flt(acos(a[0].type==V_INT?(double)a[0].ival:a[0].fval)); }
static Val bi_min(Val *a, int n) { if(n<1) return val_none(); Val r=a[0]; for(int i=1;i<n;i++) if(a[i].type==V_INT&&r.type==V_INT&&a[i].ival<r.ival) r=a[i]; else if(a[i].type==V_FLOAT&&r.type==V_FLOAT&&a[i].fval<r.fval) r=a[i]; return val_copy(r); }
static Val bi_max(Val *a, int n) { if(n<1) return val_none(); Val r=a[0]; for(int i=1;i<n;i++) if(a[i].type==V_INT&&r.type==V_INT&&a[i].ival>r.ival) r=a[i]; else if(a[i].type==V_FLOAT&&r.type==V_FLOAT&&a[i].fval>r.fval) r=a[i]; return val_copy(r); }

static Val bi_rand_int(Val *a, int n) { long long lo=a[0].ival,hi=a[1].ival; if(lo>hi){long long t=lo;lo=hi;hi=t;} return val_int(lo+rand()%(hi-lo+1)); }
static Val bi_rand_float(Val *a, int n) { return val_flt((double)rand()/RAND_MAX); }
static Val bi_uniform(Val *a, int n) { double lo=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double hi=a[1].type==V_INT?(double)a[1].ival:a[1].fval; return val_flt(lo+(double)rand()/RAND_MAX*(hi-lo)); }
static Val bi_rand_bool(Val *a, int n) { return val_bool(rand()%2==0); }
static Val bi_choice(Val *a, int n) { if(a[0].llen==0) return val_none(); return val_copy(a[0].li[rand()%a[0].llen]); }
static Val bi_shuffle(Val *a, int n) { Val r=val_copy(a[0]); for(int i=r.llen-1;i>0;i--){int j=rand()%(i+1);Val t=r.li[i];r.li[i]=r.li[j];r.li[j]=t;} return r; }
static Val bi_dice(Val *a, int n) { int sides=(int)a[0].ival; if(sides<1) sides=6; return val_int(1+rand()%sides); }
static Val bi_rand_color(Val *a, int n) { char buf[8]; snprintf(buf,8,"#%02X%02X%02X",rand()%256,rand()%256,rand()%256); return val_str(buf); }
static Val bi_rand_string(Val *a, int n) { int len=(int)a[0].ival; if(len<0) len=0; const char *cs="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789"; int cl=62; char *r=malloc(len+1); for(int i=0;i<len;i++) r[i]=cs[rand()%cl]; r[len]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=r; v.slen=len; return v; }
static Val bi_weighted_pick(Val *a, int n) { Val items=a[0]; Val weights=a[1]; double total=0; for(int i=0;i<weights.llen;i++) total+=weights.li[i].type==V_INT?(double)weights.li[i].ival:weights.li[i].fval; double rv=(double)rand()/RAND_MAX*total; double acc=0; for(int i=0;i<items.llen;i++){double w=weights.li[i].type==V_INT?(double)weights.li[i].ival:weights.li[i].fval;acc+=w;if(rv<=acc) return val_copy(items.li[i]);} return val_copy(items.li[items.llen-1]); }
static Val bi_sample(Val *a, int n) { Val lst=val_copy(a[0]); int cnt=(int)a[1].ival; if(cnt>lst.llen) cnt=lst.llen; if(cnt<0) cnt=0; Val result=val_list(); for(int i=0;i<cnt;i++){int j=i+rand()%(lst.llen-i);Val t=lst.li[i];lst.li[i]=lst.li[j];lst.li[j]=t;if(result.llen>=result.lcap){result.lcap*=2;result.li=realloc(result.li,sizeof(Val)*result.lcap);}result.li[result.llen++]=val_copy(lst.li[i]);} for(int i=0;i<lst.llen;i++) val_free(&lst.li[i]); free(lst.li); return result; }
static Val bi_time_now(Val *a, int n) { struct timeval tv; gettimeofday(&tv,NULL); return val_int((long long)tv.tv_sec); }
static Val bi_sleep(Val *a, int n) { double s=a[0].type==V_INT?(double)a[0].ival:a[0].fval; usleep((useconds_t)(s*1000000)); return val_none(); }

static Val bi_read_file(Val *a, int n) { FILE *f=fopen(a[0].sval,"rb"); if(!f){snprintf(mnos_error_msg,sizeof mnos_error_msg-1,"Khong mo duoc: %s",a[0].sval);mnos_error_flag=1;return val_str("");} fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET); char *buf=malloc(sz+1); fread(buf,1,sz,f); buf[sz]=0; fclose(f); Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=buf; v.slen=(int)sz; return v; }
static Val bi_write_file(Val *a, int n) { FILE *f=fopen(a[0].sval,"w"); if(!f){mnos_error_flag=1;return val_bool(0);} fwrite(a[1].sval,1,strlen(a[1].sval),f); fclose(f); return val_bool(1); }
static Val bi_append_file(Val *a, int n) { FILE *f=fopen(a[0].sval,"a"); if(!f){mnos_error_flag=1;return val_bool(0);} fwrite(a[1].sval,1,strlen(a[1].sval),f); fclose(f); return val_bool(1); }
static Val bi_file_exists(Val *a, int n) { struct stat st; return val_bool(stat(a[0].sval,&st)==0&&S_ISREG(st.st_mode)); }
static Val bi_path_exists(Val *a, int n) { struct stat st; return val_bool(stat(a[0].sval,&st)==0); }
static Val bi_is_dir(Val *a, int n) { struct stat st; return val_bool(stat(a[0].sval,&st)==0&&S_ISDIR(st.st_mode)); }
static Val bi_is_file(Val *a, int n) { struct stat st; return val_bool(stat(a[0].sval,&st)==0&&S_ISREG(st.st_mode)); }
static Val bi_file_size(Val *a, int n) { struct stat st; if(stat(a[0].sval,&st)!=0) return val_int(0); return val_int(st.st_size); }
static Val bi_delete_file(Val *a, int n) { return val_bool(remove(a[0].sval)==0); }
static Val bi_make_dir(Val *a, int n) { return val_bool(mkdir(a[0].sval,0755)==0); }
static Val bi_copy_file(Val *a, int n) { FILE *src=fopen(a[0].sval,"rb"); if(!src) return val_bool(0); FILE *dst=fopen(a[1].sval,"wb"); if(!dst){fclose(src);return val_bool(0);} char buf[8192]; size_t nr; while((nr=fread(buf,1,8192,src))>0) fwrite(buf,1,nr,dst); fclose(src); fclose(dst); return val_bool(1); }
static Val bi_move_file(Val *a, int n) { int r=rename(a[0].sval,a[1].sval); if(r==0) return val_bool(1); FILE *src=fopen(a[0].sval,"rb"); if(!src) return val_bool(0); FILE *dst=fopen(a[1].sval,"wb"); if(!dst){fclose(src);return val_bool(0);} char buf[8192]; size_t nr; while((nr=fread(buf,1,8192,src))>0) fwrite(buf,1,nr,dst); fclose(src); fclose(dst); remove(a[0].sval); return val_bool(1); }
static Val bi_list_dir(Val *a, int n) { Val r=val_list(); DIR *d=opendir(a[0].sval); if(!d) return r; struct dirent *ent; while((ent=readdir(d))!=NULL){if(strcmp(ent->d_name,".")==0||strcmp(ent->d_name,"..")==0) continue;if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(ent->d_name);} closedir(d); return r; }
static Val bi_read_lines(Val *a, int n) { Val r=val_list(); FILE *f=fopen(a[0].sval,"r"); if(!f) return r; char buf[65536]; while(fgets(buf,sizeof buf,f)){int len=strlen(buf);while(len>0&&(buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]=0;if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(buf);} fclose(f); return r; }
static Val bi_current_dir(Val *a, int n) { char buf[4096]; getcwd(buf,sizeof buf); return val_str(buf); }
static Val bi_change_dir(Val *a, int n) { return val_bool(chdir(a[0].sval)==0); }
static Val bi_parent_dir(Val *a, int n) { char buf[4096]; strncpy(buf,a[0].sval,sizeof buf); buf[sizeof buf-1]=0; char *last=strrchr(buf,'/'); if(last&&last!=buf) *last=0; else strcpy(buf,"/"); return val_str(buf); }
static Val bi_abs_path(Val *a, int n) { char buf[4096]; if(!realpath(a[0].sval,buf)) strcpy(buf,a[0].sval); return val_str(buf); }
static Val bi_path_join(Val *a, int n) { char buf[8192]; snprintf(buf,sizeof buf,"%s/%s",a[0].sval,a[1].sval); return val_str(buf); }
static Val bi_path_name(Val *a, int n) { char *c=strrchr(a[0].sval,'/'); return val_str(c?c+1:a[0].sval); }
static Val bi_path_ext(Val *a, int n) { char *dot=strrchr(a[0].sval,'.'); return val_str(dot?dot:""); }
static Val bi_path_base(Val *a, int n) { char buf[4096]; strncpy(buf,a[0].sval,sizeof buf); buf[sizeof buf-1]=0; char *dot=strrchr(buf,'.'); char *slash=strrchr(buf,'/'); if(dot&&(slash==NULL||dot>slash)) *dot=0; return val_str(buf); }
static Val bi_system(Val *a, int n) { return val_int(system(a[0].sval)); }
static Val bi_shell(Val *a, int n) { return val_int(system(a[0].sval)); }
static Val bi_which(Val *a, int n) { char cmd[8192]; snprintf(cmd,sizeof cmd,"which %s 2>/dev/null",a[0].sval); FILE *p=popen(cmd,"r"); if(!p) return val_str(""); char out[4096]={0}; fgets(out,sizeof out,p); pclose(p); int len=strlen(out); while(len>0&&(out[len-1]=='\n'||out[len-1]=='\r')) out[--len]=0; return val_str(out); }
static Val bi_env_get(Val *a, int n) { char *v=getenv(a[0].sval); return v?val_str(v):val_str(""); }
static Val bi_set_env(Val *a, int n) { setenv(a[0].sval,a[1].sval,1); return val_none(); }
static Val bi_input(Val *a, int n) { char buf[65536]; if(n>0&&a[0].type==V_STR) printf("%s",a[0].sval); fflush(stdout); if(!fgets(buf,sizeof buf,stdin)) buf[0]=0; int len=strlen(buf); while(len>0&&(buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=strdup(buf); v.slen=len; return v; }
static Val bi_exit_fn(Val *a, int n) { exit(n>0?(int)a[0].ival:0); return val_none(); }
static Val bi_version(Val *a, int n) { return val_str("Manios v1.0.0"); }
static Val bi_args_fn(Val *a, int n) { return val_copy(g_args_val); }
static Val bi_hide_cursor(Val *a, int n) { printf("\033[?25l"); fflush(stdout); return val_none(); }
static Val bi_show_cursor(Val *a, int n) { printf("\033[?25h"); fflush(stdout); return val_none(); }
static Val bi_clear_screen(Val *a, int n) { printf("\033[2J\033[H"); fflush(stdout); return val_none(); }
static Val bi_term_width(Val *a, int n) { struct winsize w; ioctl(STDOUT_FILENO,TIOCGWINSZ,&w); return val_int(w.ws_col); }
static Val bi_term_height(Val *a, int n) { struct winsize w; ioctl(STDOUT_FILENO,TIOCGWINSZ,&w); return val_int(w.ws_row); }
static Val bi_move_cursor(Val *a, int n) { printf("\033[%d;%dH",(int)a[0].ival,(int)a[1].ival); fflush(stdout); return val_none(); }
static Val bi_color(Val *a, int n) { printf("\033[%sm",a[0].sval); fflush(stdout); return val_none(); }
static Val bi_bold_color(Val *a, int n) { printf("\033[1;%sm",a[0].sval); fflush(stdout); return val_none(); }
static Val bi_reset_color(Val *a, int n) { printf("\033[0m"); fflush(stdout); return val_none(); }
static Val bi_flush(Val *a, int n) { fflush(stdout); return val_none(); }
static Val bi_write(Val *a, int n) { char *s=val_to_str(a[0]); printf("%s",s); fflush(stdout); free(s); return val_none(); }

static void write_u16_be(FILE *f, uint16_t v) { uint8_t b[2]={(v>>8)&0xFF,v&0xFF}; fwrite(b,1,2,f); }
static uint16_t read_u16_be(FILE *f) { uint8_t b[2]; fread(b,1,2,f); return ((uint16_t)b[0]<<8)|b[1]; }
static void write_u32_be(FILE *f, uint32_t v) { uint8_t b[4]={(v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF}; fwrite(b,1,4,f); }
static uint32_t read_u32_be(FILE *f) { uint8_t b[4]; fread(b,1,4,f); return ((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3]; }
static void write_i64_be(FILE *f, int64_t v) { for(int i=56;i>=0;i-=8){uint8_t b=(v>>i)&0xFF;fwrite(&b,1,1,f);} }
static int64_t read_i64_be(FILE *f) { int64_t v=0; for(int i=0;i<8;i++){uint8_t b;fread(&b,1,1,f);v=(v<<8)|b;} return v; }
static void write_f64_be(FILE *f, double v) { uint64_t u; memcpy(&u,&v,8); for(int i=56;i>=0;i-=8){uint8_t b=(u>>i)&0xFF;fwrite(&b,1,1,f);} }
static double read_f64_be(FILE *f) { uint64_t u=0; for(int i=0;i<8;i++){uint8_t b;fread(&b,1,1,f);u=(u<<8)|b;} double v; memcpy(&v,&u,8); return v; }

typedef struct { uint8_t *data; int len; int cap; } SerBuf;
static void sbuf_init(SerBuf *b) { b->len=0; b->cap=256; b->data=malloc(256); }
static void sbuf_write(SerBuf *b, void *d, int n) { while(b->len+n>b->cap){b->cap*=2;b->data=realloc(b->data,b->cap);} memcpy(b->data+b->len,d,n); b->len+=n; }
static void sbuf_u16(SerBuf *b, uint16_t v) { uint8_t d[2]={(v>>8)&0xFF,v&0xFF}; sbuf_write(b,d,2); }
static void sbuf_u32(SerBuf *b, uint32_t v) { uint8_t d[4]={(v>>24)&0xFF,(v>>16)&0xFF,(v>>8)&0xFF,v&0xFF}; sbuf_write(b,d,4); }
static void sbuf_i64(SerBuf *b, int64_t v) { for(int i=56;i>=0;i-=8){uint8_t d=(v>>i)&0xFF;sbuf_write(b,&d,1);} }
static void sbuf_f64(SerBuf *b, double v) { uint64_t u; memcpy(&u,&v,8); for(int i=56;i>=0;i-=8){uint8_t d=(u>>i)&0xFF;sbuf_write(b,&d,1);} }

static void mnos_ser_file(Val v, FILE *f) {
    switch(v.type) {
        case V_NONE: write_u16_be(f,MNOS_TAG_NONE); break;
        case V_BOOL: { write_u16_be(f,MNOS_TAG_BOOL); uint8_t b=v.bval?1:0; fwrite(&b,1,1,f); break; }
        case V_INT: write_u16_be(f,MNOS_TAG_INT); write_i64_be(f,v.ival); break;
        case V_FLOAT: write_u16_be(f,MNOS_TAG_FLOAT); write_f64_be(f,v.fval); break;
        case V_STR: case V_BYTES: write_u16_be(f,MNOS_TAG_TEXT); write_u32_be(f,(uint32_t)v.slen); fwrite(v.sval,1,v.slen,f); break;
        case V_LIST: { write_u16_be(f,MNOS_TAG_LIST); write_u32_be(f,(uint32_t)v.llen); for(int i=0;i<v.llen;i++) mnos_ser_file(v.li[i],f); break; }
        case V_DICT: { write_u16_be(f,MNOS_TAG_MAP); write_u32_be(f,(uint32_t)v.dlen); for(int i=0;i<v.dlen;i++){Val k=val_str(v.dkeys[i]);mnos_ser_file(k,f);mnos_ser_file(v.dvals[i],f);val_free(&k);} break; }
        case V_TUPLE: { write_u16_be(f,MNOS_TAG_TUPLE); write_u32_be(f,(uint32_t)v.tlen); for(int i=0;i<v.tlen;i++) mnos_ser_file(v.tu[i],f); break; }
        default: write_u16_be(f,MNOS_TAG_NONE); break;
    }
}
static void mnos_ser_buf(Val v, SerBuf *b) {
    switch(v.type) {
        case V_NONE: sbuf_u16(b,MNOS_TAG_NONE); break;
        case V_BOOL: { sbuf_u16(b,MNOS_TAG_BOOL); uint8_t bv=v.bval?1:0; sbuf_write(b,&bv,1); break; }
        case V_INT: sbuf_u16(b,MNOS_TAG_INT); sbuf_i64(b,v.ival); break;
        case V_FLOAT: sbuf_u16(b,MNOS_TAG_FLOAT); sbuf_f64(b,v.fval); break;
        case V_STR: case V_BYTES: sbuf_u16(b,MNOS_TAG_TEXT); sbuf_u32(b,(uint32_t)v.slen); sbuf_write(b,v.sval,v.slen); break;
        case V_LIST: { sbuf_u16(b,MNOS_TAG_LIST); sbuf_u32(b,(uint32_t)v.llen); for(int i=0;i<v.llen;i++) mnos_ser_buf(v.li[i],b); break; }
        case V_DICT: { sbuf_u16(b,MNOS_TAG_MAP); sbuf_u32(b,(uint32_t)v.dlen); for(int i=0;i<v.dlen;i++){Val k=val_str(v.dkeys[i]);mnos_ser_buf(k,b);mnos_ser_buf(v.dvals[i],b);val_free(&k);} break; }
        case V_TUPLE: { sbuf_u16(b,MNOS_TAG_TUPLE); sbuf_u32(b,(uint32_t)v.tlen); for(int i=0;i<v.tlen;i++) mnos_ser_buf(v.tu[i],b); break; }
        default: sbuf_u16(b,MNOS_TAG_NONE); break;
    }
}
static Val mnos_deser_file(FILE *f) {
    if(feof(f)) return val_none();
    uint16_t tag=read_u16_be(f);
    switch(tag) {
        case MNOS_TAG_NONE: return val_none();
        case MNOS_TAG_BOOL: { uint8_t b; if(fread(&b,1,1,f)!=1) return val_none(); return val_bool(b); }
        case MNOS_TAG_INT: return val_int(read_i64_be(f));
        case MNOS_TAG_FLOAT: return val_flt(read_f64_be(f));
        case MNOS_TAG_TEXT: { uint32_t len=read_u32_be(f); char *s=malloc(len+1); if(fread(s,1,len,f)!=len){free(s);return val_none();} s[len]=0; Val v; memset(&v,0,sizeof v); v.type=V_STR; v.sval=s; v.slen=(int)len; return v; }
        case MNOS_TAG_LIST: { uint32_t cnt=read_u32_be(f); Val r=val_list(); for(uint32_t i=0;i<cnt;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=mnos_deser_file(f);} return r; }
        case MNOS_TAG_MAP: { uint32_t cnt=read_u32_be(f); Val r=val_dict(); for(uint32_t i=0;i<cnt;i++){Val k=mnos_deser_file(f);Val v=mnos_deser_file(f);if(r.dlen>=r.dcap){r.dcap*=2;r.dkeys=realloc(r.dkeys,sizeof(char*)*r.dcap);r.dvals=realloc(r.dvals,sizeof(Val)*r.dcap);}r.dkeys[r.dlen]=strdup(k.sval);r.dvals[r.dlen]=val_copy(v);r.dlen++;val_free(&k);val_free(&v);} return r; }
        case MNOS_TAG_TUPLE: { uint32_t cnt=read_u32_be(f); Val *items=malloc(sizeof(Val)*cnt); for(uint32_t i=0;i<cnt;i++) items[i]=mnos_deser_file(f); Val r=val_tuple(items,(int)cnt); for(uint32_t i=0;i<cnt;i++) val_free(&items[i]); free(items); return r; }
        default: return val_none();
    }
}

static Val bi_mnos_dump(Val *a, int n) { FILE *f=fopen(a[1].sval,"wb"); if(!f){mnos_error_flag=1;return val_bool(0);} write_u16_be(f,MNOS_SER_VERSION); mnos_ser_file(a[0],f); write_u16_be(f,MNOS_TAG_EOF); fclose(f); return val_bool(1); }
static Val bi_mnos_load(Val *a, int n) { FILE *f=fopen(a[0].sval,"rb"); if(!f){mnos_error_flag=1;return val_none();} read_u16_be(f); Val r=mnos_deser_file(f); fclose(f); return r; }
static Val bi_mnos_pack(Val *a, int n) { SerBuf b; sbuf_init(&b); sbuf_u16(&b,MNOS_SER_VERSION); mnos_ser_buf(a[0],&b); sbuf_u16(&b,MNOS_TAG_EOF); Val v; memset(&v,0,sizeof v); v.type=V_BYTES; v.sval=(char*)b.data; v.slen=b.len; return v; }
static Val bi_mnos_unpack(Val *a, int n) { FILE *f=fmemopen((void*)a[0].sval,a[0].slen,"rb"); if(!f) return val_none(); read_u16_be(f); Val r=mnos_deser_file(f); fclose(f); return r; }
static Val bi_mnos_version(Val *a, int n) { return val_str("1.0.0"); }

static Val bi_dict_get(Val *a, int n) { if(a[0].type!=V_DICT) return val_none(); for(int i=0;i<a[0].dlen;i++) if(strcmp(a[0].dkeys[i],a[1].sval)==0) return val_copy(a[0].dvals[i]); return val_none(); }
static Val bi_dict_set(Val *a, int n) { if(a[0].type!=V_DICT) return val_none(); for(int i=0;i<a[0].dlen;i++){if(strcmp(a[0].dkeys[i],a[1].sval)==0){val_free(&a[0].dvals[i]);a[0].dvals[i]=val_copy(a[2]);return val_copy(a[2]);}} if(a[0].dlen>=a[0].dcap){a[0].dcap*=2;a[0].dkeys=realloc(a[0].dkeys,sizeof(char*)*a[0].dcap);a[0].dvals=realloc(a[0].dvals,sizeof(Val)*a[0].dcap);} a[0].dkeys[a[0].dlen]=strdup(a[1].sval); a[0].dvals[a[0].dlen]=val_copy(a[2]); a[0].dlen++; return val_copy(a[2]); }
static Val bi_dict_has(Val *a, int n) { if(a[0].type!=V_DICT) return val_bool(0); for(int i=0;i<a[0].dlen;i++) if(strcmp(a[0].dkeys[i],a[1].sval)==0) return val_bool(1); return val_bool(0); }
static Val bi_dict_keys(Val *a, int n) { Val r=val_list(); if(a[0].type!=V_DICT) return r; for(int i=0;i<a[0].dlen;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(a[0].dkeys[i]);} return r; }
static Val bi_dict_vals(Val *a, int n) { Val r=val_list(); if(a[0].type!=V_DICT) return r; for(int i=0;i<a[0].dlen;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_copy(a[0].dvals[i]);} return r; }
static Val bi_dict_remove(Val *a, int n) { if(a[0].type!=V_DICT) return val_bool(0); for(int i=0;i<a[0].dlen;i++){if(strcmp(a[0].dkeys[i],a[1].sval)==0){free(a[0].dkeys[i]);val_free(&a[0].dvals[i]);for(int j=i;j<a[0].dlen-1;j++){a[0].dkeys[j]=a[0].dkeys[j+1];a[0].dvals[j]=a[0].dvals[j+1];} a[0].dlen--;return val_bool(1);}} return val_bool(0); }
static Val bi_dict_len(Val *a, int n) { return val_int(a[0].type==V_DICT?a[0].dlen:0); }
static Val bi_dict_merge(Val *a, int n) { Val r=val_copy(a[0]); for(int i=0;i<a[1].dlen;i++){int found=0;for(int j=0;j<r.dlen;j++){if(strcmp(r.dkeys[j],a[1].dkeys[i])==0){val_free(&r.dvals[j]);r.dvals[j]=val_copy(a[1].dvals[i]);found=1;break;}} if(!found){if(r.dlen>=r.dcap){r.dcap*=2;r.dkeys=realloc(r.dkeys,sizeof(char*)*r.dcap);r.dvals=realloc(r.dvals,sizeof(Val)*r.dcap);}r.dkeys[r.dlen]=strdup(a[1].dkeys[i]);r.dvals[r.dlen]=val_copy(a[1].dvals[i]);r.dlen++;}} return r; }
static Val bi_dict_items(Val *a, int n) { Val r=val_list(); if(a[0].type!=V_DICT) return r; for(int i=0;i<a[0].dlen;i++){Val pair=val_list();if(pair.llen>=pair.lcap){pair.lcap*=2;pair.li=realloc(pair.li,sizeof(Val)*pair.lcap);}pair.li[pair.llen++]=val_str(a[0].dkeys[i]);if(pair.llen>=pair.lcap){pair.lcap*=2;pair.li=realloc(pair.li,sizeof(Val)*pair.lcap);}pair.li[pair.llen++]=val_copy(a[0].dvals[i]);if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=pair;} return r; }
static Val bi_dict_clear(Val *a, int n) { if(a[0].type!=V_DICT) return val_none(); for(int i=0;i<a[0].dlen;i++){free(a[0].dkeys[i]);val_free(&a[0].dvals[i]);} a[0].dlen=0; return val_none(); }
static Val bi_dict_update(Val *a, int n) { if(a[0].type!=V_DICT||a[1].type!=V_DICT) return val_none(); for(int i=0;i<a[1].dlen;i++){int found=0;for(int j=0;j<a[0].dlen;j++){if(strcmp(a[0].dkeys[j],a[1].dkeys[i])==0){val_free(&a[0].dvals[j]);a[0].dvals[j]=val_copy(a[1].dvals[i]);found=1;break;}}if(!found){if(a[0].dlen>=a[0].dcap){a[0].dcap*=2;a[0].dkeys=realloc(a[0].dkeys,sizeof(char*)*a[0].dcap);a[0].dvals=realloc(a[0].dvals,sizeof(Val)*a[0].dcap);}a[0].dkeys[a[0].dlen]=strdup(a[1].dkeys[i]);a[0].dvals[a[0].dlen]=val_copy(a[1].dvals[i]);a[0].dlen++;}} return val_none(); }
static Val bi_dict_pop(Val *a, int n) { if(a[0].type!=V_DICT) return val_none(); for(int i=0;i<a[0].dlen;i++){if(strcmp(a[0].dkeys[i],a[1].sval)==0){Val r=val_copy(a[0].dvals[i]);free(a[0].dkeys[i]);val_free(&a[0].dvals[i]);for(int j=i;j<a[0].dlen-1;j++){a[0].dkeys[j]=a[0].dkeys[j+1];a[0].dvals[j]=a[0].dvals[j+1];}a[0].dlen--;return r;}} return n>2?val_copy(a[2]):val_none(); }
static Val bi_dict_copy(Val *a, int n) { return val_copy(a[0]); }
static Val bi_list_insert(Val *a, int n) { if(a[0].type!=V_LIST) return val_none(); Val r=val_copy(a[0]); int idx=(int)a[1].ival; if(idx<0) idx=r.llen+idx; if(idx<0) idx=0; if(idx>r.llen) idx=r.llen; if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);} for(int i=r.llen;i>idx;i--) r.li[i]=r.li[i-1]; r.li[idx]=val_copy(a[2]); r.llen++; return r; }
static Val bi_list_remove(Val *a, int n) { if(a[0].type!=V_LIST) return val_bool(0); Val r=val_copy(a[0]); for(int i=0;i<r.llen;i++){if(val_eq(r.li[i],a[1])){val_free(&r.li[i]);for(int j=i;j<r.llen-1;j++) r.li[j]=r.li[j+1];r.llen--;return val_bool(1);}} return val_bool(0); }
static Val bi_list_pop(Val *a, int n) { if(a[0].type!=V_LIST||a[0].llen==0) return val_none(); Val r; int idx=n>1?(int)a[1].ival:a[0].llen-1; if(idx<0) idx=a[0].llen+idx; if(idx<0||idx>=a[0].llen) return val_none(); r=val_copy(a[0].li[idx]); val_free(&a[0].li[idx]); for(int i=idx;i<a[0].llen-1;i++) a[0].li[i]=a[0].li[i+1]; a[0].llen--; return r; }
static Val bi_list_count(Val *a, int n) { if(a[0].type!=V_LIST) return val_int(0); int c=0; for(int i=0;i<a[0].llen;i++) if(val_eq(a[0].li[i],a[1])) c++; return val_int(c); }
static Val bi_list_flatten(Val *a, int n) { Val r=val_list(); for(int i=0;i<a[0].llen;i++){if(a[0].li[i].type==V_LIST){Val sub=bi_list_flatten(a[0].li+i,1);for(int j=0;j<sub.llen;j++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_copy(sub.li[j]);}val_free(&sub);}else{if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_copy(a[0].li[i]);}} return r; }
static Val bi_list_copy(Val *a, int n) { return val_copy(a[0]); }
static Val bi_list_clear(Val *a, int n) { if(a[0].type!=V_LIST) return val_none(); for(int i=0;i<a[0].llen;i++) val_free(&a[0].li[i]); a[0].llen=0; return val_none(); }
static Val bi_enumerate(Val *a, int n) { Val r=val_list(); int start=n>1?(int)a[1].ival:0; if(a[0].type!=V_LIST) return r; for(int i=0;i<a[0].llen;i++){Val pair=val_list();if(pair.llen>=pair.lcap){pair.lcap*=2;pair.li=realloc(pair.li,sizeof(Val)*pair.lcap);}pair.li[pair.llen++]=val_int(i+start);if(pair.llen>=pair.lcap){pair.lcap*=2;pair.li=realloc(pair.li,sizeof(Val)*pair.lcap);}pair.li[pair.llen++]=val_copy(a[0].li[i]);if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=pair;} return r; }
static Val bi_zip(Val *a, int n) { Val r=val_list(); if(n<2) return r; int minlen=a[0].llen; for(int i=1;i<n;i++) if(a[i].llen<minlen) minlen=a[i].llen; for(int i=0;i<minlen;i++){Val pair=val_list();for(int j=0;j<n;j++){if(pair.llen>=pair.lcap){pair.lcap*=2;pair.li=realloc(pair.li,sizeof(Val)*pair.lcap);}pair.li[pair.llen++]=val_copy(a[j].li[i]);}if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=pair;} return r; }
static Val bi_map_fn(Val *a, int n) { return val_none(); }
static Val bi_filter_fn(Val *a, int n) { return val_none(); }
static Val bi_any_fn(Val *a, int n) { if(a[0].type!=V_LIST) return val_bool(0); for(int i=0;i<a[0].llen;i++) if(val_truthy(a[0].li[i])) return val_bool(1); return val_bool(0); }
static Val bi_all_fn(Val *a, int n) { if(a[0].type!=V_LIST) return val_bool(1); for(int i=0;i<a[0].llen;i++) if(!val_truthy(a[0].li[i])) return val_bool(0); return val_bool(1); }
static Val bi_sum_fn(Val *a, int n) { Val r=val_int(0); if(a[0].type!=V_LIST) return r; for(int i=0;i<a[0].llen;i++){if(a[0].li[i].type==V_INT){r.type=V_INT;r.ival+=a[0].li[i].ival;}else if(a[0].li[i].type==V_FLOAT){if(r.type==V_INT){r.type=V_FLOAT;r.fval=(double)r.ival;}r.fval+=a[0].li[i].fval;}} return r; }
static Val bi_sorted_fn(Val *a, int n) { return bi_sort(a,n); }
static Val bi_reversed_fn(Val *a, int n) { return bi_reverse(a,n); }
static Val bi_hex(Val *a, int n) { char buf[32]; snprintf(buf,32,"0x%llx",(unsigned long long)a[0].ival); return val_str(buf); }
static Val bi_oct(Val *a, int n) { char buf[32]; snprintf(buf,32,"0o%llo",(unsigned long long)a[0].ival); return val_str(buf); }
static Val bi_bin(Val *a, int n) { char buf[128]; char *p=buf+127; *p=0; unsigned long long v=(unsigned long long)a[0].ival; if(v==0){*(--p)='0';return val_str(p);} int cnt=0; while(v>0){*(--p)='0'+(v&1);v>>=1;cnt++;} if(cnt%4!=0){int pad=4-(cnt%4);for(int i=0;i<pad;i++) *(--p)='0';} return val_str(p); }
static Val bi_repr_fn(Val *a, int n) { if(a[0].type==V_STR){int sl=strlen(a[0].sval);char *r=malloc(sl+3);r[0]='"';memcpy(r+1,a[0].sval,sl);r[sl+1]='"';r[sl+2]=0;Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=sl+2;return v;} char *s=val_to_str(a[0]); Val r2=val_str(s); free(s); return r2; }
static Val bi_isinstance(Val *a, int n) { if(n<2) return val_bool(0); char *tn=a[1].sval; if(a[0].type==V_INT&&strcmp(tn,"int")==0) return val_bool(1); if(a[0].type==V_FLOAT&&strcmp(tn,"float")==0) return val_bool(1); if(a[0].type==V_STR&&strcmp(tn,"str")==0) return val_bool(1); if(a[0].type==V_BOOL&&strcmp(tn,"bool")==0) return val_bool(1); if(a[0].type==V_LIST&&(strcmp(tn,"list")==0||strcmp(tn,"tuple")==0)) return val_bool(1); if(a[0].type==V_DICT&&strcmp(tn,"dict")==0) return val_bool(1); if(a[0].type==V_TUPLE&&strcmp(tn,"tuple")==0) return val_bool(1); if(a[0].type==V_NONE&&strcmp(tn,"none")==0) return val_bool(1); if(a[0].type==V_OBJ){ObjInst *o=(ObjInst*)a[0].obj;if(strcmp(o->cls->name,tn)==0) return val_bool(1);} return val_bool(0); }
static Val bi_bool_fn(Val *a, int n) { return val_bool(val_truthy(a[0])); }
static Val bi_tuple_fn(Val *a, int n) { return val_tuple(a,n); }
static Val bi_list_fn(Val *a, int n) { if(n>0&&a[0].type==V_LIST) return val_copy(a[0]); if(n>0&&a[0].type==V_TUPLE){Val r=val_list();for(int i=0;i<a[0].tlen;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_copy(a[0].tu[i]);}return r;} return val_list(); }
static Val bi_dict_fn(Val *a, int n) { if(n>0&&a[0].type==V_DICT) return val_copy(a[0]); return val_dict(); }
static Val bi_val_none_fn(Val *a, int n) { return val_none(); }
static Val bi_exp(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(exp(v)); }
static Val bi_degrees(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(v*180.0/3.14159265358979323846); }
static Val bi_radians(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(v*3.14159265358979323846/180.0); }
static Val bi_gcd_fn(Val *a, int n) { long long x=llabs(a[0].ival),y=llabs(a[1].ival); while(y){long long t=y;y=x%y;x=t;} return val_int(x); }
static Val bi_modf_fn(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double ipart; double fpart=modf(v,&ipart); Val r=val_list();if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_flt(ipart);if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_flt(fpart); return r; }
static Val bi_trunc(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_int((long long)trunc(v)); }
static Val bi_copysign(Val *a, int n) { double x=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double y=a[1].type==V_INT?(double)a[1].ival:a[1].fval; return val_flt(copysign(x,y)); }
static Val bi_fmod_fn(Val *a, int n) { double x=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double y=a[1].type==V_INT?(double)a[1].ival:a[1].fval; return val_flt(fmod(x,y)); }
static Val bi_hypot_fn(Val *a, int n) { double x=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double y=a[1].type==V_INT?(double)a[1].ival:a[1].fval; return val_flt(hypot(x,y)); }
static Val bi_atan2_fn(Val *a, int n) { double x=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double y=a[1].type==V_INT?(double)a[1].ival:a[1].fval; return val_flt(atan2(y,x)); }
static Val bi_inf_fn(Val *a, int n) { return val_flt(1.0/0.0); }
static Val bi_nan_fn(Val *a, int n) { return val_flt(0.0/0.0); }
static Val bi_e_fn(Val *a, int n) { return val_flt(2.71828182845904523536); }
static Val bi_log2_fn(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_flt(log2(v)); }
static Val bi_isnan(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_bool(v!=v); }
static Val bi_isinf(Val *a, int n) { double v=a[0].type==V_INT?(double)a[0].ival:a[0].fval; return val_bool(v==1.0/0.0||v==-1.0/0.0); }
static Val bi_str_ljust(Val *a, int n) { int w=(int)a[1].ival; char fc=n>2&&a[2].type==V_STR&&a[2].slen>0?a[2].sval[0]:' '; int sl=strlen(a[0].sval); int pad=w>sl?w-sl:0; char *r=malloc(w+1); memcpy(r,a[0].sval,sl); for(int i=0;i<pad;i++) r[sl+i]=fc; r[w]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=w; return v; }
static Val bi_str_rjust(Val *a, int n) { int w=(int)a[1].ival; char fc=n>2&&a[2].type==V_STR&&a[2].slen>0?a[2].sval[0]:' '; int sl=strlen(a[0].sval); int pad=w>sl?w-sl:0; char *r=malloc(w+1); for(int i=0;i<pad;i++) r[i]=fc; memcpy(r+pad,a[0].sval,sl); r[w]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=w; return v; }
static Val bi_str_center(Val *a, int n) { int w=(int)a[1].ival; char fc=n>2&&a[2].type==V_STR&&a[2].slen>0?a[2].sval[0]:' '; int sl=strlen(a[0].sval); int pad=w>sl?w-sl:0; int lp=pad/2,rp=pad-lp; char *r=malloc(w+1); for(int i=0;i<lp;i++) r[i]=fc; memcpy(r+lp,a[0].sval,sl); for(int i=0;i<rp;i++) r[lp+sl+i]=fc; r[w]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=w; return v; }
static Val bi_str_zfill(Val *a, int n) { int w=(int)a[1].ival; char *s=a[0].sval; int sl=strlen(s); int neg=(sl>0&&s[0]=='-'); int digits=sl-(neg?1:0); int pad=w>digits?w-digits:0; char *r=malloc(w+1); int p=0; if(neg) r[p++]='-'; for(int i=0;i<pad;i++) r[p++]='0'; memcpy(r+p,s+(neg?1:0),digits); p+=digits; r[p]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=p; return v; }
static Val bi_str_partition(Val *a, int n) { char *s=a[0].sval; char *sep=a[1].sval; Val r=val_list(); char *found=strstr(s,sep); if(!found){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(s);if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str("");if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str("");return r;} int bl=found-s; char *before=malloc(bl+1);memcpy(before,s,bl);before[bl]=0; char *after=strdup(found+strlen(sep)); if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(before);if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(sep);if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(after); free(before);free(after); return r; }
static Val bi_str_rfind(Val *a, int n) { char *s=a[0].sval; char *sub=a[1].sval; int slen=strlen(sub); int sl=strlen(s); if(slen==0) return val_int(sl); for(int i=sl-slen;i>=0;i--){if(memcmp(s+i,sub,slen)==0) return val_int(i);} return val_int(-1); }
static Val bi_str_rsplit(Val *a, int n) { char *s=a[0].sval; char *sep=a[1].sval; Val r=val_list(); int seplen=strlen(sep); if(seplen==0) return r; int sl=strlen(s); int maxsplit=n>2?(int)a[2].ival:sl; int cnt=0; char *end=s+sl; while(cnt<maxsplit){char *found=NULL;int best=-1;for(char *p=s;p+seplen<=end;p++){if(memcmp(p,sep,seplen)==0) found=p;}if(!found) break;int len=found-s;if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str("");strncpy(r.li[r.llen-1].sval,s,len);r.li[r.llen-1].sval[len]=0;r.li[r.llen-1].slen=len;s=found+seplen;cnt++;} if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(s); Val rev=val_list(); for(int i=r.llen-1;i>=0;i--){if(rev.llen>=rev.lcap){rev.lcap*=2;rev.li=realloc(rev.li,sizeof(Val)*rev.lcap);}rev.li[rev.llen++]=r.li[i];} return rev; }
static Val bi_str_splitlines(Val *a, int n) { Val r=val_list(); char *s=a[0].sval; int sl=strlen(s); int start=0; for(int i=0;i<sl;i++){if(s[i]=='\n'||s[i]=='\r'){int len=i-start;char *line=malloc(len+1);memcpy(line,s+start,len);line[len]=0;if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(line);free(line);start=i+1;if(s[i]=='\r'&&i+1<sl&&s[i+1]=='\n') start++;}} if(start<sl){int len=sl-start;char *line=malloc(len+1);memcpy(line,s+start,len);line[len]=0;if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(line);free(line);} return r; }
static Val bi_str_swapcase(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl+1); for(int i=0;i<sl;i++){if(isupper((unsigned char)s[i])) r[i]=tolower((unsigned char)s[i]);else if(islower((unsigned char)s[i])) r[i]=toupper((unsigned char)s[i]);else r[i]=s[i];} r[sl]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=sl; return v; }
static Val bi_str_isupper(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); if(sl==0) return val_bool(0); int has_alpha=0; for(int i=0;i<sl;i++){if(isalpha((unsigned char)s[i])){has_alpha=1;if(!isupper((unsigned char)s[i])) return val_bool(0);}} return val_bool(has_alpha); }
static Val bi_str_islower(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); if(sl==0) return val_bool(0); int has_alpha=0; for(int i=0;i<sl;i++){if(isalpha((unsigned char)s[i])){has_alpha=1;if(!islower((unsigned char)s[i])) return val_bool(0);}} return val_bool(has_alpha); }
static Val bi_str_isspace(Val *a, int n) { char *s=a[0].sval; if(!s[0]) return val_bool(0); for(int i=0;s[i];i++) if(!isspace((unsigned char)s[i])) return val_bool(0); return val_bool(1); }
static Val bi_str_isnumeric(Val *a, int n) { char *s=a[0].sval; if(!s[0]) return val_bool(0); for(int i=0;s[i];i++) if(!isdigit((unsigned char)s[i])&&s[i]!='.'&&s[i]!='-') return val_bool(0); return val_bool(1); }
static Val bi_str_lpad(Val *a, int n) { return bi_str_ljust(a,n); }
static Val bi_str_rpad(Val *a, int n) { return bi_str_rjust(a,n); }
static Val bi_str_just(Val *a, int n) { return bi_str_ljust(a,n); }
static Val bi_str_escape(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl*4+1); int p=0; for(int i=0;i<sl;i++){unsigned char c=s[i];if(c=='\n'){r[p++]='\\';r[p++]='n';}else if(c=='\t'){r[p++]='\\';r[p++]='t';}else if(c=='\r'){r[p++]='\\';r[p++]='r';}else if(c=='\\'){r[p++]='\\';r[p++]='\\';}else if(c=='"'){r[p++]='\\';r[p++]='"';}else if(c<32||c>126){p+=snprintf(r+p,4,"\\x%02X",c);}else r[p++]=c;} r[p]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=p; return v; }
static Val bi_str_unescape(Val *a, int n) { char *s=a[0].sval; int sl=strlen(s); char *r=malloc(sl+1); int p=0; for(int i=0;i<sl;i++){if(s[i]=='\\'&&i+1<sl){i++;switch(s[i]){case 'n':r[p++]='\n';break;case 't':r[p++]='\t';break;case 'r':r[p++]='\r';break;default:r[p++]=s[i];break;}}else r[p++]=s[i];} r[p]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=p; return v; }
static Val bi_str_dedent(Val *a, int n) { char *s=a[0].sval; Val lines=bi_str_splitlines(a,n); if(lines.llen==0) return val_str(""); int minindent=INT_MAX; for(int i=0;i<lines.llen;i++){char *l=lines.li[i].sval;int ind=0;while(l[ind]==' '||l[ind]=='\t') ind++;if(l[ind]!='\0'&&ind<minindent) minindent=ind;} if(minindent==INT_MAX) minindent=0; int total=0; for(int i=0;i<lines.llen;i++) total+=strlen(lines.li[i].sval)-minindent+(i<lines.llen-1?1:0); char *r=malloc(total+1); int p=0; for(int i=0;i<lines.llen;i++){char *l=lines.li[i].sval;int sl2=strlen(l);if(sl2>minindent){memcpy(r+p,l+minindent,sl2-minindent);p+=sl2-minindent;}if(i<lines.llen-1) r[p++]='\n';} r[p]=0; val_free(&lines); Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=p; return v; }
static Val bi_str_indent(Val *a, int n) { char *s=a[0].sval; int nspaces=(int)a[1].ival; Val lines=bi_str_splitlines(a,0); int total=0; for(int i=0;i<lines.llen;i++) total+=nspaces+strlen(lines.li[i].sval)+(i<lines.llen-1?1:0); char *r=malloc(total+1); int p=0; for(int i=0;i<lines.llen;i++){for(int j=0;j<nspaces;j++) r[p++]=' ';int sl2=strlen(lines.li[i].sval);memcpy(r+p,lines.li[i].sval,sl2);p+=sl2;if(i<lines.llen-1) r[p++]='\n';} r[p]=0; val_free(&lines); Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=p; return v; }
static Val bi_str_word_count(Val *a, int n) { char *s=a[0].sval; int cnt=0; int inword=0; for(int i=0;s[i];i++){if(isspace((unsigned char)s[i])) inword=0;else{if(!inword) cnt++;inword=1;}} return val_int(cnt); }
static Val bi_str_lines(Val *a, int n) { return bi_str_splitlines(a,n); }
static Val bi_str_bytes_len(Val *a, int n) { return val_int(a[0].slen); }
static Val bi_str_to_bytes(Val *a, int n) { Val r=val_list(); for(int i=0;i<a[0].slen;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_int((unsigned char)a[0].sval[i]);} return r; }
static Val bi_str_from_bytes(Val *a, int n) { if(a[0].type!=V_LIST) return val_str(""); int len=a[0].llen; char *r=malloc(len+1); for(int i=0;i<len;i++) r[i]=(char)a[0].li[i].ival; r[len]=0; Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=r;v.slen=len; return v; }
static Val bi_time_ms(Val *a, int n) { struct timeval tv; gettimeofday(&tv,NULL); return val_int((long long)tv.tv_sec*1000+tv.tv_usec/1000); }
static Val bi_time_us(Val *a, int n) { struct timeval tv; gettimeofday(&tv,NULL); return val_int((long long)tv.tv_sec*1000000+tv.tv_usec); }
static Val bi_time_format(Val *a, int n) { time_t t=(time_t)(a[0].type==V_INT?a[0].ival:0); struct tm *tm_info=localtime(&t); char buf[256]; const char *fmt=n>1?a[1].sval:"%Y-%m-%d %H:%M:%S"; strftime(buf,sizeof buf,fmt,tm_info); return val_str(buf); }
static Val bi_date_str(Val *a, int n) { time_t t=time(NULL); struct tm *tm_info=localtime(&t); char buf[64]; strftime(buf,sizeof buf,"%Y-%m-%d",tm_info); return val_str(buf); }
static Val bi_getpid(Val *a, int n) { return val_int((long long)getpid()); }
static Val bi_hostname(Val *a, int n) { char buf[256]; if(gethostname(buf,256)==0) return val_str(buf); return val_str("localhost"); }
static Val bi_username(Val *a, int n) { char *u=getenv("USER"); if(!u) u=getenv("LOGNAME"); if(!u) u=getenv("USERNAME"); return val_str(u?u:"unknown"); }
static Val bi_home_dir(Val *a, int n) { char *h=getenv("HOME"); return val_str(h?h:"/tmp"); }
static Val bi_temp_dir(Val *a, int n) { char *t=getenv("TMPDIR"); if(!t) t=getenv("TEMP"); if(!t) t=getenv("TMP"); if(!t) t="/tmp"; return val_str(t); }
static Val bi_read_bytes(Val *a, int n) { FILE *f=fopen(a[0].sval,"rb"); if(!f) return val_list(); fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET); Val r=val_list(); unsigned char buf[4096]; size_t nr; while((nr=fread(buf,1,4096,f))>0){for(size_t i=0;i<nr;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_int(buf[i]);}} fclose(f); return r; }
static Val bi_write_bytes(Val *a, int n) { FILE *f=fopen(a[0].sval,"wb"); if(!f) return val_bool(0); if(a[1].type==V_LIST){for(int i=0;i<a[1].llen;i++){unsigned char c=(unsigned char)(a[1].li[i].type==V_INT?a[1].li[i].ival:0);fwrite(&c,1,1,f);}}else if(a[1].type==V_BYTES){fwrite(a[1].sval,1,a[1].slen,f);} fclose(f); return val_bool(1); }
static Val bi_assert_fn(Val *a, int n) { if(!val_truthy(a[0])){fprintf(stderr,"Loi: assert that bai\n");if(n>1){char *s=val_to_str(a[1]);fprintf(stderr,"  %s\n",s);free(s);}mnos_error_flag=1;strncpy(mnos_error_msg,"assert failed",sizeof(mnos_error_msg)-1);} return val_none(); }
static Val bi_print_fn(Val *a, int n) { for(int i=0;i<n;i++){if(i>0) printf(" ");char *s=val_to_str(a[i]);printf("%s",s);free(s);} printf("\n"); fflush(stdout); return val_none(); }
static Val bi_print_sep(Val *a, int n) { char *sep=n>0?a[0].sval:" "; Val *items=n>1?a+1:a; int ni=n>1?n-1:0; for(int i=0;i<ni;i++){if(i>0) printf("%s",sep);char *s=val_to_str(items[i]);printf("%s",s);free(s);} printf("\n"); fflush(stdout); return val_none(); }
static Val bi_chr_utf8(Val *a, int n) { unsigned int cp=(unsigned int)a[0].ival; char buf[5]; if(cp<0x80){buf[0]=cp;buf[1]=0;}else if(cp<0x800){buf[0]=0xC0|(cp>>6);buf[1]=0x80|(cp&0x3F);buf[2]=0;}else if(cp<0x10000){buf[0]=0xE0|(cp>>12);buf[1]=0x80|((cp>>6)&0x3F);buf[2]=0x80|(cp&0x3F);buf[3]=0;}else{buf[0]=0xF0|(cp>>18);buf[1]=0x80|((cp>>12)&0x3F);buf[2]=0x80|((cp>>6)&0x3F);buf[3]=0x80|(cp&0x3F);buf[4]=0;} Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=strdup(buf);v.slen=strlen(buf); return v; }
static Val bi_ord_utf8(Val *a, int n) { unsigned char *s=(unsigned char*)a[0].sval; if(!s[0]) return val_int(-1); if(s[0]<0x80) return val_int(s[0]); if((s[0]&0xE0)==0xC0&&a[0].slen>=2) return val_int(((s[0]&0x1F)<<6)|(s[1]&0x3F)); if((s[0]&0xF0)==0xE0&&a[0].slen>=3) return val_int(((s[0]&0x0F)<<12)|((s[1]&0x3F)<<6)|(s[2]&0x3F)); if((s[0]&0xF8)==0xF0&&a[0].slen>=4) return val_int(((s[0]&0x07)<<18)|((s[1]&0x3F)<<12)|((s[2]&0x3F)<<6)|(s[3]&0x3F)); return val_int(s[0]); }
static Val bi_str_hash(Val *a, int n) { uint32_t h=5381; for(int i=0;i<a[0].slen;i++) h=((h<<5)+h)+(unsigned char)a[0].sval[i]; return val_int((long long)(h&0x7FFFFFFF)); }
static Val bi_to_str(Val *a, int n) { char *s=val_to_str(a[0]); Val r=val_str(s); free(s); return r; }
static Val bi_to_int(Val *a, int n) { if(a[0].type==V_INT) return val_int(a[0].ival); if(a[0].type==V_FLOAT) return val_int((long long)a[0].fval); if(a[0].type==V_STR) return val_int(atoll(a[0].sval)); if(a[0].type==V_BOOL) return val_int(a[0].bval?1:0); return val_int(0); }
static Val bi_to_float(Val *a, int n) { if(a[0].type==V_FLOAT) return val_flt(a[0].fval); if(a[0].type==V_INT) return val_flt((double)a[0].ival); if(a[0].type==V_STR) return val_flt(atof(a[0].sval)); return val_flt(0.0); }
static Val bi_gc_collect(Val *a, int n) { return val_none(); }
static Val bi_exec_fn(Val *a, int n) { mnos_run_source(a[0].sval,"<exec>"); return val_none(); }
static Val bi_clamp(Val *a, int n) { if(a[0].type==V_INT){long long v=a[0].ival,lo=a[1].ival,hi=a[2].ival;if(v<lo) v=lo;if(v>hi) v=hi;return val_int(v);} double v2=a[0].type==V_INT?(double)a[0].ival:a[0].fval; double lo2=a[1].type==V_INT?(double)a[1].ival:a[1].fval; double hi2=a[2].type==V_INT?(double)a[2].ival:a[2].fval; if(v2<lo2) v2=lo2; if(v2>hi2) v2=hi2; return val_flt(v2); }
static Val bi_map_val(Val *a, int n) { if(a[0].type!=V_DICT) return val_none(); return val_copy(a[0]); }
static Val bi_mkdirs(Val *a, int n) { char *p=strdup(a[0].sval); for(char *s=p+1;*s;s++){if(*s=='/'){*s=0;mkdir(p,0755);*s='/';}} mkdir(p,0755); free(p); return val_bool(1); }
static Val bi_glob_fn(Val *a, int n) { Val r=val_list(); char cmd[8192]; snprintf(cmd,8192,"find %s -maxdepth 1 -name '%s' 2>/dev/null",n>1?a[1].sval:".",a[0].sval); FILE *p2=popen(cmd,"r"); if(!p2) return r; char buf[4096]; while(fgets(buf,sizeof buf,p2)){int len=strlen(buf);while(len>0&&(buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]=0;if(len>0){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=val_str(buf);}} pclose(p2); return r; }
static Val bi_env_all(Val *a, int n) { Val r=val_dict(); extern char **environ; for(int i=0;environ[i];i++){char *eq=strchr(environ[i],'=');if(eq){int klen=eq-environ[i];char *key=malloc(klen+1);memcpy(key,environ[i],klen);key[klen]=0;if(r.dlen>=r.dcap){r.dcap*=2;r.dkeys=realloc(r.dkeys,sizeof(char*)*r.dcap);r.dvals=realloc(r.dvals,sizeof(Val)*r.dcap);}r.dkeys[r.dlen]=key;r.dvals[r.dlen]=val_str(eq+1);r.dlen++;}} return r; }

void register_builtins(Env *global) {
    reg_builtin("str",bi_str); reg_builtin("int",bi_int_fn); reg_builtin("float",bi_float_fn);
    reg_builtin("type_of",bi_type_of); reg_builtin("len",bi_len);
    reg_builtin("upper",bi_upper); reg_builtin("lower",bi_lower); reg_builtin("trim",bi_trim);
    reg_builtin("lstrip",bi_lstrip); reg_builtin("rstrip",bi_rstrip); reg_builtin("strip",bi_strip);
    reg_builtin("substr",bi_substr); reg_builtin("replace",bi_replace); reg_builtin("contains",bi_contains);
    reg_builtin("starts_with",bi_starts_with); reg_builtin("ends_with",bi_ends_with);
    reg_builtin("char_at",bi_char_at); reg_builtin("repeat",bi_repeat); reg_builtin("join",bi_join);
    reg_builtin("split",bi_split); reg_builtin("count",bi_count_sub); reg_builtin("find",bi_find);
    reg_builtin("title",bi_title); reg_builtin("capitalize",bi_capitalize);
    reg_builtin("isdigit",bi_isdigit); reg_builtin("isalpha",bi_isalpha); reg_builtin("isalnum",bi_isalnum);
    reg_builtin("ord",bi_ord); reg_builtin("chr",bi_chr); reg_builtin("format",bi_format);
    reg_builtin("range",bi_range);
    reg_builtin("sort",bi_sort); reg_builtin("reverse",bi_reverse); reg_builtin("slice",bi_slice);
    reg_builtin("append",bi_append); reg_builtin("extend",bi_extend);
    reg_builtin("index_of",bi_index_of); reg_builtin("has",bi_has);
    reg_builtin("cos",bi_cos); reg_builtin("sin",bi_sin); reg_builtin("tan",bi_tan); reg_builtin("pi",bi_pi);
    reg_builtin("sqrt",bi_sqrt); reg_builtin("floor",bi_floor); reg_builtin("ceil",bi_ceil);
    reg_builtin("abs",bi_abs); reg_builtin("round",bi_round); reg_builtin("log",bi_log);
    reg_builtin("log10",bi_log10); reg_builtin("pow",bi_pow_fn);
    reg_builtin("atan",bi_atan); reg_builtin("asin",bi_asin); reg_builtin("acos",bi_acos);
    reg_builtin("min",bi_min); reg_builtin("max",bi_max);
    reg_builtin("rand_int",bi_rand_int); reg_builtin("rand_float",bi_rand_float);
    reg_builtin("uniform",bi_uniform); reg_builtin("rand_bool",bi_rand_bool);
    reg_builtin("choice",bi_choice); reg_builtin("shuffle",bi_shuffle); reg_builtin("dice",bi_dice);
    reg_builtin("rand_color",bi_rand_color); reg_builtin("rand_string",bi_rand_string);
    reg_builtin("weighted_pick",bi_weighted_pick); reg_builtin("sample",bi_sample);
    reg_builtin("time_now",bi_time_now); reg_builtin("sleep",bi_sleep);
    reg_builtin("read_file",bi_read_file); reg_builtin("write_file",bi_write_file);
    reg_builtin("append_file",bi_append_file); reg_builtin("file_exists",bi_file_exists);
    reg_builtin("path_exists",bi_path_exists); reg_builtin("is_dir",bi_is_dir); reg_builtin("is_file",bi_is_file);
    reg_builtin("file_size",bi_file_size); reg_builtin("delete_file",bi_delete_file);
    reg_builtin("make_dir",bi_make_dir); reg_builtin("copy_file",bi_copy_file);
    reg_builtin("move_file",bi_move_file); reg_builtin("list_dir",bi_list_dir);
    reg_builtin("read_lines",bi_read_lines);
    reg_builtin("current_dir",bi_current_dir); reg_builtin("change_dir",bi_change_dir);
    reg_builtin("parent_dir",bi_parent_dir); reg_builtin("abs_path",bi_abs_path);
    reg_builtin("path_join",bi_path_join); reg_builtin("path_name",bi_path_name);
    reg_builtin("path_ext",bi_path_ext); reg_builtin("path_base",bi_path_base);
    reg_builtin("system",bi_system); reg_builtin("shell",bi_shell); reg_builtin("which",bi_which);
    reg_builtin("env_get",bi_env_get); reg_builtin("set_env",bi_set_env);
    reg_builtin("input",bi_input); reg_builtin("exit",bi_exit_fn);
    reg_builtin("version",bi_version); reg_builtin("args",bi_args_fn);
    reg_builtin("hide_cursor",bi_hide_cursor); reg_builtin("show_cursor",bi_show_cursor);
    reg_builtin("clear_screen",bi_clear_screen); reg_builtin("term_width",bi_term_width);
    reg_builtin("term_height",bi_term_height); reg_builtin("move_cursor",bi_move_cursor);
    reg_builtin("color",bi_color); reg_builtin("bold_color",bi_bold_color);
    reg_builtin("reset_color",bi_reset_color); reg_builtin("flush",bi_flush); reg_builtin("write",bi_write);
    reg_builtin("mnos_dump",bi_mnos_dump); reg_builtin("mnos_load",bi_mnos_load);
    reg_builtin("mnos_pack",bi_mnos_pack); reg_builtin("mnos_unpack",bi_mnos_unpack);
    reg_builtin("mnos_version",bi_mnos_version);
    reg_builtin("dict_get",bi_dict_get); reg_builtin("dict_set",bi_dict_set);
    reg_builtin("dict_has",bi_dict_has); reg_builtin("dict_keys",bi_dict_keys);
    reg_builtin("dict_vals",bi_dict_vals); reg_builtin("dict_remove",bi_dict_remove);
    reg_builtin("dict_len",bi_dict_len); reg_builtin("dict_merge",bi_dict_merge);
    reg_builtin("dict_items",bi_dict_items); reg_builtin("dict_clear",bi_dict_clear);
    reg_builtin("dict_update",bi_dict_update); reg_builtin("dict_pop",bi_dict_pop);
    reg_builtin("dict_copy",bi_dict_copy);
    reg_builtin("list_insert",bi_list_insert); reg_builtin("list_remove",bi_list_remove);
    reg_builtin("list_pop",bi_list_pop); reg_builtin("list_count",bi_list_count);
    reg_builtin("list_flatten",bi_list_flatten); reg_builtin("list_copy",bi_list_copy);
    reg_builtin("list_clear",bi_list_clear);
    reg_builtin("enumerate",bi_enumerate); reg_builtin("zip",bi_zip);
    reg_builtin("any",bi_any_fn); reg_builtin("all",bi_all_fn);
    reg_builtin("sum",bi_sum_fn); reg_builtin("sorted",bi_sorted_fn);
    reg_builtin("reversed",bi_reversed_fn);
    reg_builtin("hex",bi_hex); reg_builtin("oct",bi_oct); reg_builtin("bin",bi_bin);
    reg_builtin("repr",bi_repr_fn); reg_builtin("isinstance",bi_isinstance);
    reg_builtin("bool",bi_bool_fn); reg_builtin("tuple",bi_tuple_fn);
    reg_builtin("list",bi_list_fn); reg_builtin("dict",bi_dict_fn);
    reg_builtin("val_none",bi_val_none_fn);
    reg_builtin("exp",bi_exp); reg_builtin("degrees",bi_degrees);
    reg_builtin("radians",bi_radians); reg_builtin("gcd",bi_gcd_fn);
    reg_builtin("modf",bi_modf_fn); reg_builtin("trunc",bi_trunc);
    reg_builtin("copysign",bi_copysign); reg_builtin("fmod",bi_fmod_fn);
    reg_builtin("hypot",bi_hypot_fn); reg_builtin("atan2",bi_atan2_fn);
    reg_builtin("inf",bi_inf_fn); reg_builtin("nan",bi_nan_fn);
    reg_builtin("e",bi_e_fn); reg_builtin("log2",bi_log2_fn);
    reg_builtin("isnan",bi_isnan); reg_builtin("isinf",bi_isinf);
    reg_builtin("str_ljust",bi_str_ljust); reg_builtin("str_rjust",bi_str_rjust);
    reg_builtin("str_center",bi_str_center); reg_builtin("str_zfill",bi_str_zfill);
    reg_builtin("str_partition",bi_str_partition); reg_builtin("str_rfind",bi_str_rfind);
    reg_builtin("str_rsplit",bi_str_rsplit); reg_builtin("str_splitlines",bi_str_splitlines);
    reg_builtin("str_swapcase",bi_str_swapcase); reg_builtin("str_isupper",bi_str_isupper);
    reg_builtin("str_islower",bi_str_islower); reg_builtin("str_isspace",bi_str_isspace);
    reg_builtin("str_isnumeric",bi_str_isnumeric); reg_builtin("str_lpad",bi_str_lpad);
    reg_builtin("str_rpad",bi_str_rpad); reg_builtin("str_just",bi_str_just);
    reg_builtin("str_escape",bi_str_escape); reg_builtin("str_unescape",bi_str_unescape);
    reg_builtin("str_dedent",bi_str_dedent); reg_builtin("str_indent",bi_str_indent);
    reg_builtin("str_word_count",bi_str_word_count); reg_builtin("str_lines",bi_str_lines);
    reg_builtin("str_bytes_len",bi_str_bytes_len); reg_builtin("str_to_bytes",bi_str_to_bytes);
    reg_builtin("str_from_bytes",bi_str_from_bytes);
    reg_builtin("time_ms",bi_time_ms); reg_builtin("time_us",bi_time_us);
    reg_builtin("time_format",bi_time_format); reg_builtin("date_str",bi_date_str);
    reg_builtin("getpid",bi_getpid); reg_builtin("hostname",bi_hostname);
    reg_builtin("username",bi_username); reg_builtin("home_dir",bi_home_dir);
    reg_builtin("temp_dir",bi_temp_dir);
    reg_builtin("read_bytes",bi_read_bytes); reg_builtin("write_bytes",bi_write_bytes);
    reg_builtin("assert",bi_assert_fn); reg_builtin("print",bi_print_fn);
    reg_builtin("print_sep",bi_print_sep); reg_builtin("chr_utf8",bi_chr_utf8);
    reg_builtin("ord_utf8",bi_ord_utf8); reg_builtin("str_hash",bi_str_hash);
    reg_builtin("to_str",bi_to_str); reg_builtin("to_int",bi_to_int);
    reg_builtin("to_float",bi_to_float); reg_builtin("gc_collect",bi_gc_collect);
    reg_builtin("exec",bi_exec_fn); reg_builtin("clamp",bi_clamp);
    reg_builtin("mkdirs",bi_mkdirs); reg_builtin("glob",bi_glob_fn);
    reg_builtin("env_all",bi_env_all);
}

Val eval_node(Node *nd, Env *env);
static void collect_crafts(Node *p) { for(Node *n=p;n;n=n->next) if(n->type==N_CRAFT){if(fn_count>=MAX_FNS) return;fn_names[fn_count]=strdup(n->name);fn_bodies[fn_count]=n->body;fn_nparams[fn_count]=n->nparams;fn_params[fn_count]=malloc(sizeof(char*)*n->nparams);for(int i=0;i<n->nparams;i++) fn_params[fn_count][i]=strdup(n->params[i]);fn_count++;} }

ClassDef *register_class_def(Node *nd, Env *env) {
    ClassDef *cd=calloc(1,sizeof(ClassDef)); strncpy(cd->name,nd->name,127);
    cd->prop_cap=8; cd->prop_names=malloc(sizeof(char*)*8); cd->prop_defaults=malloc(sizeof(Val)*8);
    cd->method_cap=8; cd->method_names=malloc(sizeof(char*)*8); cd->method_bodies=malloc(sizeof(Node*)*8);
    cd->method_params=malloc(sizeof(char**)*8); cd->method_nparams=malloc(sizeof(int)*8);
    for(Node *s=nd->body;s;s=s->next){
        if(s->type==N_HOLD){if(cd->nprop>=cd->prop_cap){cd->prop_cap*=2;cd->prop_names=realloc(cd->prop_names,sizeof(char*)*cd->prop_cap);cd->prop_defaults=realloc(cd->prop_defaults,sizeof(Val)*cd->prop_cap);}cd->prop_names[cd->nprop]=strdup(s->name);cd->prop_defaults[cd->nprop]=eval_node(s->left,env);cd->nprop++;}
        else if(s->type==N_CRAFT){if(cd->nmethod>=cd->method_cap){cd->method_cap*=2;cd->method_names=realloc(cd->method_names,sizeof(char*)*cd->method_cap);cd->method_bodies=realloc(cd->method_bodies,sizeof(Node*)*cd->method_cap);cd->method_params=realloc(cd->method_params,sizeof(char**)*cd->method_cap);cd->method_nparams=realloc(cd->method_nparams,sizeof(int)*cd->method_cap);}cd->method_names[cd->nmethod]=strdup(s->name);cd->method_bodies[cd->nmethod]=s->body;cd->method_nparams[cd->nmethod]=s->nparams;cd->method_params[cd->nmethod]=malloc(sizeof(char*)*s->nparams);for(int i=0;i<s->nparams;i++) cd->method_params[cd->nmethod][i]=strdup(s->params[i]);cd->nmethod++;}
    }
    if(cls_count<MAX_CLS) cls_defs[cls_count++]=cd;
    Val cv; memset(&cv,0,sizeof cv); cv.type=V_CLASS; cv.cls=cd;
    env_set(env,cd->name,cv);
    return cd;
}

Val create_instance(ClassDef *cls, Env *env) {
    ObjInst *inst=calloc(1,sizeof(ObjInst)); inst->cls=cls; inst->nprops=cls->nprop;
    inst->props=malloc(sizeof(Val)*cls->nprop);
    for(int i=0;i<cls->nprop;i++) inst->props[i]=val_copy(cls->prop_defaults[i]);
    Val v; memset(&v,0,sizeof v); v.type=V_OBJ; v.obj=inst; return v;
}

Val call_method(ObjInst *obj, const char *method, Val *args, int nargs, Env *env) {
    ClassDef *cls=obj->cls;
    for(int i=0;i<cls->nmethod;i++){
        if(strcmp(cls->method_names[i],method)==0){
            Env local; env_init(&local,global_env);
            Val thisv; memset(&thisv,0,sizeof thisv); thisv.type=V_OBJ; thisv.obj=obj;
            env_set(&local,"this",thisv);
            for(int j=0;j<nargs&&j<cls->method_nparams[i];j++) env_set(&local,cls->method_params[i][j],val_copy(args[j]));
            ret_flag=0;
            for(Node *s=cls->method_bodies[i];s;s=s->next){eval_node(s,&local);if(ret_flag) break;}
            Val r=ret_flag?ret_val:val_none(); env_free(&local); return r;
        }
    }
    return val_none();
}

Val call_function(const char *name, Val *args, int nargs, Env *env) {
    BuiltinFn bf=find_builtin(name); if(bf) return bf(args,nargs);
    for(int i=0;i<fn_count;i++){
        if(strcmp(fn_names[i],name)==0){
            Env local; env_init(&local,global_env);
            for(int j=0;j<nargs&&j<fn_nparams[i];j++) env_set(&local,fn_params[i][j],val_copy(args[j]));
            ret_flag=0;
            for(Node *s=fn_bodies[i];s;s=s->next){eval_node(s,&local);if(ret_flag) break;}
            Val r=ret_flag?ret_val:val_none(); env_free(&local); return r;
        }
    }
    for(int i=0;i<cls_count;i++) if(strcmp(cls_defs[i]->name,name)==0) return create_instance(cls_defs[i],env);
    return val_none();
}

static void do_import_file(const char *path, Env *env) {
    FILE *f=fopen(path,"r"); if(!f) return;
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *src=malloc(sz+1); fread(src,1,sz,f); src[sz]=0; fclose(f);
    char old_dir[4096]; strncpy(old_dir,current_file_dir,4095);
    char *sl=strrchr(path,'/');
    if(sl){int len=sl-path; if(len>0&&len<4095){memcpy(current_file_dir,path,len);current_file_dir[len]=0;}}
    int nt; Token *tks=lex(src,&nt);
    Node *ast=parse(tks,nt);
    for(Node *n=ast;n;n=n->next){if(n->type==N_CRAFT) collect_crafts(n);if(n->type==N_FORM) register_class_def(n,env);}
    for(Node *n=ast;n;n=n->next) eval_node(n,env);
    free(tks); free(src);
    strncpy(current_file_dir,old_dir,4095); current_file_dir[4095]=0;
}

static int try_import(const char *path, Env *env) {
    FILE *f=fopen(path,"r"); if(!f) return 0; fclose(f);
    do_import_file(path,env); return 1;
}

static void do_import(const char *name, Env *env) {
    const char *home=getenv("HOME"); if(!home) home="/root";
    const char *prefix=getenv("PREFIX");
    char buf[4096];
    snprintf(buf,4096,"%s/%s.mno",current_file_dir,name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"%s/%s/init.mno",current_file_dir,name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"%s/lib/%s.mno",current_file_dir,name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"%s/lib/%s/init.mno",current_file_dir,name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"%s/.manios/lib/%s.mno",home,name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"%s/.manios/lib/%s/init.mno",home,name);
    if(try_import(buf,env)) return;
    if(prefix){
        snprintf(buf,4096,"%s/share/manios/lib/%s.mno",prefix,name);
        if(try_import(buf,env)) return;
        snprintf(buf,4096,"%s/share/manios/lib/%s/init.mno",prefix,name);
        if(try_import(buf,env)) return;
    }
    snprintf(buf,4096,"/usr/local/share/manios/lib/%s.mno",name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"/usr/local/share/manios/lib/%s/init.mno",name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"/usr/share/manios/lib/%s.mno",name);
    if(try_import(buf,env)) return;
    snprintf(buf,4096,"/usr/share/manios/lib/%s/init.mno",name);
    if(try_import(buf,env)) return;
    if(strchr(name,'/')){
        snprintf(buf,4096,"%s/%s.mno",current_file_dir,name);
        if(try_import(buf,env)) return;
    }
    fprintf(stderr,"Khong tim thay thu vien: %s\n",name);
}

Val eval_node(Node *nd, Env *env) {
    if(!nd) return val_none();
    switch(nd->type){
    case N_INT_LIT: return val_int(nd->lit_val.ival);
    case N_FLT_LIT: return val_flt(nd->lit_val.fval);
    case N_STR_LIT: return val_str(nd->lit_val.sval);
    case N_BOOL_LIT: return val_bool(nd->lit_val.bval);
    case N_NONE_LIT: return val_none();
    case N_THIS: return env_get(env,"this");
    case N_IDENT: if(env_has(env,nd->name)) return env_get(env,nd->name); return val_none();
    case N_BINOP: { Val l=eval_node(nd->left,env); Val r=eval_node(nd->right,env); Val res=val_none(); switch(nd->op){case T_PLUS:res=val_add(l,r);break;case T_MINUS:res=val_sub(l,r);break;case T_STAR:res=val_mul(l,r);break;case T_SLASH:res=val_div(l,r);break;case T_PERCENT:res=val_mod(l,r);break;case T_CARET:res=val_pow(l,r);break;default:res=val_cmp(l,r,nd->op);break;} val_free(&l);val_free(&r);return res; }
    case N_UNOP: { Val v=eval_node(nd->left,env); Val r=(nd->op==T_MINUS)?val_neg(v):val_not(v); val_free(&v); return r; }
    case N_NOT: { Val v=eval_node(nd->left,env); Val r=val_not(v); val_free(&v); return r; }
    case N_AND: { Val l=eval_node(nd->left,env); if(!val_truthy(l)){val_free(&l);return val_bool(0);} val_free(&l); Val r=eval_node(nd->right,env); int t=val_truthy(r); val_free(&r); return val_bool(t); }
    case N_HOLD: { Val v=eval_node(nd->left,env); env_set(env,nd->name,v); return v; }
    case N_SET: { if(strchr(nd->name,'.')){char buf[256];strncpy(buf,nd->name,255);char *dot=strrchr(buf,'.');*dot=0;char *field=dot+1;Val *objv_ref=env_get_ref(env,buf);if(objv_ref&&objv_ref->type==V_OBJ){ObjInst *o=(ObjInst*)objv_ref->obj;for(int i=0;i<o->cls->nprop;i++){if(strcmp(o->cls->prop_names[i],field)==0){val_free(&o->props[i]);o->props[i]=eval_node(nd->left,env);return o->props[i];}}} return val_none();} if(nd->right){Val *lst_ref=env_get_ref(env,nd->name);if(lst_ref){Val lst=val_copy(*lst_ref);Val idx=eval_node(nd->right,env);Val val=eval_node(nd->left,env);if(lst.type==V_LIST){int i=(int)idx.ival;if(i<0)i=lst.llen+i;if(i>=0&&i<lst.llen){val_free(&lst.li[i]);lst.li[i]=val;}}else if(lst.type==V_DICT){char *key=val_to_str(idx);int found=0;for(int i=0;i<lst.dlen;i++){if(strcmp(lst.dkeys[i],key)==0){val_free(&lst.dvals[i]);lst.dvals[i]=val;found=1;break;}}if(!found){if(lst.dlen>=lst.dcap){lst.dcap*=2;lst.dkeys=realloc(lst.dkeys,sizeof(char*)*lst.dcap);lst.dvals=realloc(lst.dvals,sizeof(Val)*lst.dcap);}lst.dkeys[lst.dlen]=key;lst.dvals[lst.dlen]=val;lst.dlen++;}else free(key);}val_free(lst_ref);*lst_ref=lst;return val;}else{Val lst=env_get(env,nd->name);Val idx=eval_node(nd->right,env);Val val=eval_node(nd->left,env);if(lst.type==V_LIST){int i=(int)idx.ival;if(i<0)i=lst.llen+i;if(i>=0&&i<lst.llen){val_free(&lst.li[i]);lst.li[i]=val;}}else if(lst.type==V_DICT){char *key=val_to_str(idx);int found=0;for(int i=0;i<lst.dlen;i++){if(strcmp(lst.dkeys[i],key)==0){val_free(&lst.dvals[i]);lst.dvals[i]=val;found=1;break;}}if(!found){if(lst.dlen>=lst.dcap){lst.dcap*=2;lst.dkeys=realloc(lst.dkeys,sizeof(char*)*lst.dcap);lst.dvals=realloc(lst.dvals,sizeof(Val)*lst.dcap);}lst.dkeys[lst.dlen]=key;lst.dvals[lst.dlen]=val;lst.dlen++;}else free(key);}env_set(env,nd->name,lst);return val;}} Val v=eval_node(nd->left,env);env_set(env,nd->name,v);return v; }
    case N_ASSIGN: { Val v=eval_node(nd->left,env); env_set(env,nd->name,v); return v; }
    case N_INPLACE_ADD: { Val c=env_get(env,nd->name); Val a=eval_node(nd->left,env); Val r=val_add(c,a); val_free(&c);val_free(&a);env_set(env,nd->name,r);return r; }
    case N_INPLACE_SUB: { Val c=env_get(env,nd->name); Val a=eval_node(nd->left,env); Val r=val_sub(c,a); val_free(&c);val_free(&a);env_set(env,nd->name,r);return r; }
    case N_INPLACE_MUL: { Val c=env_get(env,nd->name); Val a=eval_node(nd->left,env); Val r=val_mul(c,a); val_free(&c);val_free(&a);env_set(env,nd->name,r);return r; }
    case N_INPLACE_DIV: { Val c=env_get(env,nd->name); Val a=eval_node(nd->left,env); Val r=val_div(c,a); val_free(&c);val_free(&a);env_set(env,nd->name,r);return r; }
    case N_YELL: { Val v=eval_node(nd->left,env); char *s=val_to_str(v); printf("%s\n",s); fflush(stdout); free(s);val_free(&v);return val_none(); }
    case N_YELLN: { Val v=eval_node(nd->left,env); char *s=val_to_str(v); printf("%s",s); fflush(stdout); free(s);val_free(&v);return val_none(); }
    case N_CHECK: { Val c=eval_node(nd->cond,env); if(val_truthy(c)){val_free(&c);for(Node *s=nd->body;s;s=s->next) eval_node(s,env);}else{val_free(&c);if(nd->elif_body) eval_node(nd->elif_body,env);} return val_none(); }
    case N_SPIN: { Val cv=eval_node(nd->cond,env); int cnt=(int)cv.ival; val_free(&cv); for(int i=0;i<cnt;i++) for(Node *s=nd->body;s;s=s->next) eval_node(s,env); return val_none(); }
    case N_SPIN_RANGE: { Val sv=eval_node(nd->cond,env); Val ev=eval_node(nd->right,env); long long start=sv.ival,end=ev.ival; val_free(&sv);val_free(&ev); for(long long i=start;i<end;i++){env_set(env,nd->name,val_int(i));for(Node *s=nd->body;s;s=s->next) eval_node(s,env);} return val_none(); }
    case N_SPIN_WHILE: { while(1){Val c=eval_node(nd->cond,env);if(!val_truthy(c)){val_free(&c);break;}val_free(&c);for(Node *s=nd->body;s;s=s->next) eval_node(s,env);} return val_none(); }
    case N_CRAFT: { if(fn_count>=MAX_FNS) return val_none(); fn_names[fn_count]=strdup(nd->name);fn_bodies[fn_count]=nd->body;fn_nparams[fn_count]=nd->nparams;fn_params[fn_count]=malloc(sizeof(char*)*nd->nparams);for(int i=0;i<nd->nparams;i++) fn_params[fn_count][i]=strdup(nd->params[i]);fn_count++;return val_none(); }
    case N_GIVE: { ret_flag=1;ret_val=eval_node(nd->left,env);return ret_val; }
    case N_FORM: { return val_none(); }
    case N_INP: { do_import(nd->name,env);return val_none(); }
    case N_GRAB: { char buf[65536];buf[0]=0;if(fgets(buf,sizeof buf,stdin)){int len=strlen(buf);while(len>0&&(buf[len-1]=='\n'||buf[len-1]=='\r')) buf[--len]=0;} Val v;memset(&v,0,sizeof v);v.type=V_STR;v.sval=strdup(buf);v.slen=strlen(buf);env_set(env,nd->name,v);return v; }
    case N_PUSH: { Val lst=env_get(env,nd->name); Val val=eval_node(nd->left,env); if(lst.type==V_LIST){if(lst.llen>=lst.lcap){lst.lcap*=2;lst.li=realloc(lst.li,sizeof(Val)*lst.lcap);}lst.li[lst.llen++]=val;env_set(env,nd->name,lst);} return val_none(); }
    case N_POP: { Val lst=env_get(env,nd->name); Val r=val_none(); if(lst.type==V_LIST&&lst.llen>0){r=val_copy(lst.li[lst.llen-1]);val_free(&lst.li[lst.llen-1]);lst.llen--;env_set(env,nd->name,lst);} return r; }
    case N_HALT: exit(0);
    case N_LIST_LIT: { Val r=val_list(); for(int i=0;i<nd->nitems;i++){if(r.llen>=r.lcap){r.lcap*=2;r.li=realloc(r.li,sizeof(Val)*r.lcap);}r.li[r.llen++]=eval_node(nd->items[i],env);} return r; }
    case N_DICT_LIT: { Val r=val_dict(); for(int i=0;i<nd->nitems/2;i++){Val kv=eval_node(nd->items[i*2],env);Val vv=eval_node(nd->items[i*2+1],env);if(r.dlen>=r.dcap){r.dcap*=2;r.dkeys=realloc(r.dkeys,sizeof(char*)*r.dcap);r.dvals=realloc(r.dvals,sizeof(Val)*r.dcap);}r.dkeys[r.dlen]=strdup(kv.sval);r.dvals[r.dlen]=val_copy(vv);r.dlen++;val_free(&kv);val_free(&vv);} return r; }
    case N_TUPLE_LIT: { Val items[64]; int n=nd->nitems; if(n>64) n=64; for(int i=0;i<n;i++) items[i]=eval_node(nd->items[i],env); Val r=val_tuple(items,n); for(int i=0;i<n;i++) val_free(&items[i]); return r; }
    case N_INDEX: { Val base=eval_node(nd->left,env); Val idx=eval_node(nd->right,env); Val r=val_none(); if(base.type==V_LIST){int i=(int)idx.ival;if(i<0)i=base.llen+i;if(i>=0&&i<base.llen) r=val_copy(base.li[i]);}else if(base.type==V_STR){int i=(int)idx.ival;if(i<0)i=base.slen+i;if(i>=0&&i<base.slen){char c[2]={base.sval[i],0};r=val_str(c);}}else if(base.type==V_DICT){char *key=val_to_str(idx);for(int i=0;i<base.dlen;i++){if(strcmp(base.dkeys[i],key)==0){r=val_copy(base.dvals[i]);break;}}free(key);}else if(base.type==V_TUPLE){int i=(int)idx.ival;if(i<0)i=base.tlen+i;if(i>=0&&i<base.tlen) r=val_copy(base.tu[i]);} val_free(&base);val_free(&idx);return r; }
    case N_DOT: { Val base=eval_node(nd->left,env); Val r=val_none(); if(base.type==V_OBJ){ObjInst *o=(ObjInst*)base.obj;for(int i=0;i<o->cls->nprop;i++){if(strcmp(o->cls->prop_names[i],nd->name)==0){r=val_copy(o->props[i]);break;}}}else if(base.type==V_STR&&strcmp(nd->name,"len")==0) r=val_int(base.slen); else if(base.type==V_LIST&&strcmp(nd->name,"len")==0) r=val_int(base.llen); else if(base.type==V_DICT&&strcmp(nd->name,"len")==0) r=val_int(base.dlen); val_free(&base);return r; }
    case N_CALL: { Val fnv=eval_node(nd->left,env); Val args[64]; int na=nd->nitems; if(na>64) na=64; for(int i=0;i<na;i++) args[i]=eval_node(nd->items[i],env); if(fnv.type==V_CLASS){Val r=create_instance((ClassDef*)fnv.cls,env);val_free(&fnv);for(int i=0;i<na;i++) val_free(&args[i]);return r;} if(nd->left->type==N_DOT){char fname[256]={0};if(nd->left->left->type==N_IDENT) strncpy(fname,nd->left->left->name,255); else if(nd->left->left->type==N_THIS) strncpy(fname,"this",255); if(strcmp(fname,"this")==0||env_get(env,fname).type==V_OBJ){Val objv=env_get(env,fname);if(objv.type==V_OBJ){ObjInst *o=(ObjInst*)objv.obj;Val r=call_method(o,nd->left->name,args,na,env);val_free(&objv);val_free(&fnv);for(int i=0;i<na;i++) val_free(&args[i]);return r;}}} if(nd->left->type==N_IDENT){const char *nm=nd->left->name;BuiltinFn bf=find_builtin(nm);if(bf){Val r=bf(args,na);val_free(&fnv);for(int i=0;i<na;i++) val_free(&args[i]);return r;}for(int i=0;i<fn_count;i++){if(strcmp(fn_names[i],nm)==0){Env local;env_init(&local,global_env);for(int j=0;j<na&&j<fn_nparams[i];j++) env_set(&local,fn_params[i][j],val_copy(args[j]));ret_flag=0;for(Node *s=fn_bodies[i];s;s=s->next){eval_node(s,&local);if(ret_flag) break;}Val r=ret_flag?ret_val:val_none();env_free(&local);val_free(&fnv);for(int j=0;j<na;j++) val_free(&args[j]);return r;}}for(int i=0;i<cls_count;i++){if(strcmp(cls_defs[i]->name,nm)==0){Val r=create_instance(cls_defs[i],env);val_free(&fnv);for(int j=0;j<na;j++) val_free(&args[j]);return r;}}} val_free(&fnv);for(int i=0;i<na;i++) val_free(&args[i]);return val_none(); }
    case N_SHIELD: { mnos_error_flag=0;mnos_error_msg[0]=0;for(Node *s=nd->body;s;s=s->next) eval_node(s,env);if(mnos_error_flag&&nd->elif_body){mnos_error_flag=0;if(nd->name[0]) env_set(env,nd->name,val_str(mnos_error_msg));for(Node *s=nd->elif_body;s;s=s->next) eval_node(s,env);}mnos_error_flag=0;return val_none(); }
    case N_RAISE: { Val v=eval_node(nd->left,env); char *s=val_to_str(v); fprintf(stderr,"Loi: %s\n",s); mnos_error_flag=1; strncpy(mnos_error_msg,s,sizeof(mnos_error_msg)-1); free(s);val_free(&v);return val_none(); }
    default: return val_none();
    }
}

int mnos_run_file(const char *path) {
    FILE *f=fopen(path,"r"); if(!f){fprintf(stderr,"Khong mo duoc file: %s\n",path);return 1;}
    fseek(f,0,SEEK_END); long sz=ftell(f); fseek(f,0,SEEK_SET);
    char *src=malloc(sz+1); fread(src,1,sz,f); src[sz]=0; fclose(f);
    char resolved[4096]; if(!realpath(path,resolved)) strncpy(resolved,path,4096);
    char *last=strrchr(resolved,'/');
    if(last){int len=last-resolved;strncpy(current_file_dir,resolved,len);current_file_dir[len]=0;}
    else getcwd(current_file_dir,sizeof current_file_dir);
    int nt; Token *tks=lex(src,&nt);
    if(nt<=1){free(src);free(tks);return 0;}
    Env global; env_init(&global,NULL); global_env=&global;
    g_args_val=val_list(); srand((unsigned int)time(NULL));
    register_builtins(&global);
    Node *ast=parse(tks,nt);
    for(Node *n=ast;n;n=n->next){if(n->type==N_CRAFT) collect_crafts(n);if(n->type==N_FORM) register_class_def(n,&global);}
    for(Node *n=ast;n;n=n->next) eval_node(n,&global);
    node_free(ast); free(tks); free(src);
    return 0;
}

int mnos_run_source(const char *src, const char *filename) {
    if(filename){char resolved[4096];strncpy(resolved,filename,4096);char *last=strrchr(resolved,'/');if(last){int len=last-resolved;strncpy(current_file_dir,resolved,len);current_file_dir[len]=0;}else getcwd(current_file_dir,sizeof current_file_dir);}
    else getcwd(current_file_dir,sizeof current_file_dir);
    int nt; Token *tks=lex(src,&nt);
    if(nt<=1){free(tks);return 0;}
    Env global; env_init(&global,NULL); global_env=&global;
    g_args_val=val_list(); srand((unsigned int)time(NULL));
    register_builtins(&global);
    Node *ast=parse(tks,nt);
    for(Node *n=ast;n;n=n->next){if(n->type==N_CRAFT) collect_crafts(n);if(n->type==N_FORM) register_class_def(n,&global);}
    for(Node *n=ast;n;n=n->next) eval_node(n,&global);
    node_free(ast); free(tks);
    return 0;
}