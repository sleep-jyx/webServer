#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>

#define BUFFER_SIZE 64

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("用法:%s ip port\n", basename(argv[0])); //basename只选取路径上最后一个文件(当前文件)
        //printf("%s\n", argv[0]);
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    //服务器的ip和端口号
    struct sockaddr_in server_address;
    bzero(&server_address, sizeof(server_address));
    server_address.sin_family = AF_INET;              //协议族定为ipv4
    inet_pton(AF_INET, ip, &server_address.sin_addr); //将主机字节序的ip地址转为网络字节序，并写入服务器socket地址
    server_address.sin_port = htons(port);            //将主机字节序的端口号转为16位(short)网络字节序端口号

    //创建客户端上的socket描述符
    int sockfd = socket(PF_INET, SOCK_STREAM, 0); //ivp4,TCP,只是创建了一个空壳socket
    assert(sockfd >= 0);

    if (connect(sockfd, (struct sockaddr *)&server_address, sizeof(server_address)) < 0)
    { //主动连接，一般是客户端socket向服务器socket主动发起连接,连接成功后sockfd唯一的标识该连接
        printf("连接失败\n");
        close(sockfd);
        return 1;
    }
    else
    {
        printf("客户端连接服务器成功!sockfd=%d\n", sockfd);
    }

    pollfd fds[2];
    //注册标准输入(文件描述符为0) 和文件描述符sockfd上的可读事件
    fds[0].fd = 0;
    fds[0].events = POLLIN;             //注册的事件，可读事件
    fds[0].revents = 0;                 //实际发生的事件，由内核填充
    fds[1].fd = sockfd;                 //客户端上socket文件描述符
    fds[1].events = POLLIN | POLLRDHUP; //后者在socket上接收到对方关闭连接的请求之后触发。使用POLLRDHUP事件时，开头要定义——GNU_SOURCE
    fds[1].revents = 0;

    char read_buf[BUFFER_SIZE];
    int pipefd[2];          //pipefd[0]读，pipefd[1]写。默认情况下，都是阻塞的。当然可以使用程序设为非阻塞的
    int ret = pipe(pipefd); //pipe创建一个管道，实现进程间通信。调用成功后将一对打开的文件描述符填入数组
    assert(ret != -1);

    while (1)
    {
        ret = poll(fds, 2, -1); //2个文件描述符（一个监听标准输出、一个监听客户端服务器连接），2个，timeout参数为-1表示阻塞
        if (ret < 0)
        {
            printf("poll调用失败\n");
            break;
        }

        if (fds[1].revents & POLLRDHUP)
        { //POLLRDHUP是TCP连接被对方(服务器)关闭
            printf("服务器关闭了连接\n");
            break;
        }
        else if (fds[1].revents & POLLIN)
        { //客户端的socket上有数据可读（服务器发来了数据）
            memset(read_buf, '\0', BUFFER_SIZE);
            recv(fds[1].fd, read_buf, BUFFER_SIZE - 1, 0); //读取客户端socket上的数据到read_buf中
            printf("服务器发来数据：%s\n", read_buf);
        }

        if (fds[0].revents & POLLIN)
        { //如果标准输出有数据可读
            //使用splice(零拷贝)将输入的数据输出到标准输出上
            printf("标准输出(终端)有数据可读,发往服务器\n");
            ret = splice(0, NULL, pipefd[1], NULL, 32767, SPLICE_F_MORE | SPLICE_F_MOVE);      //标准输出的数据读入管道
            ret = splice(pipefd[0], NULL, sockfd, NULL, 32767, SPLICE_F_MORE | SPLICE_F_MOVE); //将管道数据输出到socket上
        }
    }

    close(sockfd);
    return 0;
}