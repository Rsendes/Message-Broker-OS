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
#include "operations.h"
#include "state.h"

#define PIPE_SIZE 256
#define BOX_SIZE 32
#define ERROR_SIZE 1024
#define MESSAGE_SIZE 1024
int max_sessions;

struct subscriber {
    char pipe_name[PIPE_SIZE];
    struct subscriber *next;
};

struct box {
    char* box_name;
    uint64_t box_size;
    char* publisher;
    uint64_t n_subscribers;
    struct subscriber *subscribers;
};

struct box_node {
    struct box *box_data;
    struct box_node *next;
};

struct box_list {
    struct box_node *head;
    int size;
};

struct box_list *list;

struct box *find_box(char *box_name) {
    struct box_node *current = list->head;
    while (current != NULL) {
        if (strncmp(current->box_data->box_name, box_name, strlen(box_name)) == 0) {
            return current->box_data;
        }
        current = current->next;
    }
    return NULL;
}

int publisher_handler(char const *pipe_name, char *box_name) {
    int tx = open(pipe_name, O_RDONLY);
    if (tx == -1) return -1;

    struct box *box = find_box(box_name);
    if (box == NULL) { //box doesn't exit
        close(tx);
        return 0;
    }

    if (box->publisher != NULL) { // box has a publisher already
        close(tx);
        return 0;
    }

    box->publisher = malloc(sizeof(char) * strlen(pipe_name));

    memcpy(box->publisher, pipe_name, strlen(pipe_name));

    int fhandle = tfs_open(box_name, TFS_O_APPEND);
    if (fhandle == -1) return -1;

    char message[MESSAGE_SIZE*sizeof(char)] = {0};
    while (read(tx, message, MESSAGE_SIZE*sizeof(char)) != 0) {
        message[strlen(message)] = '0';
        tfs_write(fhandle, message, strlen(message));
    }
    tfs_close(fhandle);
    
    close(tx);

    return 0;
}


int subscriber_handler(char const *pipe_name, char *box_name) {
    int tx = open(pipe_name, O_WRONLY);
    if (tx == -1) return -1;

    struct box *box = find_box(box_name);
    if (box == NULL) { //box doesn't exit)
        return 0;
    }

    // adiciona o subscriber a caixa
    box->n_subscribers++;
    struct subscriber *new_subscriber = (struct subscriber*)malloc(sizeof(struct subscriber));
    strcpy(new_subscriber->pipe_name, pipe_name); 
    new_subscriber->next = box->subscribers;
    box->subscribers = new_subscriber;

    int fhandle = tfs_open(box_name, TFS_O_CREAT);
    if (fhandle == -1) return -1;

    char car = 0;
    int i = 0;
    char message[MESSAGE_SIZE*sizeof(char)] = {0};    

    while(tfs_read(fhandle, &car, sizeof(char)) != 0) {
        if (car == '0') {
            ssize_t ret = write(tx, message, MESSAGE_SIZE*sizeof(char));
            if (ret == -1) return -1;
            memset(message, 0, strlen(message));
            i = 0;
            continue;
        }
        message[i++] = car;
    }
    return 0;
}


int create_box_handler(char const *pipe_name, char const *box_name) {
    int tx = open(pipe_name, O_WRONLY);
    if (tx == -1) return -1;

    int32_t return_code = 0;
    uint8_t op_code = 4;
    char error_message[ERROR_SIZE*sizeof(char)] = {0};
    memcpy(error_message, "\0", sizeof(char));
    uint8_t buf[sizeof(uint8_t) + sizeof(int32_t) + (ERROR_SIZE)*sizeof(char)] = {0};

    if (tfs_open(box_name, TFS_O_TRUNC) != -1) {
        return_code = -1;
        memcpy(error_message, "Box already exists", 19);
    }

    if (tfs_open(box_name, TFS_O_CREAT) == -1) {
        return_code = -1;
        memcpy(error_message, "Failed creating box", 20);
    }

    memcpy(buf, &op_code, sizeof(uint8_t));
    memcpy(buf + sizeof(uint8_t), &return_code, sizeof(int32_t));
    memcpy(buf + sizeof(uint8_t) + sizeof(int32_t), error_message, ERROR_SIZE*sizeof(char));

    ssize_t ret = write(tx,buf, sizeof(uint8_t) + sizeof(int32_t) + (strlen(error_message)+1)*sizeof(char));
    if (ret == -1) return -1;
    close(tx);

    if (strlen(error_message) > 0)
        return 0;

    struct box *new_box = (struct box *) malloc(sizeof(struct box));
    new_box->box_name = (char*) malloc(sizeof(char)*strlen(box_name));
    memcpy(new_box->box_name, box_name, strlen(box_name));

    struct box_node *new_node = (struct box_node *) malloc(sizeof(struct box_node));
    new_node->box_data = malloc(sizeof(struct box));
    new_node->box_data->box_name = malloc(sizeof(char) * strlen(box_name));
    memcpy(new_node->box_data->box_name, box_name, strlen(box_name));
    new_node->next = list->head;
    list->head = new_node;
    list->size++;

    return 0;
}


int remove_box_handler(char const *pipe_name, char const *box_name) {
    int tx = open(pipe_name, O_WRONLY);
    if (tx == -1) return -1;

    int32_t return_code = 0;
    uint8_t op_code = 6;
    char error_message[ERROR_SIZE*sizeof(char)] = {0};
    memcpy(error_message, "\0", sizeof(char));
    uint8_t buf[sizeof(uint8_t) + sizeof(int32_t) + (ERROR_SIZE)*sizeof(char)] = {0};

    if (tfs_open(box_name, TFS_O_TRUNC) == -1) {
        return_code = -1;
        memcpy(error_message, "Box doesnt exist", 17);
    }

    else if (tfs_unlink(box_name) == -1) {
        return_code = -1;
        memcpy(error_message, "Failed removing box", 20);
    }

    memcpy(buf, &op_code, sizeof(uint8_t));
    memcpy(buf + sizeof(uint8_t), &return_code, sizeof(int32_t));
    memcpy(buf + sizeof(uint8_t) + sizeof(int32_t), error_message, strlen(error_message));

    ssize_t ret = write(tx, buf, sizeof(uint8_t) + sizeof(int32_t) + (strlen(error_message)+1)*sizeof(char));
    if (ret == -1) return -1;

    struct box_node *current = list->head;
    struct box_node *previous = NULL;

    while (current != NULL) {
        if (strcmp(current->box_data->box_name, box_name) == 0) {
            if (previous == NULL) {
                list->head = current->next;
            } else {
                previous->next = current->next;
            }
            free(current->box_data);
            free(current);
            list->size--;
            break;
        }
        previous = current;
        current = current->next;
    }
    
    return 0;
}


int list_box_handler(char const *pipe_name) {
    int rx = open(pipe_name, O_WRONLY);
    if (rx == -1) {
        perror("Failed to open pipe for reading.");
        return -1;
    }

    uint8_t op_code = 8;
    uint8_t last = 0;
    uint8_t buf[sizeof(uint8_t) + (BOX_SIZE)*sizeof(char) + 3 * sizeof(uint64_t)] = {0}; 

    struct box_node *current = list->head;
    int aux = list->size;

    ssize_t ret = write(rx, &op_code, sizeof(uint8_t));
    if (ret == -1) return -1;

    if (aux == 0 ) {
        char empty_box_name[BOX_SIZE*sizeof(char)] = {0}; 
        last = 1;
        uint64_t empty = 0; 
        memcpy(buf, &last, sizeof(uint8_t));
        memcpy(buf + sizeof(uint8_t), empty_box_name, strlen(empty_box_name));
        memcpy(buf + sizeof(uint8_t)+ (BOX_SIZE)*sizeof(char) , &empty, sizeof(uint64_t));
        memcpy(buf + sizeof(uint8_t)+ (BOX_SIZE)*sizeof(char)+ sizeof(uint64_t), &empty, sizeof(uint64_t));
        memcpy(buf + sizeof(uint8_t)+ (BOX_SIZE)*sizeof(char)+ 2*sizeof(uint64_t), &empty, sizeof(uint64_t));

        ret = write(rx, buf, sizeof(buf));
        if (ret == -1) return -1;
        return 0;
    }
    while (aux >1) {
        uint64_t n_publisher;
        if ( current->box_data->publisher != NULL) {
            n_publisher = 1;
        }

        memcpy(buf, &last, sizeof(uint8_t));
        memcpy(buf + sizeof(uint8_t), current->box_data->box_name, BOX_SIZE*sizeof(char));
        memcpy(buf + sizeof(uint8_t) + (BOX_SIZE)*sizeof(char), &current->box_data->box_size, sizeof(uint64_t));
        memcpy(buf + sizeof(uint8_t) + (BOX_SIZE)*sizeof(char) + sizeof(uint64_t), &n_publisher, sizeof(uint64_t)); 
        memcpy(buf + sizeof(uint8_t) + (BOX_SIZE)*sizeof(char) + 2 * sizeof(uint64_t), &current->box_data->n_subscribers, sizeof(uint64_t));
        
        ret = write(rx, buf, sizeof(buf));
        if (ret == -1) return -1;

        current = current->next;
        aux--;
    }
    last = 1;
    uint64_t n_publisher;
    if ( current->box_data->publisher != NULL) {
        n_publisher = 1;
    }

    memcpy(buf, &last, sizeof(uint8_t));
    memcpy(buf + sizeof(uint8_t), current->box_data->box_name, BOX_SIZE*sizeof(char));
    memcpy(buf + sizeof(uint8_t) + (BOX_SIZE)*sizeof(char), &current->box_data->box_size, sizeof(uint64_t));
    memcpy(buf + sizeof(uint8_t) + (BOX_SIZE)*sizeof(char) + sizeof(uint64_t), &n_publisher, sizeof(uint64_t));
    memcpy(buf + sizeof(uint8_t) + (BOX_SIZE)*sizeof(char) + 2 * sizeof(uint64_t), &current->box_data->n_subscribers, sizeof(uint64_t));
    
    ret = write(rx, buf, sizeof(buf));
    if (ret == -1) return -1;

    return 0;
}


int main(int argc, char **argv) {
    if(argc != 3) return -1;
    tfs_init(NULL);

    list = (struct box_list *)malloc(sizeof(struct box_list));
    list->head = NULL;

    char register_pipe_name[PIPE_SIZE*sizeof(char)] = {0};
    memcpy(register_pipe_name, argv[1], strlen(argv[1]));
    //max sessions

    if (unlink(register_pipe_name) != 0 && errno != ENOENT) // no caso de já existir
        return -1;

    if (mkfifo(register_pipe_name, 0640) != 0) // criamos o pipename
        return -1;

    while(true){

        int rx = open(register_pipe_name, O_RDONLY);
        if (rx == -1) return -1;

        uint8_t op_code = 0;
        ssize_t ret = read(rx, &op_code, sizeof(uint8_t));
        if (ret == -1) {
            return -1;
        }

        if (op_code > 10) // verifica se op_code eh valido
            return -1;

        if (op_code == 0) continue;

        char pipe_name[PIPE_SIZE*sizeof(char)] = {0};
        char box_name[BOX_SIZE*sizeof(char)] = {0};

        char buffer[(PIPE_SIZE + BOX_SIZE)*sizeof(char)] = {0};
        ret = read(rx, buffer, (PIPE_SIZE + BOX_SIZE)*sizeof(char));
        if (ret == -1) return -1;

        strcpy(pipe_name, strtok(buffer, "|"));

        if (op_code != 7)
            strcpy(box_name, strtok(NULL, "|"));

        if (op_code == 1){
            if (publisher_handler(pipe_name, box_name) == -1)
                return -1;
        } else if (op_code == 2) {
            if (subscriber_handler(pipe_name, box_name) == -1)
                return -1;
        } else if (op_code == 3) {
            if (create_box_handler(pipe_name, box_name) == -1) 
                return -1;
        } else if (op_code == 5) {
            if (remove_box_handler(pipe_name, box_name) == -1)
                return -1;
        } else if (op_code == 7) {
            if (list_box_handler(pipe_name) == -1)
                return -1;
        }
        
        close(rx);
    }
    
}