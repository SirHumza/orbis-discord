#include "files.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int cmp_name(const void *a, const void *b){
    return strcmp((const char *)a, (const char *)b);
}

int file_list(const char *dir, char names[][FILE_NAME_MAX], int cap){
    DIR *d;
    struct dirent *e;
    int n = 0;
    if(!dir || !names || cap <= 0) return 0;
    d = opendir(dir);
    if(!d) return 0;
    while((e = readdir(d)) != NULL && n < cap){
        struct stat st;
        char path[512];
        if(e->d_name[0] == '.') continue;
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
        snprintf(names[n], FILE_NAME_MAX, "%s", e->d_name);
        n++;
    }
    closedir(d);
    qsort(names, (size_t)n, FILE_NAME_MAX, cmp_name);
    return n;
}
