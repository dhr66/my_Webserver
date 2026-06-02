#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <queue>
#include <functional>
#include "locker.h"

// ============================================================
// 线程池类 (Day3)
// 功能：管理一组工作线程，异步处理客户端请求
// 模型：生产者-消费者模型
// ============================================================

template<typename T>
class ThreadPool {
public:
    // ============================================================
    // 构造函数
    // 参数：thread_count - 线程数量
    //       max_requests - 任务队列最大长度
    // ============================================================
    ThreadPool(int thread_count = 8, int max_requests = 10000)
        : m_thread_count(thread_count)
        , m_max_requests(max_requests)
        , m_threads(nullptr)
        , m_stop(false) {

        // 参数检查
        if (thread_count <= 0 || max_requests <= 0) {
            throw std::runtime_error("线程池参数无效");
        }

        // 创建线程数组
        m_threads = new pthread_t[m_thread_count];
        if (!m_threads) {
            throw std::runtime_error("线程数组分配失败");
        }

        // 创建工作线程
        for (int i = 0; i < m_thread_count; i++) {
            // 线程函数必须是静态函数，通过 this 指针访问成员
            if (pthread_create(m_threads + i, nullptr, worker, this) != 0) {
                delete[] m_threads;
                throw std::runtime_error("线程创建失败");
            }

            // 分离线程，线程结束后自动释放资源
            if (pthread_detach(m_threads[i]) != 0) {
                delete[] m_threads;
                throw std::runtime_error("线程分离失败");
            }
        }
    }

    // ============================================================
    // 析构函数
    // ============================================================
    ~ThreadPool() {
        delete[] m_threads;
        m_stop = true;
    }

    // ============================================================
    // 添加任务到队列
    // 参数：task - 任务函数（回调函数）
    // 返回：true - 添加成功，false - 队列已满
    // ============================================================
    bool append(std::function<void()> task) {
        // 加锁（互斥访问任务队列）
        m_queue_locker.lock();

        // 检查队列是否已满
        if (m_task_queue.size() >= m_max_requests) {
            m_queue_locker.unlock();
            return false;
        }

        // 添加任务到队列
        m_task_queue.push(task);

        // 解锁
        m_queue_locker.unlock();

        // 通知一个等待的线程
        m_queue_sem.post();

        return true;
    }

private:
    // ============================================================
    // 工作线程函数（静态函数）
    // 每个工作线程循环：等待任务 → 取出任务 → 执行任务
    // ============================================================
    static void* worker(void* arg) {
        ThreadPool* pool = (ThreadPool*)arg;
        pool->run();
        return nullptr;
    }

    // ============================================================
    // 线程运行函数
    // ============================================================
    void run() {
        while (!m_stop) {
            // 等待任务（信号量减 1，阻塞直到有任务）
            m_queue_sem.wait();

            // 加锁
            m_queue_locker.lock();

            // 检查队列是否为空
            if (m_task_queue.empty()) {
                m_queue_locker.unlock();
                continue;
            }

            // 取出任务
            std::function<void()> task = m_task_queue.front();
            m_task_queue.pop();

            // 解锁
            m_queue_locker.unlock();

            // 执行任务
            if (task) {
                task();
            }
        }
    }

private:
    int m_thread_count;         // 线程数量
    int m_max_requests;         // 任务队列最大长度
    pthread_t* m_threads;       // 线程数组
    std::queue<std::function<void()>> m_task_queue; // 任务队列
    Locker m_queue_locker;      // 任务队列互斥锁
    Sem m_queue_sem;            // 任务队列信号量（有任务时唤醒线程）
    bool m_stop;                // 是否停止线程池
};

#endif // THREADPOOL_H
