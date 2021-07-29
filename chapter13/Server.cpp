#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>

#define USER_LIMIT 5
#define BUFFER_SIZE 1024
#define FD_LIMIT 65535
#define MAX_EVENT_NUMBER 1024
#define PROCESS_LIMIT 65536

//处理一个客户连接必要的数据
struct client_data
{
    sockaddr_in address; //客户端的socket地址
    int connfd;          //socket文件描述符
    pid_t pid;           //处理这个连接的子进程的pid
    int pipefd[2];       //和父进程通信用的管道
};

static const char *shm_name = "/my_shm";
int sig_pipefd[2];
int epollfd;
int listenfd;
int shmfd;
char *share_mem = 0;
//客户连接数组，进程使用客户连接的编号来索引
client_data *users = 0;
//子进程和客户连接的映射关系表，用进程的pid映射得到客户连接编号
int *sub_process = 0;
int user_count = 0;
bool stop_child = false;

//设置非阻塞
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//向epoll池中新增事件，采用边沿触发模式
void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

//信号发往信号管道，随后epoll一同监听该信号管道，达到统一事件源的目的?
void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

//
void addsig(int sig, void (*handler)(int), bool restart = true)
{
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    if (restart)
    {
        sa.sa_flags |= SA_RESTART;
    }
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

//关闭事件源，用于退出
void del_resource()
{
    close(sig_pipefd[0]);
    close(sig_pipefd[1]);
    close(listenfd);
    close(epollfd);
    shm_unlink(shm_name);
    delete[] users;
    delete[] sub_process;
}

//停止一个子进程
void child_term_handler(int sig)
{
    stop_child = true;
}

//idx待处理的客户编号，users[]所有客户数据，*share_mem指出共享内存的起始地址
int run_child(int idx, client_data *users, char *share_mem)
{
    epoll_event events[MAX_EVENT_NUMBER];
    //子进程使用IO复用技术同时监听两个文件描述符：客户连接socket和与父进程通信的管道文件描述符
    int child_epollfd = epoll_create(5);
    assert(child_epollfd != -1);
    int connfd = users[idx].connfd;
    addfd(child_epollfd, connfd);
    int pipefd = users[idx].pipefd[1];
    addfd(child_epollfd, pipefd);
    int ret;
    //子进程新增终端访问的信号处理函数?
    addsig(SIGTERM, child_term_handler, false);

    while (!stop_child)
    {
        //监听epoll文件描述符上的事件，暂时有两个(客户、管道),监听到后将事件复制到events中
        int number = epoll_wait(child_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        { //-1是错误
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            //如果是该子进程负责的客户连接到来
            if ((sockfd == connfd) && (events[i].events & EPOLLIN))
            {
                //将共享内存区中属于第idx个客户的区域重置
                memset(share_mem + idx * BUFFER_SIZE, '\0', BUFFER_SIZE);
                //将数据读取到对应的读缓存中
                ret = recv(connfd, share_mem + idx * BUFFER_SIZE, BUFFER_SIZE - 1, 0);
                if (ret < 0)
                {
                    if (errno != EAGAIN)
                    {
                        stop_child = true;
                    }
                }
                else if (ret == 0)
                {
                    stop_child = true;
                }
                else
                {
                    //子进程成功读取客户数据后用管道通知主进程处理
                    //不用把数据发过去，因为是共享内存区，只告知主进程数据在共享内存中的哪段区域中即可
                    send(pipefd, (char *)&idx, sizeof(idx), 0);
                }
            }
            //如果收到来自主进程的管道信息(双向管道吗?)
            //客户访问服务器，主进程让子进程处理，子进程获取到客户的请求数据存放到共享内存中并通知主进程在内存的哪一段。
            //主进程接收到请求数据后，将数据填入到共享内存区，再通过管道发给子进程具体是在哪一段区域
            //子进程再发给客户
            else if ((sockfd == pipefd) && (events[i].events & EPOLLIN))
            {
                int client = 0;
                ret = recv(sockfd, (char *)&client, sizeof(client), 0);
                if (ret < 0)
                {
                    if (errno != EAGAIN)
                    {
                        stop_child = true;
                    }
                }
                else if (ret == 0)
                {
                    stop_child = true;
                }
                else
                {
                    send(connfd, share_mem + client * BUFFER_SIZE, BUFFER_SIZE, 0);
                }
            }
            else
            {
                continue;
            }
        }
    }

    close(connfd);
    close(pipefd);
    close(child_epollfd);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);

    //创建那么多子进程做什么？
    user_count = 0;
    users = new client_data[USER_LIMIT + 1];
    sub_process = new int[PROCESS_LIMIT];
    for (int i = 0; i < PROCESS_LIMIT; ++i)
    {
        sub_process[i] = -1;
    }

    epoll_event events[MAX_EVENT_NUMBER];
    epollfd = epoll_create(5); //监听5个事件？服务器端自己一个，消息管道一个，最多5个客户，7个怎么处理？
    assert(epollfd != -1);
    addfd(epollfd, listenfd); //监听服务端socket

    /*普通管道的创建：
       int pipe1[2];
       pipe(pipe1);    
    */
    //创建双向管道sig_pipefd
    ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);
    setnonblocking(sig_pipefd[1]);
    addfd(epollfd, sig_pipefd[0]); //监听消息管道

    addsig(SIGCHLD, sig_handler); //20	Child terminated or stopped.
    addsig(SIGTERM, sig_handler); //15	Termination request.
    addsig(SIGINT, sig_handler);  //2	Interactive attention signal.
    addsig(SIGPIPE, SIG_IGN);     //13	Broken pipe，忽略
    bool stop_server = false;
    bool terminate = false;

    //打开一段共享内存区?shmget()呢？
    shmfd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    assert(shmfd != -1);
    ret = ftruncate(shmfd, USER_LIMIT * BUFFER_SIZE);
    assert(ret != -1);

    share_mem = (char *)mmap(NULL, USER_LIMIT * BUFFER_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shmfd, 0);
    assert(share_mem != MAP_FAILED);
    close(shmfd); //shmfd利用完毕，作用就是打开share_mem

    //运行服务器
    while (!stop_server)
    {
        int number = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            //轮询到服务器端事件，有客户连接到来
            if (sockfd == listenfd)
            {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                if (connfd < 0)
                {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (user_count >= USER_LIMIT)
                {
                    const char *info = "too many users\n";
                    printf("%s", info);
                    send(connfd, info, strlen(info), 0);
                    close(connfd);
                    continue;
                }
                users[user_count].address = client_address;
                users[user_count].connfd = connfd;
                ret = socketpair(PF_UNIX, SOCK_STREAM, 0, users[user_count].pipefd);
                assert(ret != -1);
                pid_t pid = fork();
                if (pid < 0)
                { //fork失败
                    close(connfd);
                    continue;
                }
                else if (pid == 0)
                { //子进程
                    close(epollfd);
                    close(listenfd);
                    close(users[user_count].pipefd[0]);
                    close(sig_pipefd[0]);
                    close(sig_pipefd[1]);
                    run_child(user_count, users, share_mem);
                    munmap((void *)share_mem, USER_LIMIT * BUFFER_SIZE);
                    exit(0);
                }
                else
                { //主进程
                    close(connfd);
                    close(users[user_count].pipefd[1]);
                    addfd(epollfd, users[user_count].pipefd[0]); //增加监听刚刚去处理的子进程消息管道
                    users[user_count].pid = pid;                 //子进程pid
                    sub_process[pid] = user_count;               //子进程pid映射为客户编号
                    user_count++;
                }
            }
            //轮询到消息管道，可能发生的消息为：子进程终止、终端请求、用户输入中止、管道破损
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                //将消息管道的数据复制到signals中
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                { //成功接收到信号数据
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        //子进程退出，表示有某个客户端关闭了连接
                        case SIGCHLD:
                        {
                            pid_t pid;
                            int stat;
                            //阻塞调用，因为不知道刚刚退出的子进程的pid是多少，故pid为-1
                            //又有WNOHANG，非阻塞调用，所以到底是阻塞还是非阻塞?
                            while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                            {
                                int del_user = sub_process[pid]; //映射出下线的用户编号
                                sub_process[pid] = -1;
                                if ((del_user < 0) || (del_user > USER_LIMIT))
                                {
                                    printf("the deleted user was not change\n");
                                    continue;
                                }
                                //从epoll监听事件中，删除监听刚刚下线的客户的管道
                                epoll_ctl(epollfd, EPOLL_CTL_DEL, users[del_user].pipefd[0], 0);
                                close(users[del_user].pipefd[0]);
                                //将最后一个用户替补到刚刚下线的位置上
                                users[del_user] = users[--user_count];
                                sub_process[users[del_user].pid] = del_user; //更新映射
                                printf("child %d exit, now we have %d users\n", del_user, user_count);
                            }
                            if (terminate && user_count == 0)
                            {
                                stop_server = true;
                            }
                            break;
                        }
                        case SIGTERM:
                        case SIGINT:
                        {
                            //结束服务器程序
                            printf("kill all the clild now\n");
                            if (user_count == 0)
                            {
                                stop_server = true;
                                break;
                            }
                            for (int i = 0; i < user_count; ++i)
                            {
                                int pid = users[i].pid;
                                kill(pid, SIGTERM);
                            }
                            terminate = true;
                            break;
                        }
                        default:
                        {
                            break;
                        }
                        }
                    }
                }
            }
            //轮询到客户端管道信息
            else if (events[i].events & EPOLLIN)
            {
                int child = 0;
                ret = recv(sockfd, (char *)&child, sizeof(child), 0);
                printf("read data from child accross pipe。this is child %d\n", child);

                if (ret == -1)
                {
                    continue;
                }
                else if (ret == 0)
                {
                    continue;
                }
                else
                { //成功读取到数据，给其他用户的缓冲区都发送数据，发送的数据是第child块缓冲区的数据
                    for (int j = 0; j < user_count; ++j)
                    {
                        if (users[j].pipefd[0] != sockfd)
                        {
                            printf("send data to child accross pipe\n");
                            send(users[j].pipefd[0], (char *)&child, sizeof(child), 0);
                        }
                    }
                }
            }
        }
    }

    del_resource();
    return 0;
}
