#include <errno.h>
#include <stdlib.h>
#include <pthread.h>
#include "msgqueue.h"

/*
 * put队列: head2/put_head/put_tail,存储生产者提交的待消费消息,是生产者往里放消息的地方
 * get队列: head2/get_head,消费者从中取消息的地方
 *
 * 采用双队列设计，实现高效多线程通信：
 * 降低锁竞争，put和get分别用各自的锁互不干扰，减少了碰撞的概率
 * 多对多转化为一对多，有多个put和get的mutex和cond
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

/*队列元素通用性设计，使用void*，节点偏移linkoff之后有一个指针类型用于连接下一个 */
void msgqueue_set_nonblock(msgqueue_t *queue)
{
	queue->nonblock = 1;
	pthread_mutex_lock(&queue->put_mutex);
	pthread_cond_signal(&queue->get_cond);
	pthread_cond_broadcast(&queue->put_cond);
	pthread_mutex_unlock(&queue->put_mutex);
}

void msgqueue_set_block(msgqueue_t *queue)
{
	queue->nonblock = 0;
}

void msgqueue_put(void *msg, msgqueue_t *queue)
{
	void **link = (void **)((char *)msg + queue->linkoff);

	*link = NULL; // 一维指针转化为二维指针，相当于 next指针为NULL;
	pthread_mutex_lock(&queue->put_mutex);
	while (queue->msg_cnt > queue->msg_max - 1 && !queue->nonblock)
		pthread_cond_wait(&queue->put_cond, &queue->put_mutex);

	*queue->put_tail = link;
	queue->put_tail = link;
	queue->msg_cnt++;
	pthread_mutex_unlock(&queue->put_mutex);
	pthread_cond_signal(&queue->get_cond);
}

void msgqueue_put_head(void *msg, msgqueue_t *queue)
{
	void **link = (void **)((char *)msg + queue->linkoff);

	pthread_mutex_lock(&queue->put_mutex);
	while (*queue->get_head)
	{
		if (pthread_mutex_trylock(&queue->get_mutex) == 0)
		{
			pthread_mutex_unlock(&queue->put_mutex);
			*link = *queue->get_head;
			*queue->get_head = link;
			pthread_mutex_unlock(&queue->get_mutex);
			return;
		}
	}

	while (queue->msg_cnt > queue->msg_max - 1 && !queue->nonblock)
		pthread_cond_wait(&queue->put_cond, &queue->put_mutex);

	*link = *queue->put_head;
	if (*link == NULL)
		queue->put_tail = link;

	*queue->put_head = link;
	queue->msg_cnt++;
	pthread_mutex_unlock(&queue->put_mutex);
	pthread_cond_signal(&queue->get_cond);
}

static size_t __msgqueue_swap(msgqueue_t *queue)
{
	void **get_head = queue->get_head;
	size_t cnt;

	pthread_mutex_lock(&queue->put_mutex);
	while (queue->msg_cnt == 0 && !queue->nonblock)
		pthread_cond_wait(&queue->get_cond, &queue->put_mutex);

	cnt = queue->msg_cnt;
	if (cnt > queue->msg_max - 1)
		pthread_cond_broadcast(&queue->put_cond);

	queue->get_head = queue->put_head;
	queue->put_head = get_head;
	queue->put_tail = get_head;
	queue->msg_cnt = 0;
	pthread_mutex_unlock(&queue->put_mutex);
	return cnt;
}

void *msgqueue_get(msgqueue_t *queue)
{
	void *msg;

	pthread_mutex_lock(&queue->get_mutex);
	if (*queue->get_head || __msgqueue_swap(queue) > 0)
	{
		msg = (char *)*queue->get_head - queue->linkoff;
		*queue->get_head = *(void **)*queue->get_head;
	}
	else
		msg = NULL;

	pthread_mutex_unlock(&queue->get_mutex);
	return msg;
}

msgqueue_t *msgqueue_create(size_t maxlen, int linkoff)
{
	msgqueue_t *queue = (msgqueue_t *)malloc(sizeof (msgqueue_t));
	int ret;

	if (!queue)
		return NULL;

	ret = pthread_mutex_init(&queue->get_mutex, NULL);
	if (ret == 0)
	{
		ret = pthread_mutex_init(&queue->put_mutex, NULL);
		if (ret == 0)
		{
			ret = pthread_cond_init(&queue->get_cond, NULL);
			if (ret == 0)
			{
				ret = pthread_cond_init(&queue->put_cond, NULL);
				if (ret == 0)
				{
					queue->msg_max = maxlen;
					queue->linkoff = linkoff;
					queue->head1 = NULL;
					queue->head2 = NULL;
					queue->get_head = &queue->head1;
					queue->put_head = &queue->head2;
					queue->put_tail = &queue->head2;
					queue->msg_cnt = 0;
					queue->nonblock = 0;
					return queue;
				}

				pthread_cond_destroy(&queue->get_cond);
			}

			pthread_mutex_destroy(&queue->put_mutex);
		}

		pthread_mutex_destroy(&queue->get_mutex);
	}

	errno = ret;
	free(queue);
	return NULL;
}

void msgqueue_destroy(msgqueue_t *queue)
{
	pthread_cond_destroy(&queue->put_cond);
	pthread_cond_destroy(&queue->get_cond);
	pthread_mutex_destroy(&queue->put_mutex);
	pthread_mutex_destroy(&queue->get_mutex);
	free(queue);
}
