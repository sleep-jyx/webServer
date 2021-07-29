#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
int main()
{
    printf("本进程 pid=%d\n", getpid());
    int p = fork(); //返回是子进程的pid
    //子进程
    if (p == 0)
    {
        printf("这是子进程 pid=%d ppid=%d\n", getpid(), getppid());
    }
    else
    {
        //父进程中，p是子进程的pid
        printf("这是父进程 pid=%d p=%d ppid=%d\n", getpid(), p, getppid());
    }
    return 0;
}