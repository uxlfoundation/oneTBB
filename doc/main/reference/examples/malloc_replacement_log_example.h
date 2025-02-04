#include "oneapi/tbb/tbbmalloc_proxy.h"
#include <stdio.h>

int main(){
    char **func_replacement_log;
    int func_replacement_status = TBB_malloc_replacement_log(&func_replacement_log);

    if (func_replacement_status != 0) {
        printf("tbbmalloc_proxy cannot replace memory allocation routines\n");
        for (char** log_string = func_replacement_log; *log_string != 0; log_string++) {
            printf("%s\n",*log_string);
        }
    }

    return 0;
}
