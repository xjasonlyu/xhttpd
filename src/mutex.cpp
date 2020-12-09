#include <exception>
#include <pthread.h>
#include <semaphore.h>

#include "mutex.h"

// class Flag
Flag::Flag() {
    if (sem_init(&m_sem, 0, 0) != 0)
        throw std::exception();
}

Flag::~Flag() {
    sem_destroy(&m_sem);
}

bool Flag::wait() {
    return sem_wait(&m_sem) == 0;
}

bool Flag::post() {
    return sem_post(&m_sem) == 0;
}

// class Mutex
Mutex::Mutex() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
        throw std::exception();
}

Mutex::~Mutex() {
    pthread_mutex_destroy(&m_mutex);
}

bool Mutex::lock() {
    return pthread_mutex_lock(&m_mutex) == 0;
}

bool Mutex::unlock() {
    return pthread_mutex_unlock(&m_mutex) == 0;
}

// class Cond
Cond::Cond() {
    if (pthread_mutex_init(&m_mutex, NULL) != 0)
        throw std::exception();

    if (pthread_cond_init(&m_cond, NULL) != 0) {
        pthread_mutex_destroy(&m_mutex);
        throw std::exception();
    }
}

Cond::~Cond() {
    pthread_mutex_destroy(&m_mutex);
    pthread_cond_destroy(&m_cond);
}

bool Cond::wait() {
    int ret = 0;
    pthread_mutex_lock(&m_mutex);
    ret = pthread_cond_wait(&m_cond, &m_mutex);
    pthread_mutex_unlock(&m_mutex);

    return ret == 0;
}

bool Cond::signal() {
    return pthread_cond_signal(&m_cond) == 0;
}
