#include <stdio.h>
#include <sys/epoll.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <poll.h>
#include <fcntl.h>
#include <iostream>
using namespace std;

int main()
{
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    printf("%d\n", epollfd);

    //测试文件描述符的分配，看来是自动增长型的
    int listenfd = -1;
    cout << "test socket:" << endl;
    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    cout << "first socket：" << listenfd << endl;
    close(listenfd);
    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    cout << "second socket：" << listenfd << endl;
    return 0;
}