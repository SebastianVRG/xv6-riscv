/////////////////////////////////////2.2
#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  // print the priority the process starts with
  printf("initial priority: %d\n", getpriority());

  // change the priority to 20
  setpriority(20);
  // print current priority (20)
  printf("after setpriority(20): %d\n", getpriority());

  // child process should inherit the parent's priority
  if(fork() == 0){
    printf("child inherited: %d\n", getpriority());
    exit(0);
  }
  // wait for the child to finish before the parent continues
  wait(0);

  // 50 is out of range, so it should result in -1
  printf("setpriority(50) returns: %d (expected -1)\n", setpriority(50));

  // priority must be unchanged after the failed call
  printf("priority after failed set: %d (expected 20)\n", getpriority());

  exit(0);
}
//////////////////////////////////////2.2
