#ifndef PROCESSPOOL_H
#define PROCESSPOOL_H

#include <sys/types.h>
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
#include <sys/stat.h>

//子进程类
class process
{
public:
    //构造函数
    process() : m_pid(-1) {}

public:
    pid_t m_pid;
    int m_pipefd[2];
};

//进程池类
template <typename T>
class processpool
{
private:
    //私有构造函数，只能通过静态函数创建实例?
    processpool(int listenfd, int process_number = 8);

public:
    //单体模式？最多创建一个preocesspool实例？
    static processpool<T> *create(int listenfd, int process_number = 8)
    {
        if (!m_instance)
        {
            m_instance = new processpool<T>(listenfd, process_number);
        }
        return m_instance;
    }
    //析构函数
    ~processpool()
    {
        delete[] m_sub_process;
    }
    //启动进程池
    void run();

private:
    void setup_sig_pipe();
    void run_parent();
    void run_child();

private:
    static const int MAX_PROCESS_NUMBER = 16;  //进程池允许的最大子进程数量
    static const int USER_PER_PROCESS = 65536; //每个子进程最多能处理的客户数量
    static const int MAX_EVENT_NUMBER = 10000; //epoll最多能处理的事件数
    int m_process_number;                      //进程池中的进程总数
    int m_idx;                                 //子进程在池中的序号
    int m_epollfd;                             //每个进程都有一个epoll事件表，用m_epollfd标识
    int m_listenfd;                            //监听socket
    int m_stop;                                //进程是否停止运行
    process *m_sub_process;                    //保存所有子进程的描述信息
    static processpool<T> *m_instance;         //进程池静态实例
};
//静态变量初始化
template <typename T>
processpool<T> *processpool<T>::m_instance = NULL;

//信号管道
static int sig_pipefd[2];

static int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

//向epoll内核事件表中添加事件，边沿触发模式
static void addfd(int epollfd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

static void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

static void sig_handler(int sig)
{
    int save_errno = errno;
    int msg = sig;
    send(sig_pipefd[1], (char *)&msg, 1, 0);
    errno = save_errno;
}

static void addsig(int sig, void(handler)(int), bool restart = true)
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

//进程池构造函数
template <typename T>
processpool<T>::processpool(int listenfd, int process_number)
    : m_listenfd(listenfd), m_process_number(process_number), m_idx(-1), m_stop(false)
{
    assert((process_number > 0) && (process_number <= MAX_PROCESS_NUMBER));

    m_sub_process = new process[process_number];
    assert(m_sub_process);

    //为进程池中的每个进程进行初始化：分配与父进程通信的双向管道、设置pid
    for (int i = 0; i < process_number; ++i)
    {
        int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, m_sub_process[i].m_pipefd);
        assert(ret == 0);

        m_sub_process[i].m_pid = fork();
        assert(m_sub_process[i].m_pid >= 0);
        if (m_sub_process[i].m_pid > 0)
        { //父进程。
            //使用getpid()得到进程号，fork()返回值是子进程pid
            close(m_sub_process[i].m_pipefd[1]); //父进程之后只用m_pipefd[0]进行读写
            continue;
        }
        else
        { //子进程
            //fork()返回值是0，但getpid()得到的是fork()的正返回值
            close(m_sub_process[i].m_pipefd[0]); //子进程之后只用m_pipefd[1]进行读写
            m_idx = i;
            break;
        }
    }
}

//统一事件源
template <typename T>
void processpool<T>::setup_sig_pipe()
{
    m_epollfd = epoll_create(5);
    assert(m_epollfd != -1);

    //创建消息管道
    int ret = socketpair(PF_UNIX, SOCK_STREAM, 0, sig_pipefd);
    assert(ret != -1);

    setnonblocking(sig_pipefd[1]);   //1端非阻塞，暴露给用户、外界、终端等
    addfd(m_epollfd, sig_pipefd[0]); //进程监听0端

    addsig(SIGCHLD, sig_handler);
    addsig(SIGTERM, sig_handler);
    addsig(SIGINT, sig_handler);
    addsig(SIGPIPE, SIG_IGN);
}

//父进程中m_idx都被初始化为-1，子进程中m_idx初始化为子进程的编号
template <typename T>
void processpool<T>::run()
{
    if (m_idx != -1)
    {
        run_child();
        return;
    }
    run_parent();
}

template <typename T>
void processpool<T>::run_child()
{
    //epoll事件1、每个孩子进程都创建消息管道，并监听
    setup_sig_pipe();

    //epoll事件2、只要有子进程存在，说明至少父进程已经创建了子进程，所以必然有消息管道。监听之
    //孩子进程监听父子管道1端
    int pipefd = m_sub_process[m_idx].m_pipefd[1];
    addfd(m_epollfd, pipefd);

    epoll_event events[MAX_EVENT_NUMBER];
    //使用模板的目的就在这
    T *users = new T[USER_PER_PROCESS];
    assert(users);
    int number = 0;
    int ret = -1;

    while (!m_stop)
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            //从父子管道1端监听到父进程的消息
            if ((sockfd == pipefd) && (events[i].events & EPOLLIN))
            {
                int client = 0;
                //？
                //从父子管道读取数据，将结果保存在变量client中，读取成功则表示有新客户连接到来
                ret = recv(sockfd, (char *)&client, sizeof(client), 0);
                if (((ret < 0) && (errno != EAGAIN)) || ret == 0)
                {
                    continue;
                }
                else
                {
                    struct sockaddr_in client_address;
                    socklen_t client_addrlength = sizeof(client_address);
                    int connfd = accept(m_listenfd, (struct sockaddr *)&client_address, &client_addrlength);
                    if (connfd < 0)
                    {
                        printf("errno is: %d\n", errno);
                        continue;
                    }
                    //epoll事件3、连接成功后，子进程开始监听客户连接
                    addfd(m_epollfd, connfd);
                    //这就是模板参数类要实现的方法
                    //让这个模板参数去处理客户连接
                    users[connfd].init(m_epollfd, connfd, client_address);
                }
            }
            //子进程监听到消息管道
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                //从0端接收消息数据
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        //子进程退出
                        case SIGCHLD:
                        {
                            pid_t pid;
                            int stat;
                            while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                            {
                                continue;
                            }
                            break;
                        }
                        //子进程中断
                        case SIGTERM:
                        case SIGINT:
                        {
                            m_stop = true;
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
            //子进程接收到客户消息
            else if (events[i].events & EPOLLIN)
            {
                users[sockfd].process();
            }
            else
            {
                continue;
            }
        }
    }

    delete[] users;
    users = NULL;
    close(pipefd);
    //close( m_listenfd );
    close(m_epollfd);
}

//父进程
template <typename T>
void processpool<T>::run_parent()
{
    //和父子管道不一样，父进程读写0端，子进程读写1端。父子管道连接父子
    //父子进程都会创建消息管道，1端非阻塞监听消息，0端是进程用于读取。消息管道连接进程与外界
    setup_sig_pipe();

    //监听服务器
    addfd(m_epollfd, m_listenfd);

    epoll_event events[MAX_EVENT_NUMBER];
    int sub_process_counter = 0;
    int new_conn = 1;
    int number = 0;
    int ret = -1;

    while (!m_stop) //m_stop标记进程是否中止，不论父子进程
    {
        number = epoll_wait(m_epollfd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR))
        {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; i++)
        {
            int sockfd = events[i].data.fd;
            //监听到服务器
            if (sockfd == m_listenfd)
            {
                //采用Round Robin 方式挑选子进程去处理请求
                int i = sub_process_counter;
                do
                {
                    if (m_sub_process[i].m_pid != -1)
                    { //若该子进程已经启动，那就它了
                        break;
                    }
                    //否则选下一个子进程
                    i = (i + 1) % m_process_number;
                } while (i != sub_process_counter); //只对进程池中的所有进程轮询一轮

                //运行到这，说明所有子进程都还没启动？客户请求还没找到子进程处理？
                if (m_sub_process[i].m_pid == -1)
                {
                    m_stop = true; //m_stop置为true是使得子进程终止啊，本来就没运行终止什么？
                    break;         //服务器崩溃
                }

                //如果运行到这，说明客户请求已经被分配到某个子进程SPi了
                //下次再有客户请求，就从SPi子进程的后一个进程开始轮询
                sub_process_counter = (i + 1) % m_process_number;
                //随便发送一个数据，只是用来告诉子进程，有个请求待处理，无其他含义。或许也有阻塞用于同步的含义
                send(m_sub_process[i].m_pipefd[0], (char *)&new_conn, sizeof(new_conn), 0);
                printf("send request to child %d\n", i);
            }
            //监听消息管道
            else if ((sockfd == sig_pipefd[0]) && (events[i].events & EPOLLIN))
            {
                int sig;
                char signals[1024];
                ret = recv(sig_pipefd[0], signals, sizeof(signals), 0);
                if (ret <= 0)
                {
                    continue;
                }
                else
                {
                    for (int i = 0; i < ret; ++i)
                    {
                        switch (signals[i])
                        {
                        case SIGCHLD:
                        {
                            pid_t pid;
                            int stat;
                            while ((pid = waitpid(-1, &stat, WNOHANG)) > 0)
                            {
                                for (int i = 0; i < m_process_number; ++i)
                                {
                                    if (m_sub_process[i].m_pid == pid)
                                    {
                                        printf("child %d join\n", i);
                                        close(m_sub_process[i].m_pipefd[0]);
                                        m_sub_process[i].m_pid = -1;
                                    }
                                }
                            }
                            m_stop = true;
                            for (int i = 0; i < m_process_number; ++i)
                            {
                                if (m_sub_process[i].m_pid != -1)
                                {
                                    m_stop = false;
                                }
                            }
                            break;
                        }
                        case SIGTERM:
                        case SIGINT:
                        {
                            printf("kill all the clild now\n");
                            for (int i = 0; i < m_process_number; ++i)
                            {
                                int pid = m_sub_process[i].m_pid;
                                if (pid != -1)
                                {
                                    kill(pid, SIGTERM);
                                }
                            }
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
            else
            {
                continue;
            }
        }
    }

    //close( m_listenfd );
    close(m_epollfd);
}

#endif
