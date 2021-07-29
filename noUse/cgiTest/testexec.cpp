#include <stdio.h>
//#include <sys/types.h>
#include <unistd.h>
int main()
{
    int p = fork();
    if (p == 0)
    {
        printf("子进程\n");
        execl("/home/jyx/文档/unix/linuxServer/root/cgi/cgiTest.cgi", "hello", NULL);
    }
    else
    {
        printf("父进程\n");
    }
    return 0;
}