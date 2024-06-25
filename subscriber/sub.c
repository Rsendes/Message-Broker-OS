#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>
#include "logging.h"

#define PIPE_SIZE 256
#define BOX_SIZE 32
#define MESSAGE_SIZE 1024

int main(int argc, char **argv) {
    if (argc != 4) return -1;

    uint8_t code = 2;
    char register_pipe_name[PIPE_SIZE*sizeof(char)] = {0};
    char pipe_name[PIPE_SIZE*sizeof(char)] = {0};
    char box_name[BOX_SIZE*sizeof(char)] = {0};
    memcpy(register_pipe_name, argv[1], strlen(argv[1]));
    memcpy(pipe_name, argv[2], strlen(argv[2]));
    memcpy(box_name, argv[3], strlen(argv[3]));

    if (unlink(pipe_name) != 0 && errno != ENOENT) // no caso de já existir
        return -1;

    if (mkfifo(pipe_name, 0640) != 0) // criamos o pipename
        return -1;

    int tx = open(register_pipe_name, O_WRONLY);
    if (tx == -1) return -1;

    uint8_t buf[sizeof(uint8_t) + (PIPE_SIZE+BOX_SIZE+1)*sizeof(char)] = {0};
    memcpy(buf, &code, sizeof(uint8_t));
    memcpy(buf + sizeof(uint8_t), pipe_name, strlen(pipe_name));
    memcpy(buf + sizeof(uint8_t) + strlen(pipe_name)*sizeof(char), "|", 1);
    memcpy(buf + sizeof(uint8_t) + (strlen(pipe_name)+1)*sizeof(char), box_name, strlen(box_name));

    ssize_t ret = write(tx,buf, sizeof(uint8_t) + (strlen(pipe_name)+strlen(box_name)+1)*sizeof(char));
    if (ret == -1) return -1;

    int rx = open(pipe_name, O_RDONLY);
    if (rx == -1) return -1;

    char message[MESSAGE_SIZE*sizeof(char)] = {0};

    while (true) {
        if (read(rx, message, MESSAGE_SIZE*sizeof(char)) != 0)
            fprintf(stdout, "%s\n", message);
    }

    close(tx);
}
