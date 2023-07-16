#include <lib.h>

#define ARRAY_SIZE (128 * 1024 * 1024)

char big_array[ARRAY_SIZE];

int main() {
    pid_t pid1, pid2;

    pid1 = fork();

    if (pid1 == 0) {
        // Child
        printf("\n\n\tI am child 1\n\n");
    } else {
        pid2 = fork();

        if (pid2 == 0) {
            // Child
            memset(big_array, 0xd0, sizeof big_array);
            printf("\n\n\tI am child 2\n\n");
        } else {
            printf("\n\n\tI am the parent\n\n");
            waitpid(pid2, NULL, 0);
        }
    }

    return 0;
}