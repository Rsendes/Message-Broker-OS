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
#define COMMAND_SIZE 8
#define ERROR_SIZE 1024

int main(int argc, char **argv) {
    if ((argc != 4) && (argc != 5))
        return -1;
    
    uint8_t code = 0;
    char register_pipe_name[PIPE_SIZE*sizeof(char)] = {0};
    char pipe_name[PIPE_SIZE*sizeof(char)] = {0};
    char command[COMMAND_SIZE*sizeof(char)] = {0};
    memcpy(register_pipe_name, argv[1], strlen(argv[1]));
    memcpy(pipe_name, argv[2], strlen(argv[2]));
    memcpy(command, argv[3], strlen(argv[3]));

    if (unlink(pipe_name) != 0 && errno != ENOENT) // no caso de já existir
        return -1;

    if (mkfifo(pipe_name, 0640) != 0) // criamos o pipename
        return -1;

    int tx = open(register_pipe_name, O_WRONLY);
    if (tx == -1) return -1;

    if (memcmp(command, "create", strlen(command)) == 0) {
        code = 3;
    } else if (memcmp(command, "remove", strlen(command)) == 0) {
        code = 5;
    } else if (memcmp(command, "list", strlen(command)) == 0) {
        code = 7;
    }

    if (argc == 4) {
        uint8_t buf[sizeof(uint8_t) + PIPE_SIZE*sizeof(char)] = {0};
        memcpy(buf, &code, sizeof(uint8_t));
        memcpy(buf + sizeof(uint8_t), pipe_name, strlen(pipe_name));

        ssize_t ret = write(tx,buf, sizeof(uint8_t) + strlen(pipe_name)*sizeof(char));
        if (ret == -1) return -1;
    }

    else if (argc == 5) {
        char box_name[BOX_SIZE*sizeof(char)] = {0};
        memcpy(box_name, argv[4], strlen(argv[4]));
        uint8_t buf[sizeof(uint8_t) + (PIPE_SIZE+BOX_SIZE+1)*sizeof(char)] = {0};
        memcpy(buf, &code, sizeof(uint8_t));
        memcpy(buf + sizeof(uint8_t), pipe_name, strlen(pipe_name));
        memcpy(buf + sizeof(uint8_t) + strlen(pipe_name)*sizeof(char), "|", 1);
        memcpy(buf + sizeof(uint8_t) + (strlen(pipe_name)+1)*sizeof(char), box_name, strlen(box_name));

        ssize_t ret = write(tx,buf, sizeof(uint8_t) + (strlen(pipe_name)+strlen(box_name)+1)*sizeof(char));
        if (ret == -1) return -1;
    }

    int rx = open(pipe_name, O_RDONLY);
    if (rx == -1) return -1;

    uint8_t op_code;
    ssize_t ret = read(rx, &op_code, sizeof(char));
    if (ret == -1) return -1;

    // verifica op_code

    if (op_code == 4 || op_code == 6) {
        int32_t return_code;
        ret = read(rx, &return_code, sizeof(int32_t));
        if (ret == -1) return -1;

        if (return_code == 0)
            fprintf(stdout, "OK\n");
        else {
            char error_message[ERROR_SIZE*sizeof(char)] = {0};
            ret = read(rx, error_message, ERROR_SIZE*sizeof(char));
            if (ret == -1) return -1;
            fprintf(stdout, "ERROR %s\n", error_message);
        }
    }
    else if (op_code == 8) {
        uint8_t last;
            char box_name[BOX_SIZE*sizeof(char)] = {0};
            char empty_box_name[BOX_SIZE*sizeof(char)] = {0};
            uint64_t box_size;
            uint64_t n_publisher;
            uint64_t n_subscribers;
        while (true) {
            ret = read(rx, &last, sizeof(uint8_t));
            if (ret == -1) return -1;
            ret = read(rx, box_name, BOX_SIZE*sizeof(char));
            if (ret == -1) return -1;
            ret = read(rx, &box_size, sizeof(uint64_t));
            if (ret == -1) return -1;
            ret = read(rx, &n_publisher, sizeof(uint64_t));
            if (ret == -1) return -1;
            ret = read(rx, &n_subscribers, sizeof(uint64_t));
            if (ret == -1) return -1;

            if (memcmp(box_name, empty_box_name, strlen(box_name)) == 0)
                fprintf(stdout, "NO BOXES FOUND\n");
            else
                fprintf(stdout, "%s %zu %zu %zu\n", box_name, box_size, n_publisher, n_subscribers);
            if (last == 1) break;
        }
    }

    //recebe resposta do mbroker
    //imprime resposta
    close(tx);
}