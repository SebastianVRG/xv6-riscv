#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int start, end, pid;

    // need at least 2 args: 
    // text "time1" and the command to time
    if (argc < 2) {
        fprintf(2, "usage: time1 command [args...]\n");
        exit(1);
    }

    // tick count before starting the child
    start = uptime();

    // fork: creates a child process
    // returns child's pid to parent, 0 to child, negative on failure
    pid = fork();
    
    // if negative, fork failed, no child was created
    if (pid < 0) {
        
        fprintf(2, "time1: fork failed\n");
        exit(1);

    // if 0 it is a child process
    // exec replaces child's code with the command to run
    } else if (pid == 0) {
        
        exec(argv[1], argv + 1);

        // if exec failed
        fprintf(2, "time1: exec %s failed\n", argv[1]);
        exit(1);

    // parent process
    // wait blocks until child finishes
    } else {
        
        wait(0);

        // tick count after child finished
        end = uptime();

        printf("elapsed time: %d ticks\n", end - start);
    }

    exit(0);
}
