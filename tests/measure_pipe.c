#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void) {
    int pair[2];
    if (pipe(pair) < 0) {
        perror("pipe");
        return 1;
    }
    int flags = fcntl(pair[1], F_GETFL);
    if (flags < 0 || fcntl(pair[1], F_SETFL, flags | O_NONBLOCK) < 0) {
        perror("fcntl");
        return 1;
    }

    char buf[4096];
    memset(buf, 'x', sizeof buf);
    size_t total = 0;
    while (1) {
        ssize_t n = write(pair[1], buf, sizeof buf);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            perror("write");
            return 1;
        }
        if (n == 0) {
            break;
        }
        total += (size_t)n;
    }
    printf("%zu\n", total);
    return 0;
}
