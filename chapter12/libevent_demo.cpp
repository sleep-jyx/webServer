#include <sys/signal.h>
#include <event.h>
//信号处理函数
void signal_cb(int fd, short event, void *argc)
{
    struct event_base *base = (event_base *)argc;
    struct timeval delay = {2, 0};
    printf("caught an interrupt signal;exiting cleanly in two seconds...\n");
    event_base_loopexit(base, &delay);
}

//定时事件处理函数
void timeout_cb(int fd, short event, void *argc)
{
    printf("timeout\n");
}

int main()
{
    struct event_base *base = event_init(); //创建event_base对象，相当于一个reactor实例

    //创建信号事件处理器
    struct event *signal_event = evsignal_new(base, SIGINT, signal_cb, base);
    event_add(signal_event, NULL);

    //创建定时事件处理器
    timeval tv = {1, 0};
    struct event *timeout_event = evtimer_new(base, timeout_cb, NULL);
    event_add(timeout_event, &tv);

    event_base_dispatch(base);

    event_free(timeout_event);
    event_free(signal_event);
    event_base_free(base);
}
