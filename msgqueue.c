#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "msgqueue.h"

struct __msgqueue
{
    size_t msg_max;  // 队列的最大消息数
    size_t msg_cnt;  // 当前消息数
    int linkoff;
    int nonblock;
    void *head1;
    void *head2;
    void **get_head;
    void **put_head;
    void **put_tail;
    pthread_mutex_t get_mutex;
    pthread_mutex_t put_mutex;
    pthread_cond_t get_cond;
    pthread_cond_t put_cond;
};
