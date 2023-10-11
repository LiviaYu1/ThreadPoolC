#pragma once
#include "pthread.h"
#include<stdlib.h>
#include<cstdio>
#include<string.h>
#include<unistd.h>

typedef struct ThreadPool ThreadPool;

//�����̳߳ز���ʼ��
ThreadPool* threadPoolCreate(int min,int max,int queueSize);

//�����̳߳صĺ���
int threadPoolDestroy(ThreadPool* pool);

//�������ĺ���
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

//��ȡ�������̵߳ĸ���

int threadPoolBusyNum(ThreadPool* pool);

//��ȡ���ŵ��̵߳ĸ���

int threadPoolAliveNum(ThreadPool* pool);

////////////////////////
void* worker(void* arg);
void* manager(void* arg);
void threadExit(ThreadPool* pool);

