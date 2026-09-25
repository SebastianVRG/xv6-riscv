#include "kernel/types.h"
#include "kernel/pstat.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
    int start, end, pid;
    // 
    ///////////////////////
    int status, elapsed, pcpu;
    struct rusage ru;
    ///////////////////////

    // need at least 2 args:
    // text "time" and the command to time
    if (argc < 2) {
        fprintf(2, "usage: time command [args...]\n");
        exit(1);
    }

    // tick count before starting the child
    start = uptime();

    // fork: creates a child process
    // returns child's pid to parent, 0 to child, negative on failure
    pid = fork();

    // if negative, fork failed, no child was created
    if (pid < 0) {

        fprintf(2, "time: fork failed\n");
        exit(1);

    // if 0 it is a child process
    // exec replaces child's code with the command to run
    } else if (pid == 0) {

        exec(argv[1], argv + 1);

        // if exec failed
        fprintf(2, "time: exec %s failed\n", argv[1]);
        exit(1);

    // parent process
    // wait2 blocks until child finishes, and also returns cputime via ru
    } else {
	
	///////////////////////
        // calls the syscall
        wait2(&status, &ru);

        // tick count after child finished
        end = uptime();
        elapsed = end - start;

        // compute CPU, protect against zero division. 
        if (elapsed > 0) {
            pcpu = (ru.cputime * 100) / elapsed;
        } else {
            pcpu = 0;
        }

        printf("elapsed time: %d ticks, cpu time: %d ticks, %d%% CPU\n",
               elapsed, ru.cputime, pcpu);
    }
///////////////////////
    exit(0);
}
