#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include <bits/stdc++.h>
// ./testSocket 192.168.1.109 12345 5  ip、端口号、backlog监听队列数
static bool stop = false;
static void handle_term(int sig)
{
    stop = true;
}

int main(int argc, char *argv[])
{
    signal(SIGTERM, handle_term);
    if (argc <= 3)
    {
        printf("缺少参数\n");
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);
    int backlog = atoi(argv[3]);
    int sock = socket(PF_INET, SOCK_STREAM, 0); //ipv4,数据流(TCP)；返回一个socket文件描述符
    assert(sock >= 0);

    //创建一个ipv4 socket地址
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr); //ip字符串转网络字节序整数存入address.sin_addr
    address.sin_port = htons(port);            //主机字节序转网络字节序(short)

    int ret = bind(sock, (struct sockaddr *)&address, sizeof(address)); //socket命名
    assert(ret != -1);

    ret = listen(sock, backlog);
    assert(ret != -1);

    while (!stop)
    {
        sleep(1);
    }

    close(sock);

    return 0;
}