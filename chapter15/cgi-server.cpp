#include "processpool.h"

//用于处理客户CGI请求的类，可以作为processpool类的模板参数
class cgi_conn
{
public:
    //cgi_conn(); //这构造函数啥也没干
    //~cgi_conn();
    //真正的构造函数
    //初始化客户连接，清空读缓冲区。
    void init(int epollfd, int sockfd, const sockaddr_in &client_addr)
    { //子进程的内核事件表、该子进程正在处理的某个客户连接文件描述符、socket地址
        m_epollfd = epollfd;
        m_sockfd = sockfd;
        m_address = client_addr;
        memset(m_buf, '\0', BUFFER_SIZE);
        m_read_idx = 0;
    }
    void process();

private:
    static const int BUFFER_SIZE = 1024; //读缓冲区的大小
    int m_epollfd;
    int m_sockfd;
    sockaddr_in m_address;
    char m_buf[BUFFER_SIZE];
    int m_read_idx; //标记读缓冲中已经读入的客户数据的最后一个字节的下一个位置
};

//子进程委托CGI服务器处理客户请求
//于是在CGI服务器类中有这么一个process()方法
void cgi_conn::process()
{
    int idx = 0;
    int ret = -1;

    while (1)
    {
        //读取客户socket连接上的请求，数据读取至缓冲区m_buf[idx,1023-idx]
        idx = m_read_idx;
        ret = recv(m_sockfd, m_buf + idx, BUFFER_SIZE - 1 - idx, 0);
        //发生错误，或无数据可读
        if (ret < 0)
        {
            if (errno != EAGAIN)
            {
                removefd(m_epollfd, m_sockfd);
            }
            break;
        }
        //对方关闭连接，服务器也关闭连接
        else if (ret == 0)
        {
            removefd(m_epollfd, m_sockfd);
            break;
        }
        //成功读取到客户连接数据
        else
        {
            m_read_idx += ret;
            printf("user content is :%s\n", m_buf);
            //如果遇到"\r\n",则开始处理客户请求
            for (; idx < m_read_idx; ++idx)
            {
                if (idx >= 1 && m_buf[idx - 1] == '\r' && m_buf[idx] == '\n')
                {
                    break;
                }
            }
            if (idx == m_read_idx)
            { //如果运行到这里，说明请求数据之中没有"\r\n",读取下一个BUFFER_SIZE大小的数据(如果有的话)
                //m_buf缓冲区大小为1024，recv接收最大值小于1024或客户发来的数据不连续，需要再读，直到遇到'\r\n'这个结束符
                printf("--进入下一次循环读取数据\n");
                continue; //进入下一次读数据循环
            }
            m_buf[idx - 1] = '\0';

            char *file_name = m_buf;
            //客户请求的程序是否存在
            printf("--filename = %s\n", file_name);
            if (access(file_name, F_OK) == -1)
            {
                removefd(m_epollfd, m_sockfd);
                break;
            }
            //创建子进程来执行CGI程序
            ret = fork();
            if (ret == -1)
            { //fork()失败
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else if (ret > 0)
            { //父进程关闭连接
                removefd(m_epollfd, m_sockfd);
                break;
            }
            else
            { //子进程将标准输出定向到m_sockfd,并执行CGI程序
                printf("--创建子进程执行cgi程序\n");
                close(STDOUT_FILENO);
                dup(m_sockfd);
                execl(m_buf, m_buf, NULL);
                exit(0);
            }
        }
    }
}

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

    //cgi_conn作为模板参数参入进程池类，在run()再到run_child()中
    //声明的客户是CGI对象，在那里就调用了init()和process()函数
    processpool<cgi_conn> *pool = processpool<cgi_conn>::create(listenfd);
    if (pool) //如果进程池创建成功，就启动进程池
    {
        pool->run();
        delete pool;
    }
    close(listenfd);
    return 0;
}