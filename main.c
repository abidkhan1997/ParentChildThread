#include <stdbool.h>
#include <semaphore.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>

#define MY_WRITE_SEM "/buffer_produce_consumer_write"
#define MY_READ_SEM "/buffer_produce_consumer_read"

int out_file;

void consumer(const char TGT_FILE[], int BUFFER_SIZE, int CHUNK_SIZE, int p[2]) {
    FILE *file_ptr;
    const char *name = "OS"; /* name of the shared memory object */
    int shm_FD;              /* shared memory file descriptor */
    void *ptr, *temp_ptr;    /* pointer to shared memory object */
    char *message;
    size_t len = CHUNK_SIZE - 1, bytes_wrote = 0;
    int shMemCsrCharCount = 0;
    int shMemPrdCharCount;
    sem_t *my_write_sem, *my_read_sem;
    char *output_buffer = malloc(sizeof(char) * (BUFFER_SIZE + 100));

    file_ptr = fopen(TGT_FILE, "w");

    shm_FD = shm_open(name, O_RDONLY, 0666); /* open the shared memory object */

    /* memory map the shared memory object */
    ptr = mmap(0, BUFFER_SIZE, PROT_READ, MAP_SHARED, shm_FD, 0);
    temp_ptr = ptr;

    // sleep(1); // sleeping for 1 sec in order to allow producer to take a lead
    // over consumer

    my_write_sem = sem_open(MY_WRITE_SEM, O_CREAT, S_IRWXU, 1);
    if (my_write_sem == NULL) {
        perror("In sem_open() opening write semaphore.");
        exit(1);
    }

    my_read_sem = sem_open(MY_READ_SEM, O_CREAT, S_IRWXU, 0);
    if (my_read_sem == NULL) {
        perror("In sem_open() opening read semaphore.");
        exit(1);
    }


    /* read from the shared memory object */
    while (len == CHUNK_SIZE - 1) {
        sem_wait(my_read_sem);
        message = (char *)temp_ptr;
        len = strlen(message);
        /*fwrite(message, 1, len, file_ptr);*/
        fprintf(file_ptr, "%s", message);

        sprintf(output_buffer ,"CHILD: IN = %lu\n", len);
        write(out_file, output_buffer, strlen(output_buffer));
        sprintf(output_buffer ,"CHILD: ITEM = %s\n", message);
        write(out_file, output_buffer, strlen(output_buffer));

        bytes_wrote += len;
        shMemCsrCharCount += len;
        temp_ptr = ptr + ((((bytes_wrote % BUFFER_SIZE) + CHUNK_SIZE) < BUFFER_SIZE)
                ? (bytes_wrote % BUFFER_SIZE)
                : 0);
        if (temp_ptr == ptr)
            sem_post(my_write_sem);
    }

    sprintf(output_buffer, "Consumer Done Consuming\n");
    write(out_file, output_buffer, strlen(output_buffer));

    /* remove the shared memory object */
    shm_unlink(name);
    fclose(file_ptr);
    char *pipe_buffer = malloc(sizeof(char) * 10);
    close(p[1]);
    read(p[0], pipe_buffer, 10);
    sscanf(pipe_buffer,"%d", &shMemPrdCharCount);
    sprintf(output_buffer, "CHILD: The parent value of shMemPrdCharCount = %d\n"
            "CHILD: The child value of shMemCsrCharCount = %d\n",
            shMemPrdCharCount, shMemCsrCharCount
           );
    write(out_file, output_buffer, strlen(output_buffer));
    free(pipe_buffer);
    free(output_buffer);
}

void producer(const char SRC_FILE[], int BUFFER_SIZE, int CHUNK_SIZE, int p[2]) {
    FILE *file_ptr = fopen(SRC_FILE, "rt");
    const char *name = "OS"; /* name of the shared memory object */
    int shm_FD;              /* shared memory file descriptor */
    void *ptr, *temp_ptr;    /* pointer to shared memory object */
    char message[CHUNK_SIZE];
    size_t len = CHUNK_SIZE - 1, bytes_wrote = 0;
    int shMemPrdCharCount = 0;
    char *output_buffer = malloc(sizeof(char) * (BUFFER_SIZE + 100));
    sem_t *my_write_sem, *my_read_sem;

    /* create the shared memory object */
    shm_FD = shm_open(name, O_CREAT | O_RDWR, 0666);
    /* configure the size of the shared memory object */
    ftruncate(shm_FD, BUFFER_SIZE);

    /* memory map the shared memory object */
    ptr = temp_ptr = mmap(0, BUFFER_SIZE, PROT_WRITE, MAP_SHARED, shm_FD, 0);

    my_write_sem = sem_open(MY_WRITE_SEM, O_CREAT, S_IRWXU, 1);
    if (my_write_sem == NULL) {
        perror("In sem_open() opening write semaphore.");
        exit(1);
    }

    my_read_sem = sem_open(MY_READ_SEM, O_CREAT, S_IRWXU, 0);
    if (my_read_sem == NULL) {
        perror("In sem_open() opening read semaphore.");
        exit(1);
    }

    /* write to the shared memory object */
    while (len == CHUNK_SIZE - 1) {
        sem_wait(my_write_sem);
        // reading from the file chunk by chunk
        len = fread(message, 1, CHUNK_SIZE - 1, file_ptr);
        message[len] = '\0';
        len = sprintf(temp_ptr, "%s", message);

        sprintf(output_buffer,"PARENT: IN = %lu\n", len);
        write(out_file, output_buffer, strlen(output_buffer));
        sprintf(output_buffer,"PARENT: ITEM = %s\n", message);
        write(out_file, output_buffer, strlen(output_buffer));
        bytes_wrote += strlen(message);
        shMemPrdCharCount += strlen(message);
        temp_ptr = ptr + ((((bytes_wrote % BUFFER_SIZE) + CHUNK_SIZE) < BUFFER_SIZE)
                ? (bytes_wrote % BUFFER_SIZE)
                : 0);
        if (temp_ptr == ptr)
            sem_post(my_read_sem);
    }
    sprintf(output_buffer,"Producer Done Producing\n");
    write(out_file, output_buffer, strlen(output_buffer));
    fclose(file_ptr);
    char *pipe_buffer = malloc(sizeof(char) * 10);
    sprintf(pipe_buffer, "%d", shMemPrdCharCount);
    close(p[0]);
    write(p[1], pipe_buffer, sizeof pipe_buffer);
    sprintf(output_buffer,"PARENT: The parent value of shMemPrdCharCount = %d\n", shMemPrdCharCount);
    write(out_file, output_buffer, strlen(output_buffer));
    free(pipe_buffer);
    free(output_buffer);
}


int main(int argc, char *argv[]) {
    if (argc != 5)
        return 1;

    char *outfilename = "MYSTUDENTNAME.out";
    out_file = open(outfilename, O_CREAT | O_WRONLY | O_APPEND, 0644);

    int child_pid;
    const char *SRC_FILE = argv[1], *TGT_FILE = argv[2];
    const int CHUNK_SIZE = strtol(argv[3], (char **)NULL, 10) + 1, BUFFER_SIZE = strtol(argv[4], (char **)NULL, 10);

    int p[2];
    if (pipe(p) < 0){
        perror("Error in creating pipe.\n");
        exit(2);
    }
    child_pid = fork();
    if (child_pid == 0) {
        // Child Process
        consumer(TGT_FILE, BUFFER_SIZE, CHUNK_SIZE, p);
        exit(0);
    } else {
        // Parent Process
        producer(SRC_FILE, BUFFER_SIZE, CHUNK_SIZE, p);
        wait(0);
    }
    close(out_file);
    return 0;
}
