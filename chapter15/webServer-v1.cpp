#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("用法:%s ip port\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr); //指针转网络字节码
    address.sin_port = htons(port);            //主机码转网络字节短码

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    while (1)
    {
        struct sockaddr_in client;
        socklen_t client_addrlength = sizeof(client);
        int connfd = accept(listenfd, (struct sockaddr *)&client, &client_addrlength);
        if (connfd < 0)
        {
            printf("errno\n");
        }
        else
        {
            char request[1024];
            recv(connfd, request, 1024, 0);
            request[strlen(request) + 1] = '\0';
            printf("%s\n", request);
            printf("successeful!\n");
            char buf[520] = "HTTP/1.1 200 ok\r\nconnection: close\r\n\r\n"; //HTTP响应
            int s = send(connfd, buf, strlen(buf), 0);                      //发送响应
            //printf("send=%d\n",s);
            int fd = open("hello.html", O_RDONLY); //消息体
            sendfile(connfd, fd, NULL, 2500);      //零拷贝发送消息体
            close(fd);
            close(connfd);
        }
    }
    return 0;
}