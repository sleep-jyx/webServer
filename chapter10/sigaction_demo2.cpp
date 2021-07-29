#include <sys/wait.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

int main(void)
{
    pid_t childpid;
    int status;
    int retval;
    childpid = fork();
    if (childpid == 0)
    { //子进程
        while (1)
        {
            printf("In child process，pid = %d\n", childpid);
            sleep(1);
        }
        exit(0);
    }
    else
    { //父进程
        printf("father process ,pid = %d\n", childpid);
        sleep(2);
        if ((waitpid(childpid, &status, WNOHANG)) == 0)
        { //非阻塞等待，因为子进程睡眠10秒，肯定是等不到了
            retval = kill(childpid, SIGKILL);
            if (retval)
            {
                puts("kill failed.");
                perror("kill");
                waitpid(childpid, &status, 0);
            }
            else
            {
                printf("%d killed\n", childpid);
            }
        }
    }
    return 0;
}
