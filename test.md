# Web 服务器学习笔记

---

## 1. 状态机解析 HTTP 请求

**什么是状态机？**
按顺序处理每个部分，每处理完一部分就进入下一个状态。

```
请求行 → 请求头 → 请求体 → 完成
  ↓        ↓        ↓       ↓
状态1    状态2    状态3   状态4
```

---

## 2. 线程池

**为什么需要线程池？**
- 创建/销毁线程很耗时
- 线程池预先创建好线程，重复使用
- 控制并发数量，避免资源耗尽

**函数对象：**
```cpp
std::function<void()> task;
```
- 可以存储任何可调用的对象（函数、lambda、绑定表达式）
- 是一种数据类型，类似 int
- `std::function<void()>` 表示：无参数、无返回值的函数

---

## 3. 线程分离 vs 线程结合

```cpp
// 分离线程（推荐）
pthread_detach(thread_id);
// 线程结束后自动释放资源，不需要 join

// 结合线程
pthread_join(thread_id, nullptr);
// 阻塞等待线程结束，手动回收资源
```

---

## 4. epoll 触发模式：LT vs ET

**面试官会问："你用的是 LT 还是 ET？"**

- ET + 非阻塞 IO 是高性能服务器的标配
- 系统调用次数少，性能更高

**区别：**

| 模式 | 特点 | 适用场景 |
|------|------|----------|
| LT（水平触发） | 没读完就一直通知 | 监听 socket |
| ET（边缘触发） | 只通知一次 | 客户端 socket |

---

## 5. 非阻塞 IO

**为什么需要非阻塞？**
- ET 模式只通知一次，必须一次读完
- 如果用阻塞 IO，读到没数据时会卡住
- 非阻塞 IO 读到没数据时返回 EAGAIN，不会卡住

---

## 6. 监听 socket 用 LT，客户端 socket 用 ET

**监听 socket（listen_fd）的问题：**

ET 模式下，同时来了 3 个连接：
```
epoll_wait: [通知] 有连接！（只通知这一次）
accept: 处理了 1 个
剩下 2 个？没通知了，得等到下次有新连接才处理
```

**解决方案：**
- 监听 socket 用 **LT**（水平触发）：有连接就一直通知，直到 accept 完
- 客户端 socket 用 **ET**（边缘触发）：只通知一次，配合非阻塞 IO 循环读完

**面试话术：**
> "我的服务器监听 socket 用 LT 模式，确保所有连接都能 accept；客户端 socket 用 ET 模式，配合非阻塞 IO 循环读取，减少系统调用次数，提高性能。"

---

## 7. fd 传参与内核状态

**问题：** 设置非阻塞 IO 的函数，fd 直接传参，修改的不是原来的 fd 吗？

**答案：** fd 是整数索引，fcntl 修改的是内核状态

```
fd = 5（只是一个整数，指向内核中的文件表）

fcntl(fd, F_SETFL, flags)
     ↓
内核：好的，我把 5 号文件描述符的状态改成 flags
```

**类比：**
- fd 就像房间号（比如 501）
- fcntl 就像告诉酒店前台："501 房间改成无烟房"
- 你传的是房间号（整数），但改的是房间的状态（内核里的数据）

**结论：**
`setnonblocking(fd)` 虽然是传参复制了 fd 这个整数，但 `fcntl` 真正修改的是内核中该文件描述符的状态，效果是全局的。

---

## 8. Reactor 模式

**什么是 Reactor？**

Reactor = 反应堆。像核反应堆一样，有事件"反应"时才工作，没事件就静静待着。

**定义：**
Reactor 是一种基于事件驱动的设计模式。通过事件循环等待事件发生，然后调用相应的回调函数来处理请求。

**公式：**
```
Reactor = I/O 多路复用（epoll/select）+ 非阻塞 IO + 事件分发器 + 回调函数
```

**核心思想：** 你注册事件和回调，Reactor 循环等待事件，事件发生时自动调用你的回调。

**我的服务器采用单 Reactor 多线程模型：**
- 主线程负责事件分发（accept 新连接、监听读写事件）
- 线程池负责处理业务逻辑（解析 HTTP、读写文件、发送响应）
- 这样分工明确，主线程不会被业务逻辑阻塞

**三个核心组件：**

| 组件 | 职责 | 类比 |
|------|------|------|
| Reactor（反应器） | 监听和分发事件，调用 epoll_wait 等待事件 | 公司前台 |
| Handler（处理器） | 处理具体业务（读数据、解析协议、写响应） | 办事窗口 |
| Event/Handle（事件） | I/O 资源（socket fd），注册感兴趣的事件 | 排队号牌 |

**工作流程：**
1. 注册阶段：将 socket 和 handler 注册到 Reactor，说明关心"读事件"
2. 循环开始：Reactor 调用 `epoll_wait()` 阻塞等待事件
3. 事件到达：某个 socket 有数据 → epoll 返回就绪的 socket
4. 分发给 Handler：Reactor 根据 socket 找到 handler，调用 `handle_read()`
5. 业务处理：Handler 读取数据、处理业务（可能交给线程池）
6. 继续循环：Reactor 继续 `epoll_wait`，处理下一个就绪事件

**注意：** 事件循环通常只在一个线程里运行，避免锁竞争；耗时业务交给工作线程池异步执行。

---

## 9. 核心代码片段

### epoll 三件套

```cpp
// 1. 创建 epoll 实例
int epfd = epoll_create(1);

// 2. 注册事件
struct epoll_event event;
event.events = EPOLLIN | EPOLLET;  // ET 模式
event.data.fd = client_fd;
epoll_ctl(epfd, EPOLL_CTL_ADD, client_fd, &event);

// 3. 等待事件
struct epoll_event events[MAX_EVENTS];
int ready = epoll_wait(epfd, events, MAX_EVENTS, -1);
```

### 设置非阻塞

```cpp
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}
```

### ET 模式循环读取

```cpp
while (true) {
    int n = recv(fd, buffer, sizeof(buffer), 0);
    if (n < 0) {
        if (errno == EAGAIN) {
            break;  // 数据读完了
        }
        // 其他错误
    }
    if (n == 0) {
        // 对端关闭连接
        break;
    }
    // 处理数据
}
```

---

## 10. fd 生命周期管理

**规则：close(fd) 之前必须先 epoll_ctl(DEL)**

```cpp
// 正确顺序
epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);  // 先移除
close(client_fd);                                        // 再关闭

// 错误顺序（会导致问题）
close(client_fd);                                        // fd 被释放
epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);  // 监听了一个无效 fd
```

**为什么？**
- fd 是整数，close 后内核可能把这个整数分配给新连接
- epoll 里还保留着旧的 fd，会对新连接产生误触发
- 或者 epoll_wait 报 EBADF 错误

**面试话术：**
> "关闭连接时我会先从 epoll 移除 fd，再 close。如果顺序反了，fd 被回收后可能被新连接复用，导致 epoll 对新连接产生误触发。"

---

## 11. 长连接（HTTP/1.1 Keep-Alive）

**短连接（HTTP/1.0）：**
```
客户端 → 请求1 → 服务器
客户端 ← 响应1 ← 服务器
（连接关闭）
客户端 → 请求2 → 服务器（重新建立连接）
客户端 ← 响应2 ← 服务器
（连接关闭）
```

**长连接（HTTP/1.1）：**
```
客户端 → 请求1 → 服务器
客户端 ← 响应1 ← 服务器
（连接保持）
客户端 → 请求2 → 服务器（复用连接）
客户端 ← 响应2 ← 服务器
（连接保持，直到超时或客户端关闭）
```

**优点：**
- 减少 TCP 三次握手开销
- 降低服务器负担

**实现原理：**
- 解析 `Connection: keep-alive` 请求头
- 响应中添加 `Connection: keep-alive`
- 响应后不关闭连接，fd 继续留在 epoll 里

**一句话总结：** keep-alive 时 fd 不从 epoll 移除，新数据到达时 epoll 再次通知，工作线程再次处理。连接复用靠的是"fd 不关 + epoll 重复触发"。

---

## 12. 长连接的 epoll 触发机制

**问题：** handle_client 没有 while 循环，怎么处理同一个连接的多个请求？

**答案：** 靠 epoll 重复触发

```
第1次请求：epoll_wait 返回 → handle_client → 响应 → 不关闭 fd（keep-alive）
                                                            ↓
第2次请求：客户端发数据 → epoll_wait 再次返回 → handle_client → 响应
                                                            ↓
第3次请求：...
```

**关键：**
- 每次 epoll 通知 → 一次 handle_client 调用 → 处理一个请求
- keep-alive 时 fd 不从 epoll 移除，新数据到达时 epoll 再次通知
- 这就是 Reactor 模式：事件驱动，有事件才处理

---

## 13. 优雅断开 vs 强制断开

| | 优雅断开 | 强制断开 |
|--|---------|---------|
| 方式 | `shutdown()` + `close()` | 直接 `close()` |
| 效果 | 发送缓冲区的数据发完再关 | 直接丢弃缓冲区数据 |
| 客户端收到 | FIN（正常关闭） | RST（连接重置） |

**适用场景：**

| 场景 | 有数据在发送？ | 怎么关 |
|------|--------------|--------|
| 超时清理（空闲连接） | 没有 | 直接 `close()` 即可 |
| 服务器主动关机 | 可能有 | `shutdown()` + `close()` |
| 正在处理请求时想断开 | 有 | `shutdown()` + `close()` |

**类比：**
- 优雅断开 = 挂电话前说"我先挂了"，等对方回应再挂
- 强制断开 = 直接挂断，对方听到"嘟嘟嘟"

---

**最后更新：** 2026-06-03
