#include "mnos.h"

void env_init(Env *e, Env *parent) {
    e->count=0; e->cap=64; e->parent=parent;
    e->names=malloc(sizeof(char*)*64);
    e->vals=malloc(sizeof(Val)*64);
}

void env_set(Env *e, const char *name, Val v) {
    for(int i=0;i<e->count;i++) {
        if(strcmp(e->names[i],name)==0) { val_free(&e->vals[i]); e->vals[i]=v; return; }
    }
    if(e->count>=e->cap) { e->cap*=2; e->names=realloc(e->names,sizeof(char*)*e->cap); e->vals=realloc(e->vals,sizeof(Val)*e->cap); }
    e->names[e->count]=strdup(name);
    e->vals[e->count]=v;
    e->count++;
}

int env_has(Env *e, const char *name) {
    for(int i=0;i<e->count;i++) if(strcmp(e->names[i],name)==0) return 1;
    if(e->parent) return env_has(e->parent,name);
    return 0;
}

Val env_get(Env *e, const char *name) {
    for(int i=0;i<e->count;i++) if(strcmp(e->names[i],name)==0) return val_copy(e->vals[i]);
    if(e->parent) return env_get(e->parent,name);
    return val_none();
}

Val *env_get_ref(Env *e, const char *name) {
    for(int i=0;i<e->count;i++) if(strcmp(e->names[i],name)==0) return &e->vals[i];
    if(e->parent) return env_get_ref(e->parent,name);
    return NULL;
}

void env_free(Env *e) {
    for(int i=0;i<e->count;i++) { free(e->names[i]); val_free(&e->vals[i]); }
    free(e->names); free(e->vals);
    e->names=NULL; e->vals=NULL; e->count=0; e->cap=0;
}