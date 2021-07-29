#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>

int main()
{
    struct sigaction newact, oldact;

    /* 设置信号忽略 */
    newact.sa_handler = SIG_IGN; //这个地方也可以是函数
    sigemptyset(&newact.sa_mask);
    newact.sa_flags = 0;
    int count = 0;
    pid_t pid = 0;

    sigaction(SIGINT, &newact, &oldact); //原来的备份到oldact里面

    pid = fork();
    if (pid == 0)
    {
        while (1)
        {
            printf("child1 process,pid=%d\n", pid);
            sleep(1);
        }
        return 0;
    }

    pid = fork();
    if (pid == 0)
    {
        while (1)
        {
            printf("child2 process,pid=%d\n", pid);
            sleep(1);
        }
        return 0;
    }

    while (1)
    {
        if (count++ > 3)
        {
            sigaction(SIGINT, &oldact, NULL); //备份回来
            printf("pid = %d\n", pid);
            kill(pid, SIGKILL); //明面上kill父进程，实际子进程也一并删除了
        }

        printf("father process,pid=%d\n", pid);
        sleep(1);
    }

    return 0;
}
