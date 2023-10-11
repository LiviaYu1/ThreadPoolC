#include"threadpool.h"

const int NUMBER = 2;
//����ṹ��
typedef struct Task
{
	void (*function)(void* arg);//��void* ��һ�ַ��ͣ���Ӧ�������
	void* arg;
}Task;

//�̳߳ؽṹ��
struct ThreadPool
{
	//������У����Task������
	Task* taskQ;
	//�̳߳ص�����
	int queueCapacity;//����
	int queueSize; //�������
	int queueFront;//���� ȡԪ��
	int queueRear;//��β����Ԫ��

	pthread_t managerID; //�������߳�
	pthread_t* threadIDs; //�������߳�
	int minNum;	//��С�߳�
	int maxNum;	//����߳�
	int busyNum; //��ǰ�����̸߳���
	int liveNum; //����߳�
	int exitNum; //Ҫɱ�����߳�����

	//������Щ����Ҫ��
	pthread_mutex_t mutexPool; //�������̳߳�
	pthread_mutex_t mutexBusy; //�ڹ��������У�busynumһֱ�ڷ����仯���������޸�ʱ��Ҫ����
	
	int shutdown; //�Ƿ�Ҫ�����̳߳أ�����Ϊ1��������Ϊ0

	//������������������������������
	pthread_cond_t notFull;	//��������Ƿ�����
	pthread_cond_t notEmpty; //��������Ƿ�Ϊ����


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
		pool->liveNum = min;//��ʼ��ʱĬ�ϴ���min���߳�
		pool->exitNum = 0;

		if (pthread_mutex_init(&pool->mutexBusy, NULL) != 0 ||
			pthread_mutex_init(&pool->mutexPool, NULL) != 0 ||
			pthread_cond_init(&pool->notEmpty, NULL) != 0 ||
			pthread_cond_init(&pool->notFull, NULL) != 0)
		{
			printf("mutex or cond init fail...\n");
			break;
		}
		//�������
		pool->taskQ = (Task*)malloc(sizeof(Task) * queueSize);
		pool->queueCapacity = queueSize;
		pool->queueSize = 0;
		pool->queueFront = 0;
		pool->queueRear = 0;

		pool->shutdown = 0;

		//���������ߺ͹������߳�
		//��Ҫ���ݵ�ǰ�߳�״���ͱ����������̣߳�����Ҳ��Ҫ����pool
		pthread_create(&pool->managerID, NULL, manager, pool);
		for (int i = 0; i < min; i++)
		{
			//������Ҫ��worker�ڷ��ʵõ�taskQ���汣��Ļص����������Ծʹ���pool
			pthread_create(&pool->threadIDs[i], NULL, worker, pool);
		}
		return pool;

	} while (0);
	//�������ʧ���ˣ��ͻ�break������Ϳ��Խ�����Դ���ͷ�
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
	//�ر��̳߳�
	pool->shutdown = 1;
	//�������չ������߳�
	pthread_join(pool->managerID, NULL);
	//����������������
	for (int i = 0; i < pool->liveNum; i++)
	{
		pthread_cond_signal(&pool->notEmpty);
	}
	//�ͷŶ��ڴ�
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
		//���˾�����������
		pthread_cond_wait(&pool->notFull, &pool->mutexPool);
	}
	if (pool->shutdown)
	{
		pthread_mutex_unlock(&pool->mutexPool);
		return;
	}
	//������񵽶�β
	pool->taskQ[pool->queueRear].function = func;
	pool->taskQ[pool->queueRear].arg = arg;
	pool->queueRear = (pool->queueRear + 1) % pool->queueCapacity;
	pool->queueSize++;
	//�����Ʒ����Ҫ�������������������ϵ��������߳�
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
		//��ͣ�Ķ�������У��������������Ҫ����
		pthread_mutex_lock(&pool->mutexPool);
		// �ж���������Ƿ�Ϊ��,Ϊ��������
		while (pool->queueSize == 0 && !pool->shutdown)
		{
			pthread_cond_wait(&pool->notEmpty, &pool->mutexPool);
			//�ж��ǲ���Ҫ�����߳�
			if (pool->exitNum > 0)
			{
				pool->exitNum--;
				pool->liveNum--;
				//Ϊ�˱������������������߳���Ҫ�ͷ���
				pthread_mutex_unlock(&pool->mutexPool);
				threadExit(pool);
			}
		}
		//�ж��̳߳��Ƿ�ر���
		if (pool->shutdown)
		{
			pthread_mutex_unlock(&pool->mutexPool);
			threadExit(pool);
		}
		//�ж���ϣ���ʼ������ȡ��������
		
		Task task;
		task.function = pool->taskQ[pool->queueFront].function;
		task.arg = pool->taskQ[pool->queueFront].arg;
		//�ƶ�queuefront�����ζ��У�����һ��������
		pool->queueFront = (pool->queueFront + 1) % pool->queueCapacity;
		pool->queueSize--;
		//ͬ����������Ҫ����������
		pthread_cond_signal(&pool->notFull);
		//����
		pthread_mutex_unlock(&pool->mutexPool);

		//ִ������
		//busynum+1
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum++;
		pthread_mutex_unlock(&pool->mutexBusy);
		//����ִ��
		task.function(task.arg);
		printf("thread &ld end working...\n", pthread_self());
		//������-1
		pthread_mutex_lock(&pool->mutexBusy);
		pool->busyNum--;
		pthread_mutex_unlock(&pool->mutexBusy);


	}
	return NULL;
}

void* manager(void* arg)
{
	//ͬ���Ƚ������pool��һ������ת��
	ThreadPool* pool = (ThreadPool*)arg;
	//�͹����߲�ͬ����������Ҫһֱȥ����
	//����������Ҫ��һ��Ƶ��ȥ���״̬���ж�̬����
	while (!pool->shutdown)
	{
		//ÿ3s���һ��
		sleep(3);
		//ȡ���̳߳���������������سǵ�����
		//��Ҫ����
		pthread_mutex_lock(&pool->mutexPool);
		int queueSize = pool->queueSize;
		int liveNum = pool->liveNum;
		pthread_mutex_unlock(&pool->mutexPool);

		//ȡ��æ���߳��������Էŵ����������
		pthread_mutex_lock(&pool->mutexBusy);
		int busyNum = pool->busyNum;
		pthread_mutex_unlock(&pool->mutexBusy);

		//����߳�
		//����ĸ���>�����̸߳���&&�Ҵ����߳���<����߳���(�����̳߳�û��
		if (queueSize > liveNum && liveNum < pool->maxNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			int counter = 0;
			//���ӵ��߳�ҪС���̳߳ص����������������С��Ԥ���NUMBERֵ
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

		//�����߳�
		//æ���߳�*2<�����߳����Ҵ����߳�>��С�߳���
		if (busyNum * 2 < liveNum && liveNum > pool->minNum)
		{
			pthread_mutex_lock(&pool->mutexPool);
			pool->exitNum = NUMBER;
			pthread_mutex_unlock(&pool->mutexPool);
			//�ù������߳���ɱ������ɱ���߳�
			//���������������������̣߳������������Ѳ�ִ����ɱ������
			for (int i = 0; i < NUMBER; i++)
			{
				//��Ϊֻ��һ���߳�������������������signal broadcast����ͬ��Ч��
				//�����߳��˳��������⣬���ڣ��޷����߳�����threadIDs������0
				//������Ҫһ���µĺ���
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