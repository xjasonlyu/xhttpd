#ifndef _THREADPOOL_H_
#define _THREADPOOL_H_

#include <cstdio>
#include <exception>
#include <list>
#include <pthread.h>

#include "mutex.h"

#define DEFAULT_THREAD_NUMBER 4
#define MAX_REQUESTS_NUMBER 1000

template <typename T>
class threadpool {
  public:
    threadpool(int thread_number = DEFAULT_THREAD_NUMBER,
               int max_requests = MAX_REQUESTS_NUMBER);
    ~threadpool();

    bool append(T *request);

  private:
    static void *worker(void *arg);

    void run();

  private:
    Flag m_queue_flag; //条件变量
    Mutex m_queue_mu;  //锁

    int m_thread_number; //线程数
    int m_max_requests;  //最大请求量
    bool m_stop;         //线程池状态

    pthread_t *m_threads;       //线程
    std::list<T *> m_workqueue; //任务队列
};

template <typename T>
threadpool<T>::threadpool(int thread_number, int max_requests) {
    m_thread_number = thread_number;
    m_max_requests = max_requests;

    m_stop = false;
    m_threads = NULL;

    if ((thread_number <= 0) || (max_requests <= 0))
        throw std::exception();

    m_threads = new pthread_t[m_thread_number];
    if (!m_threads)
        throw std::exception();

    for (int i = 0; i < thread_number; ++i) {
        // printf("create the %dth thread\n", i);

        if (pthread_create(m_threads + i, NULL, worker, this) != 0) {
            delete[] m_threads;
            throw std::exception();
        }

        if (pthread_detach(m_threads[i])) {
            delete[] m_threads;
            throw std::exception();
        }
    }
}

template <typename T>
threadpool<T>::~threadpool() {
    delete[] m_threads;
    m_stop = true;
}

template <typename T>
bool threadpool<T>::append(T *request) {
    m_queue_mu.lock();
    if (m_workqueue.size() > m_max_requests) {
        m_queue_mu.unlock();
        return false;
    }
    m_workqueue.push_back(request);
    m_queue_mu.unlock();
    m_queue_flag.post();
    return true;
}

template <typename T>
void *threadpool<T>::worker(void *arg) {
    threadpool *pool = (threadpool *)arg;
    pool->run();
    return pool;
}

template <typename T>
void threadpool<T>::run() {
    while (!m_stop) {
        m_queue_flag.wait();
        m_queue_mu.lock();
        if (m_workqueue.empty()) {
            m_queue_mu.unlock();
            continue;
        }

        T *request = m_workqueue.front();
        m_workqueue.pop_front();
        m_queue_mu.unlock();
        if (!request) {
            continue;
        }

        request->process();
    }
}

#endif
