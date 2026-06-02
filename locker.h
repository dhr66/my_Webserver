#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>
#include <stdexcept>

// 线程同步封装 - 三个类：互斥锁、条件变量、信号量

// ============================================================
// 1. 互斥锁类 (Locker)
//    作用：同一时间只有一个线程能进入临界区
//    场景：保护共享数据（比如日志文件、连接池）
// ============================================================
class Locker {
public:
    Locker() {
        // 初始化互斥锁，nullptr 表示使用默认属性
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::runtime_error("互斥锁初始化失败");
        }
    }

    ~Locker() {
        pthread_mutex_destroy(&m_mutex);
    }

    // 加锁（如果已被其他线程锁定，则阻塞等待）
    bool lock() {
        //pthread_mutex_lock(&m_mutex)返回0表示成功，否则失败
        return pthread_mutex_lock(&m_mutex) == 0;
    }

    // 解锁
    bool unlock() {
        return pthread_mutex_unlock(&m_mutex) == 0;
    }

    // 获取互斥锁指针（供条件变量使用）
    pthread_mutex_t* get() {
        return &m_mutex;
    }

private:
    pthread_mutex_t m_mutex;
};

// ============================================================
// 2. 条件变量类 (Cond)
//    作用：线程间通信，"等待某个条件成立"
//    场景：生产者-消费者模型（比如日志队列）
// ============================================================
class Cond {
public:
    Cond() {
        if (pthread_cond_init(&m_cond, nullptr) != 0) {
            throw std::runtime_error("条件变量初始化失败");
        }
    }

    ~Cond() {
        pthread_cond_destroy(&m_cond);
    }

    // 等待条件成立（会自动释放互斥锁，并阻塞当前线程）
    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }

    // 唤醒一个等待的线程
    bool signal() {
        return pthread_cond_signal(&m_cond) == 0;
    }

    // 唤醒所有等待的线程
    bool broadcast() {
        return pthread_cond_broadcast(&m_cond) == 0;
    }

private:
    pthread_cond_t m_cond;
};

// ============================================================
// 3. 信号量类 (Sem)
//    作用：控制同时访问资源的线程数量
//    场景：限制并发数（比如数据库连接池最多 10 个连接）
// ============================================================
class Sem {
public:
    // 初始化信号量，value 表示允许同时访问的线程数
    Sem(int value = 0) {
        if (sem_init(&m_sem, 0, value) != 0) {
            throw std::runtime_error("信号量初始化失败");
        }
    }

    ~Sem() {
        sem_destroy(&m_sem);
    }

    // P 操作（信号量减 1，如果为 0 则阻塞等待）
    bool wait() {
        return sem_wait(&m_sem) == 0;
    }

    // V 操作（信号量加 1，唤醒等待的线程）
    bool post() {
        return sem_post(&m_sem) == 0;
    }

private:
    sem_t m_sem;
};

#endif // LOCKER_H
