#include "token.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int token_load(char *out, size_t cap){
    int fd;
    ssize_t n;
    size_t i;
    if(!out || cap == 0) return 0;
    out[0] = 0;
    fd = open(ORBIS_TOKEN_PATH, O_RDONLY);
    if(fd < 0) return 0;
    n = read(fd, out, cap - 1);
    close(fd);
    if(n <= 0) return 0;
    out[n] = 0;
    for(i = 0; i < (size_t)n; i++)
        if(out[i] == '\n' || out[i] == '\r' || out[i] == ' ' || out[i] == '\t'){
            out[i] = 0;
            break;
        }
    return out[0] != 0;
}

void token_clear(void){
    unlink(ORBIS_TOKEN_PATH);
}

int token_save(const char *token){
    int fd;
    size_t n;
    ssize_t w;
    if(!token || !*token) return 0;
    mkdir("/data/orbis-discord", 0700);
    fd = open(ORBIS_TOKEN_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if(fd < 0) return 0;
    n = strlen(token);
    w = write(fd, token, n);
    close(fd);
    chmod(ORBIS_TOKEN_PATH, 0600);
    return w == (ssize_t)n;
}
