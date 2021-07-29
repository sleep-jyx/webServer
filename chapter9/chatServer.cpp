#define _GNU_SOURCE 1
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>

#define USER_LIMIT 5   //最大用户数量
#define BUFFER_SIZE 64 //读缓冲区大小
#define FD_LIMIT 65535 //文件描述符数量限制

struct client_data
{
    sockaddr_in address;   //客户端socket地址，包括ip和port
    char *write_buf;       //待写入客户端数据的位置
    char buf[BUFFER_SIZE]; //从客户端读入的数据
};

//将文件描述符设置为非阻塞,返回旧状态用于恢复
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

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

    //服务器的socket地址，客户端连的socket也就是这个ip和port
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;              //协议族定为ipv4
    inet_pton(AF_INET, ip, &address.sin_addr); //将主机字节序的ip地址转为网络字节序，并写入服务器socket地址
    address.sin_port = htons(port);            //将主机字节序的端口号转为16位(short)网络字节序端口号

    int listenfd = socket(PF_INET, SOCK_STREAM, 0); //创建空壳socket描述符，分配文件描述符
    assert(listenfd >= 0);

    int ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address)); //socket描述符与实体绑定
    assert(ret != -1);

    ret = listen(listenfd, 5); //服务器开启监听socket，监听队列最多5个请求
    assert(ret != -1);

    //创建用户对象数据
    client_data *users = new client_data[FD_LIMIT]; //限制文件描述符数量，new同时初始化?
    pollfd fds[USER_LIMIT + 1];                     //限制用户数量,+1是因为0号留给服务器，1~5号对应五个客户端
    int user_counter = 0;
    for (int i = 1; i <= USER_LIMIT; i++)
    {
        fds[i].fd = -1;    //5个用户的文件描述符初始化为-1
        fds[i].events = 0; //无事件的初始化
    }
    //poll池的第一个文件描述符是服务器本身
    fds[0].fd = listenfd;
    fds[0].events = POLLIN | POLLERR;
    fds[0].revents = 0;

    while (1)
    {
        ret = poll(fds, user_counter + 1, -1); //轮询服务器和5个客户端
        if (ret < 0)
        {
            printf("poll调用失败\n");
            break;
        }
        for (int i = 0; i < USER_LIMIT; i++)
        {
            if ((fds[i].fd == listenfd) && (fds[i].revents & POLLIN))
            { //轮询到服务器且服务器有可读事件，服务器被动接收(accept)请求队列上的请求
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                //接收客户端的socket连接，如果连接到了客户端，返回一个新的文件描述符。
                //不同客户端创建的第一个socked描述符都是3，不好区分。
                //于是在各客户端连接服务器时，服务器来给它们排个序，也就是connfd
                //分配的connfd可以自行填补空缺
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength); //服务器socket与某个客户端socket建立连接
                if (connfd < 0)
                {
                    printf("error:%d\n", errno);
                    continue;
                }
                else
                {
                    printf("服务器为新来的客户端连接分配描述符:%d\n", connfd);
                }
                //如果请求太多，关闭新到的连接
                if (user_counter >= USER_LIMIT)
                {
                    const char *info = "too many users\n";
                    printf("%s\n", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }

                //connfd是为各个客户端新分配的文件描述符
                user_counter++;
                users[connfd].address = client_address; //将服务器连接到的客户端的socket分配给 服务器内存上的用户数组
                setnonblocking(connfd);
                fds[user_counter].fd = connfd; //将建立的文件描述符(两个socket之间的连接)注册进poll池
                fds[user_counter].events = POLLIN | POLLRDHUP | POLLERR;
                fds[user_counter].revents = 0;
                printf("来了一个新连接，现有连接%d\n", user_counter);
            } //服务器处理完毕
            else if (fds[i].revents & POLLERR)
            {
                printf("出错\n");
                //todo
            }
            else if (fds[i].revents & POLLRDHUP)
            { //客户端关闭连接，则服务器也关闭对应连接，并将用户总数减1，这样轮询poll池时可以不再轮询已下线的连接

                users[fds[i].fd] = users[fds[user_counter].fd]; //把连接队列中最后一个连接顶替刚刚下线的连接。类似插入排序
                close(fds[i].fd);                               //关闭poll池中的刚刚下线的事件
                fds[i] = fds[user_counter];                     //poll池中最后一个poll事件顶替刚刚下线的poll事件
                i--;
                user_counter--;
                printf("一个用户下线了\n");
            }
            else if (fds[i].revents & POLLIN)
            { //一开始只有服务器才有数据可读，但一般连接的客户端程序在连接前都会让自己的socket是可读事件，而且这里用到了else已经排除了服务器
                //客户端有数据可读
                int connfd = fds[i].fd;
                memset(users[connfd].buf, '\0', BUFFER_SIZE);
                ret = recv(connfd, users[connfd].buf, BUFFER_SIZE - 1, 0); //接收该socket上的数据写到用户缓冲中
                printf("用户%d 发来%d字节数据:%s\n", connfd, ret, users[connfd].buf);
                if (ret < 0)
                { //读操作出错
                    if (errno != EAGAIN)
                    {
                        close(connfd);
                        users[fds[i].fd] = users[fds[user_counter].fd]; //顶替
                        fds[i] = fds[user_counter];                     //顶替
                        i--;
                        user_counter--;
                    }
                }
                else if (ret == 0)
                { //数据长度为0？遇到文件结束符了？
                }
                else
                { //收到了客户端上socket发来的数据，将数据广播给poll池中的其它socket
                    for (int j = 0; j <= user_counter; j++)
                    {
                        if (fds[j].fd == connfd)
                        { //不用给自己也广播
                            continue;
                        }
                        //以下两行是禁止socket能够同时读写，要么读要么写
                        fds[j].events |= ~POLLIN; //使得其它socket文件描述符除了不可读其它都可以？
                        fds[j].events |= POLLOUT; //使数据可写，fds是在内核中，服务器和客户端共享，但还没调用内核的数据发送函数，下面调用
                        users[fds[j].fd].write_buf = users[connfd].buf;
                    }
                }
            }
            else if (fds[i].revents & POLLOUT)
            { //如果有数据可写
                int connfd = fds[i].fd;
                if (!users[connfd].write_buf)
                    continue;
                //调用send()给客户端发送数据，发完，客户端进程就收到了
                ret = send(connfd, users[connfd].write_buf, strlen(users[connfd].write_buf), 0);
                users[connfd].write_buf = NULL;
                //以下两行是禁止socket能够同时读写，要么读要么写
                fds[i].events |= ~POLLOUT; //禁止可写
                fds[i].events |= POLLIN;   //使数据可读
            }
        }
    }
    delete[] users;
    close(listenfd);
    return 0;
}