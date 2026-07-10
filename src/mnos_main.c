#include "mnos.h"

int main(int argc, char **argv) {
    if(argc<2){
        printf("Manios v1.0.0\n");
        printf("Cach dung: manios <file.mno>\n");
        printf("           mnos <file.mno>\n");
        printf("           manios version\n");
        return 0;
    }
    if(strcmp(argv[1],"version")==0||strcmp(argv[1],"-v")==0||strcmp(argv[1],"--version")==0){
        printf("Manios v1.0.0\n");
        printf("MNOS bytecode engine: enabled\n");
        return 0;
    }
    if(strcmp(argv[1],"tool")==0){
        return mnos_tool_main(argc-2,argv+2);
    }
    return mnos_run_file(argv[1]);
}