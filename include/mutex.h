#ifndef _MUTEX_H_
#define _MUTEX_H_

#include <exception>
#include <pthread.h>
#include <semaphore.h>

class Flag {
  public:
    Flag();
    ~Flag();

    bool wait();
    bool post();

  private:
    sem_t m_sem;
};

class Mutex {
  public:
    Mutex();
    ~Mutex();

    bool lock();
    bool unlock();

  private:
    pthread_mutex_t m_mutex;
};

class Cond {
  public:
    Cond();
    ~Cond();

    bool wait();
    bool signal();

  private:
    pthread_cond_t m_cond;
    pthread_mutex_t m_mutex;
};

#endif
