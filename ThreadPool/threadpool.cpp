#include"threadpool.h"

const int NUMBER = 2;
//任务结构体
typedef struct Task
{
	void (*function)(void* arg);//用void* 是一种泛型，适应更多情况
	void* arg;
}Task;

//线程池结构体
struct ThreadPool
{
	//任务队列，多个Task的数组
	Task* taskQ;
	//线程池的属性
	int queueCapacity;//容量
	int queueSize; //任务个数
	int queueFront;//队首 取元素
	int queueRear;//队尾，放元素

	pthread_t managerID; //管理者线程
	pthread_t* threadIDs; //工作者线程
	int minNum;	//最小线程
	int maxNum;	//最大线程
	int busyNum; //当前工作线程个数
	int liveNum; //存活线程
	int exitNum; //要杀死的线程数量

	//除了这些还需要锁
	pthread_mutex_t mutexPool; //锁整个线程池
	pthread_mutex_t mutexBusy; //在工作过程中，busynum一直在发生变化，所以在修改时需要上锁
	
	int shutdown; //是否要销毁线程池，销毁为1，不销毁为0

	//条件变量，用于阻塞生产者消费者
	pthread_cond_t notFull;	//任务队列是否满了
	pthread_cond_t notEmpty; //任务队列是否为空了


};

ThreadPool* threadPoolCreate(int min, int max, int queueSize) 
{
	ThreadPool* pool = (ThreadPool*)malloc(sizeof(ThreadPool));
	do {
		if (pool == NULL)
		{
			printf("malloc threadpool fail...\n");
			break;
		}
		pool->threadIDs = (pthread_t*)malloc(sizeof(pthread_t) * max);
		if (pool->threadIDs == NULL)
		{
			printf("malloc threadIDs fail...\n");
			break;
		}
		memset(pool->threadIDs, 0, sizeof(pthread_t) * max);
		pool->minNum = min;
		pool->maxNum = max;
		pool->busyNum = 0;
		pool->liveNum = min;//初始化时默认创建min个线程
		pool->exitNum = 0;

		if (pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or cond init fail...\n");
			break;
		}
		//任务队列
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;

		//创建管理者和工作者线程
		//需要根据当前线程状况和比例来管理线程，所以也需要传入pool
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; i++)
		{
			//我们需要在worker内访问得到taskQ里面保存的回调函数，所以就传入pool
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}
		return pool;

	} while (0);
	//如果创建失败了，就会break到这里，就可以进行资源的释放
	if (pool->threadIDs) free(pool->threadIDs);
	if (pool->taskQ) free(pool->taskQ);
	if (pool) free(pool);

	return NULL;
}

int threadPoolDestroy(ThreadPool* pool)
{
	if (pool == NULL)
	{
		return -1;
	}
	//关闭线程池
	pool->shutdown = 1;
	//阻塞回收管理者线程
	pthread_join(pool->managerID, NULL);
	//唤醒阻塞的消费者
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	//释放堆内存
	if (pool->taskQ)
	{
		free(pool->taskQ);
	}
	if (pool->threadIDs)
	{
		free(pool->threadIDs);
	}

	pthread_mutex_destroy(&pool->mutexPool);
	pthread_mutex_destroy(&pool->mutexBusy);
	pthread_cond_destroy(&pool->notEmpty);
	pthread_cond_destroy(&pool->notFull);
	free(pool);
	
	pool = NULL;

	return 0;
}

void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg)
{
	pthread_mutex_lock(&pool->mutexPool);
	while (pool->queueSize==pool->queueCapacity&&!pool->shutdown)
	{
		//满了就阻塞生产者
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//添加任务到队尾
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;
	//加入产品后需要唤醒阻塞在条件变量上的消费者线程
	pthread_cond_signal(&pool->notEmpty);

	pthread_mutex_unlock(&pool->mutexPool);
}

int threadPoolBusyNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexBusy);
	int busyNum = pool->busyNum;
	pthread_mutex_unlock(&pool->mutexBusy);
	return busyNum;
}

int threadPoolAliveNum(ThreadPool* pool)
{
	pthread_mutex_lock(&pool->mutexPool);
	int aliveNum = pool->liveNum;
	pthread_mutex_unlock(&pool->mutexPool);
	return aliveNum;
}

void* worker(void* arg)
{
	ThreadPool* pool = (ThreadPool*)arg;
	while (1)
	{
		//不停的读任务队列，获得任务，所以需要加锁
		pthread_mutex_lock(&pool->mutexPool);
		// 判断任务队列是否为空,为空则阻塞
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
			//判断是不是要销毁线程
			if (pool->exitNum > 0)
			{
				pool->exitNum--;
				pool->liveNum--;
				//为了避免死锁，抢到锁的线程需要释放锁
				pthread_mutex_unlock(&pool->mutexPool);
				threadExit(pool);
			}
		}
		//判断线程池是否关闭了
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}
		//判断完毕，开始正常的取任务消费
		
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		//移动queuefront，环形队列，到下一个任务上
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		//同样在这里需要唤醒生产者
		pthread_cond_signal(&pool->notFull);
		//销毁
		pthread_mutex_unlock(&pool->mutexPool);

		//执行任务
		//busynum+1
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		//任务执行
		task.function(task.arg);
		printf("thread &ld end working...\n", pthread_self());
		//结束后-1
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);


	}
	return NULL;
}

void* manager(void* arg)
{
	//同样先将传入的pool做一次类型转换
	ThreadPool* pool = (ThreadPool*)arg;
	//和工作者不同，工作者需要一直去工作
	//而管理者需要以一种频率去检测状态进行动态调节
	while (!pool->shutdown)
	{
		//每3s检测一次
		sleep(3);
		//取出线程池中任务的数量和县城的数量
		//需要加锁
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//取出忙的线程数量可以放到上面的锁中
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//添加线程
		//任务的个数>存活的线程个数&&且存活的线程数<最大线程数(代表线程池没满
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			//增加的线程要小于线程池的最大容量个数，且小于预设的NUMBER值
			for (int i = 0; pool->liveNum < pool->maxNum && i < pool->maxNum && counter < NUMBER; i++)
			{
				if (pool->threadIDs[i] == 0)
				{
					pthread_create(&pool->threadIDs[i], NULL, worker, pool);
					counter++;
					pool->liveNum++;
				}
			}
			pthread_mutex_unlock(&pool->mutexPool);
		}

		//销毁线程
		//忙的线程*2<存活的线程数且存活的线程>最小线程数
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//让工作的线程自杀而不是杀死线程
			//存在阻塞在条件变量的线程，可以主动唤醒并执行自杀的任务
			for (int i = 0; i < NUMBER; i++)
			{
				//因为只有一个线程能抢到互斥锁，所以signal broadcast是相同的效果
				//但是线程退出中有问题，由于，无法将线程数组threadIDs重新置0
				//所以需要一个新的函数
				pthread_cond_signal(&pool->notEmpty);
				
			}

		}
	}
	return NULL;
}

void threadExit(ThreadPool* pool)
{
	pthread_t tid = pthread_self();
	for (int i = 0; i < pool->maxNum; i++)
	{
		if (pool->threadIDs[i] == tid)
		{
			pool->threadIDs[i] = 0;
			printf("threadExit() called, &ld exit...\n", tid);
		}
	}
	pthread_exit(NULL);
	return;
}