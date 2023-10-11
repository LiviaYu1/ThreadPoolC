#pragma once
#include "pthread.h"
#include<stdlib.h>
#include<cstdio>
#include<string.h>
#include<unistd.h>

typedef struct ThreadPool ThreadPool;

//创建线程池并初始化
ThreadPool* threadPoolCreate(int min,int max,int queueSize);

//销毁线程池的函数
int threadPoolDestroy(ThreadPool* pool);

//添加任务的函数
void threadPoolAdd(ThreadPool* pool, void(*func)(void*), void* arg);

//获取工作的线程的个数

int threadPoolBusyNum(ThreadPool* pool);

//获取活着的线程的个数

int threadPoolAliveNum(ThreadPool* pool);

////////////////////////
void* worker(void* arg);
void* manager(void* arg);
void threadExit(ThreadPool* pool);

