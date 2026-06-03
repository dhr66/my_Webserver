#ifndef TIMER_H
#define TIMER_H

#include <ctime>
#include <functional>
#include <vector>
#include <algorithm>

// ============================================================
// 最小堆定时器 (Day8)
// 功能：管理连接的超时时间，自动清理空闲连接
// 原理：用最小堆（数组实现）维护所有定时器
//       堆顶 = 最近要过期的定时器
//       epoll_wait 超时时间 = 堆顶的剩余时间
// ============================================================

// 定时器节点
struct TimerNode {
    int client_fd;          // 关联的客户端 fd
    time_t expire_time;     // 过期时间（绝对时间戳）
    std::function<void(int)> callback;  // 超时回调函数（参数：client_fd）

    // 构造函数
    TimerNode(int fd, int timeout, std::function<void(int)> cb)
        : client_fd(fd)
        , expire_time(time(nullptr) + timeout)
        , callback(cb) {}

    // 更新过期时间
    void update(int timeout) {
        expire_time = time(nullptr) + timeout;
    }

    // 是否已过期
    bool is_expired() const {
        return time(nullptr) >= expire_time;
    }

    // 获取剩余时间（秒）
    int remaining() const {
        int left = expire_time - time(nullptr);
        return left > 0 ? left : 0;
    }
};

// ============================================================
// 最小堆定时器管理器
// 数据结构：用 vector 实现最小堆
// 操作：
//   add_timer    - 添加定时器 O(log n)
//   del_timer    - 删除定时器 O(log n)
//   adjust_timer - 调整过期时间 O(log n)
//   tick         - 检查并处理过期定时器 O(k log n)
//   get_timeout  - 获取堆顶剩余时间 O(1)
// ============================================================
class MinHeapTimer {
public:
    MinHeapTimer() {}

    ~MinHeapTimer() {
        m_heap.clear();
    }

    // ============================================================
    // 添加定时器
    // 插入到堆尾，然后上浮（sift up）
    // ============================================================
    void add_timer(TimerNode* timer) {
        if (!timer) return;
        m_heap.push_back(timer);
        sift_up(m_heap.size() - 1);
    }

    // ============================================================
    // 删除定时器（标记删除 + 惰性删除）
    // 为什么不用直接删除？
    //   直接删除需要在堆中找到位置，O(n)
    //   标记删除更简单，tick() 时跳过已删除的节点
    // ============================================================
    void del_timer(int client_fd) {
        // 找到对应的定时器
        for (auto it = m_heap.begin(); it != m_heap.end(); ++it) {
            if ((*it)->client_fd == client_fd) {
                // 标记为已删除（fd 设为 -1）
                (*it)->client_fd = -1;
                break;
            }
        }
    }

    // ============================================================
    // 调整定时器（延长过期时间）
    // 场景：收到新数据时，重置该连接的超时时间
    // ============================================================
    void adjust_timer(int client_fd, int timeout) {
        for (auto& timer : m_heap) {
            if (timer->client_fd == client_fd) {
                timer->update(timeout);
                // 过期时间变大，需要下沉
                // 但这里简化处理，tick() 时会重新整理
                break;
            }
        }
    }

    // ============================================================
    // 检查并处理过期定时器
    // 从堆顶开始，处理所有已过期的定时器
    // ============================================================
    void tick() {
        time_t now = time(nullptr);

        // 清理堆顶的已删除节点
        while (!m_heap.empty() && m_heap[0]->client_fd == -1) {
            pop_top();
        }

        // 处理过期定时器
        while (!m_heap.empty() && m_heap[0]->is_expired()) {
            TimerNode* top = m_heap[0];

            // 执行回调（关闭连接）
            if (top->client_fd != -1 && top->callback) {
                top->callback(top->client_fd);
            }

            pop_top();
        }
    }

    // ============================================================
    // 获取堆顶定时器的剩余时间
    // 用于设置 epoll_wait 的超时时间
    // ============================================================
    int get_timeout() {
        // 清理已删除的堆顶
        while (!m_heap.empty() && m_heap[0]->client_fd == -1) {
            pop_top();
        }

        if (m_heap.empty()) {
            return -1;  // 没有定时器，epoll_wait 无限等待
        }

        int timeout = m_heap[0]->remaining();
        return timeout * 1000;  // 转换为毫秒（epoll_wait 单位）
    }

    // 获取定时器数量
    size_t size() const {
        return m_heap.size();
    }

    bool empty() const {
        return m_heap.empty();
    }

private:
    // ============================================================
    // 上浮操作（插入新节点后维护堆性质）
    // 比较当前节点和父节点，如果当前更小就交换
    // ============================================================
    void sift_up(int index) {
        while (index > 0) {
            int parent = (index - 1) / 2;
            if (m_heap[index]->expire_time < m_heap[parent]->expire_time) {
                std::swap(m_heap[index], m_heap[parent]);
                index = parent;
            } else {
                break;
            }
        }
    }

    // ============================================================
    // 下沉操作（删除堆顶后维护堆性质）
    // 比较当前节点和子节点，如果子节点更小就交换
    // ============================================================
    void sift_down(int index) {
        int size = m_heap.size();
        while (true) {
            int smallest = index;
            int left = 2 * index + 1;
            int right = 2 * index + 2;

            if (left < size && m_heap[left]->expire_time < m_heap[smallest]->expire_time) {
                smallest = left;
            }
            if (right < size && m_heap[right]->expire_time < m_heap[smallest]->expire_time) {
                smallest = right;
            }

            if (smallest != index) {
                std::swap(m_heap[index], m_heap[smallest]);
                index = smallest;
            } else {
                break;
            }
        }
    }

    // ============================================================
    // 弹出堆顶
    // 用堆尾替换堆顶，然后下沉
    // ============================================================
    void pop_top() {
        if (m_heap.empty()) return;

        // 用堆尾替换堆顶
        m_heap[0] = m_heap.back();
        m_heap.pop_back();

        // 下沉维护堆性质
        if (!m_heap.empty()) {
            sift_down(0);
        }
    }

private:
    std::vector<TimerNode*> m_heap;  // 最小堆（数组实现）
};

#endif // TIMER_H
