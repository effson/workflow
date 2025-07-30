#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "msgqueue.h"

/*
 * put队列: head2/put_head/put_tail,存储生产者提交的待消费消息,是生产者往里放消息的地方
 * get队列: head2/get_head,消费者从中取消息的地方
 *
 * 采用双队列设计，实现高效多线程通信：
 * 降低锁竞争，put和get分别用各自的锁互不干扰
 */
struct __msgqueue
{
    size_t msg_max;  // 队列的最大消息数
    size_t msg_cnt;  // 当前消息数
    int linkoff; //从消息结构体中取出“下一个”指针的偏移，用于构建链表
    int nonblock; // 非阻塞标志
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
