#include <stdio.h>
#include <signal.h>
#include <unistd.h>

void sig_handler(int sig)
{
    printf("我是信号处理函数\n");
    printf("传入参数%d\n", sig);
}

int main()
{
    //signal(SIGINT, SIG_DFL); //默认键盘输入中断进程信号的处理函数，因为默认就是这个，加不加一样
    //signal(SIGINT, SIG_IGN); //忽略信号
    signal(SIGINT, sig_handler); //使用自定义的信号处理函数
    int i;
    for (i = 0; i < 10; ++i)
    {
        printf("hello world\n");
        sleep(1);
    }
    return 0;
}