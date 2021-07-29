#include "http_conn.h"

//定义HTTP相应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to satisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file from this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the requested file.\n";
//网站资源根目录
const char *doc_root = "/home/jyx/文档/unix/linuxServer/root";

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool one_shot)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot)
    {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

void modfd(int epollfd, int fd, int ev)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

int http_conn::m_user_count = 0; //客户连接数量
int http_conn::m_epollfd = -1;   //统一的内核事件表

//关闭与客户的连接
void http_conn::close_conn(bool real_close)
{
    if (real_close && (m_sockfd != -1))
    {
        removefd(m_epollfd, m_sockfd); //将该客户连接移出epoll
        m_sockfd = -1;
        m_user_count--; //客户连接数量-1
    }
}

//初始化与客户的连接
void http_conn::init(int sockfd, const sockaddr_in &addr)
{
    printf("--debug--:http_conn对象init(sockfd,addr)\n");
    m_sockfd = sockfd;
    m_address = addr;
    //下面这几行做了什么处理？
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(m_sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
    int reuse = 1;
    setsockopt(m_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    //将该客户连接加入epoll，只能有一个线程触发
    addfd(m_epollfd, sockfd, true);
    m_user_count++;

    init(); //不是递归，是下面这个初始化的重载
}

void http_conn::init()
{
    printf("--debug--:重置http_conn状态，客户连接数量=%d\n", m_user_count);
    m_check_state = CHECK_STATE_REQUESTLINE; //主状态机方法
    m_linger = false;                        //http请求是否保持连接

    m_method = GET;
    m_url = 0;
    m_version = 0;
    m_content_length = 0;
    m_host = 0;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, '\0', READ_BUFFER_SIZE);
    memset(m_write_buf, '\0', WRITE_BUFFER_SIZE);
    memset(m_real_file, '\0', FILENAME_LEN); //请求目标文件的完整路径
}

//解析请求的一行
http_conn::LINE_STATUS http_conn::parse_line()
{
    char temp;
    //从当前分析位置分析到已读入数据的最后一个字节之前的位置
    for (; m_checked_idx < m_read_idx; ++m_checked_idx)
    {
        temp = m_read_buf[m_checked_idx];

        if (temp == '\r')
        { //如果最后一个字符是'\r'，那么它应该还有其他数据，进入LINE_OPEN状态
            if ((m_checked_idx + 1) == m_read_idx)
            {
                return LINE_OPEN;
            }
            //如果正好读完一行，以'\r\n'结尾，进入LINE_OK状态
            else if (m_read_buf[m_checked_idx + 1] == '\n')
            {
                m_read_buf[m_checked_idx++] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            //否则转入LINE_BAD状态
            return LINE_BAD;
        }
        else if (temp == '\n')
        {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r'))
            {
                m_read_buf[m_checked_idx - 1] = '\0';
                m_read_buf[m_checked_idx++] = '\0';
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    //运行到这，说明不是上述的情况，数据还需要再读
    return LINE_OPEN;
}

//非阻塞读，循环读客户数据，直到无数据可读或者对方关闭连接
bool http_conn::read()
{
    if (m_read_idx >= READ_BUFFER_SIZE)
    { //如果读入的数据超出了缓冲区的大小，返回false
        return false;
    }

    int bytes_read = 0;
    while (true)
    {
        //从客户连接sockfd读取请求数据
        bytes_read = recv(m_sockfd, m_read_buf + m_read_idx, READ_BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                break;
            }
            return false;
        }
        else if (bytes_read == 0)
        {
            return false;
        }
        //开始下一次的读循环
        m_read_idx += bytes_read;
    }

    //只有发生无数据可读才会运行到这，返回true
    return true;
}

//解析HTTP请求行，获得请求方法、目标URL，一级HTTP版本号
http_conn::HTTP_CODE http_conn::parse_request_line(char *text)
{
    //请求数据第一行长这样： GET    /   HTTP/1.1,分为三部分，分别是请求方法、url、协议
    m_url = strpbrk(text, " \t"); //返回第一次匹配到' \t'之后的字符串
    if (!m_url)
    {
        return BAD_REQUEST;
    }
    *m_url++ = '\0'; //以'\0'分隔开GET和url，char*类型遇到'\0'截断

    char *method = text;
    if (strcasecmp(method, "GET") == 0)
    {
        m_method = GET;
    }
    else
    {
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t"); //匹配掉方法和url之间的空格
    m_version = strpbrk(m_url, " \t");
    if (!m_version)
    {
        return BAD_REQUEST;
    }
    *m_version++ = '\0';
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0)
    {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0)
    {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/')
    {
        return BAD_REQUEST;
    }

    /*
    要实现页面跳转，就改m_url
    */
    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

//解析HTTP头部，响应头？
http_conn::HTTP_CODE http_conn::parse_headers(char *text)
{
    if (text[0] == '\0')
    {
        if (m_method == HEAD)
        {
            return GET_REQUEST;
        }

        if (m_content_length != 0)
        {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        printf("--debug--:没有内容可以处理了\n");
        return GET_REQUEST;
    }
    else if (strncasecmp(text, "Connection:", 11) == 0)
    {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0)
        {
            m_linger = true;
        }
        printf("--debug--:解析出连接状态\n");
    }
    else if (strncasecmp(text, "Content-Length:", 15) == 0)
    {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    }
    else if (strncasecmp(text, "Host:", 5) == 0)
    {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
        printf("--debug--:解析出host\n");
    }
    else
    {
        int sep = strcspn(text, ":");
        //printf("--debug--:sep=%d\n", sep);
        *(text + sep) = '\0';
        printf("--debug-- 尚未实现的头部字段%s的解析\n", text);
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::parse_content(char *text)
{
    if (m_read_idx >= (m_content_length + m_checked_idx))
    {
        text[m_content_length] = '\0';
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::process_read()
{
    printf("--debug--:preocess_read()处理客户请求\n");
    LINE_STATUS line_status = LINE_OK; //行状态
    HTTP_CODE ret = NO_REQUEST;
    char *text = 0;

    //进入循环条件：(主状态机正在处理内容 且 当前行状态ok) 或者 下一行状态ok就继续读下一行。
    //跳出循环条件：(主状态机在处理头部或请求，或当前行不OK) 且 下一行状态不OK
    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK))
    {
        text = get_line();
        m_start_line = m_checked_idx;
        printf("got 1 http line:%s\n", text);

        switch (m_check_state) //主状态机
        {
        case CHECK_STATE_REQUESTLINE:
        {
            printf("--debug--:处理requestLine状态\n");
            ret = parse_request_line(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER:
        {
            printf("--debug--:处理header状态\n");
            ret = parse_headers(text);
            if (ret == BAD_REQUEST)
            {
                return BAD_REQUEST;
            }
            else if (ret == GET_REQUEST)
            {
                //处理请求
                printf("--debug--:已经解析完了需要的信息，转dorequest()开始处理请求\n");
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT:
        {
            printf("--debug--:处理content状态\n");
            ret = parse_content(text);
            if (ret == GET_REQUEST)
            {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        default:
        {
            return INTERNAL_ERROR;
        }
        }
    }

    return NO_REQUEST;
}

http_conn::HTTP_CODE http_conn::do_request()
{
    //根路径+url拼接为完整路径
    strcpy(m_real_file, doc_root);
    int len = strlen(doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);
    printf("--debug--:请求文件完整路径%s\n", m_real_file);
    //判断文件是否存在
    if (stat(m_real_file, &m_file_stat) < 0)
    {
        return NO_RESOURCE;
    }
    //权限
    if (!(m_file_stat.st_mode & S_IROTH))
    {
        return FORBIDDEN_REQUEST;
    }
    //文件是目录
    if (S_ISDIR(m_file_stat.st_mode))
    {
        return BAD_REQUEST;
    }
    //真的是文件，以只读方式获取文件描述符
    int fd = open(m_real_file, O_RDONLY);
    //将磁盘文件映射到内存进程空间
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    printf("--debug--:将磁盘文件映射到内存空间\n");
    close(fd);
    return FILE_REQUEST;
}

//删除映射
void http_conn::unmap()
{
    if (m_file_address)
    {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool http_conn::write()
{
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        init();
        return true;
    }

    while (1)
    {
        //集中写，m_iv是结构体数组，{内存地址,内存块大小}[m_iv_count]。写入客户socket连接
        temp = writev(m_sockfd, m_iv, m_iv_count);
        if (temp <= -1)
        {
            if (errno == EAGAIN)
            {
                modfd(m_epollfd, m_sockfd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }
        printf("--debug--:write()成功将内存块集中写入客户socket连接\n");
        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= bytes_have_send)
        {
            unmap();
            if (m_linger)
            {
                init();
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return true;
            }
            else
            {
                modfd(m_epollfd, m_sockfd, EPOLLIN);
                return false;
            }
        }
    }
}

bool http_conn::add_response(const char *format, ...)
{
    if (m_write_idx >= WRITE_BUFFER_SIZE)
    {
        return false;
    }
    va_list arg_list;           //解决变参问题
    va_start(arg_list, format); //对arg_list进行初始化，指向可变参数的第一个参数
    int len = vsnprintf(m_write_buf + m_write_idx, WRITE_BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (WRITE_BUFFER_SIZE - 1 - m_write_idx))
    {
        return false;
    }
    m_write_idx += len;
    printf("--debug--:m_write_idx = %d\n", m_write_idx);
    va_end(arg_list);
    return true;
}

bool http_conn::add_status_line(int status, const char *title)
{
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool http_conn::add_headers(int content_len)
{
    add_content_length(content_len);
    add_linger();
    add_blank_line();
}

bool http_conn::add_content_length(int content_len)
{
    return add_response("Content-Length: %d\r\n", content_len);
}

bool http_conn::add_linger()
{
    return add_response("Connection1: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool http_conn::add_blank_line()
{
    return add_response("%s", "\r\n");
}

bool http_conn::add_content(const char *content)
{
    return add_response("%s", content);
}

bool http_conn::process_write(HTTP_CODE ret)
{
    printf("--debug--:preocess_write()\n");
    switch (ret)
    {
    case INTERNAL_ERROR:
    {
        add_status_line(500, error_500_title);
        add_headers(strlen(error_500_form));
        if (!add_content(error_500_form))
        {
            return false;
        }
        break;
    }
    case BAD_REQUEST:
    {
        add_status_line(400, error_400_title);
        add_headers(strlen(error_400_form));
        if (!add_content(error_400_form))
        {
            return false;
        }
        break;
    }
    case NO_RESOURCE:
    {
        add_status_line(404, error_404_title);
        add_headers(strlen(error_404_form));
        if (!add_content(error_404_form))
        {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST:
    {
        add_status_line(403, error_403_title);
        add_headers(strlen(error_403_form));
        if (!add_content(error_403_form))
        {
            return false;
        }
        break;
    }
    case FILE_REQUEST:
    {
        printf("--debug--:preocess_write(),请求文件\n");
        add_status_line(200, ok_200_title);
        add_response("justForTest: %s\r\n", "hello"); //要在加blank_line之前加上，否则就不是加入头部而是加入主体了
        if (m_file_stat.st_size != 0)
        {
            add_headers(m_file_stat.st_size);
            //写缓冲区保存的是响应头部字段,注意其中字段content-Length是请求的文件长度，不包含响应头的长度
            //有点二级映射的意思
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            //客户连接请求的磁盘文件的内存映射
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }
        else
        {
            const char *ok_string = "<html><body></body></html>";
            add_headers(strlen(ok_string));
            if (!add_content(ok_string))
            {
                return false;
            }
        }
    }
    default:
    {
        return false;
    }
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void http_conn::process()
{
    printf("--debug--:process()\n");
    HTTP_CODE read_ret = process_read();
    printf("--debug--:process()解析HTTP请求完毕，开始填充HTTP应答\n");
    if (read_ret == NO_REQUEST)
    {
        modfd(m_epollfd, m_sockfd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret)
    {
        close_conn();
    }

    modfd(m_epollfd, m_sockfd, EPOLLOUT);
}
