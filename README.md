# TinyWebServer 学习笔记

> **项目概述**：从零实现一个 C++ Linux Web 服务器，涵盖网络编程、多线程、HTTP 协议等核心知识点。
>
> **适用场景**：面试准备、项目复习、知识梳理

---
## 目录

1. [项目架构](#1-项目架构)
2. [Day0：环境搭建](#2-day0环境搭建)
3. [Day1：最简 Web 服务器](#3-day1最简-web-服务器)
4. [Day2：HTTP 协议解析](#4-day2http-协议解析)
5. [Day3：线程池](#5-day3线程池)
6. [核心知识点总结](#6-核心知识点总结)
7. [面试常见问题](#7-面试常见问题)
8. [代码清单](#8-代码清单)

---

## 1. 项目架构

### 1.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                    Web 服务器架构                            │
├─────────────────────────────────────────────────────────────┤
│  客户端（浏览器）                                            │
│      ↓ HTTP 请求                                            │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              主线程（事件循环）                       │   │
│  │  socket → bind → listen → select → accept           │   │
│  └─────────────────────────────────────────────────────┘   │
│      ↓ 任务队列                                            │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              线程池（工作线程）                       │   │
│  │  线程1 │ 线程2 │ 线程3 │ ... │ 线程N                │   │
│  └─────────────────────────────────────────────────────┘   │
│      ↓ 处理请求                                            │
│  ┌─────────────────────────────────────────────────────┐   │
│  │              HTTP 处理器                             │   │
│  │  解析请求 → 读取文件 → 构建响应 → 发送响应           │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
```

### 1.2 文件结构

```
my_webserver/
├── main.cpp           # 主程序：socket 通信、事件循环
├── http_conn.h        # HTTP 连接处理类：状态机解析器
├── threadpool.h       # 线程池类：生产者-消费者模型
├── locker.h           # 线程同步封装：互斥锁、条件变量、信号量
└── root/              # 静态资源目录
    ├── index.html     # 主页
    └── post_test.html # POST 测试页面
```

### 1.3 核心模块

| 模块 | 文件 | 职责 |
|------|------|------|
| 网络通信 | main.cpp | socket 编程、事件循环 |
| HTTP 处理 | http_conn.h | 解析 HTTP 请求、构建响应 |
| 并发处理 | threadpool.h | 管理线程、异步执行任务 |
| 同步机制 | locker.h | 线程同步、互斥访问 |

---

## 2. Day0：环境搭建

### 2.1 开发环境

- **操作系统**：Windows 11 + VMware Ubuntu 虚拟机
- **开发工具**：VS Code（远程连接虚拟机）
- **编译器**：g++（支持 C++11）
- **共享文件夹**：`/mnt/hgfs/my_webserver` ↔ `C:\Users\LENOVO\Desktop\my_webserver`

### 2.2 环境配置

```bash
# 安装 g++
sudo apt update
sudo apt install g++

# 安装 open-vm-tools（共享文件夹支持）
sudo apt install open-vm-tools-desktop

# 配置开机自动挂载共享文件夹
sudo nano /etc/fstab
# 添加：host:/ /mnt/hgfs fuse.vmhgfs-fuse defaults,allow_other 0 0
```

---

## 3. Day1：最简 Web 服务器

### 3.1 核心流程

```
socket() → bind() → listen() → select() → accept() → recv() → send() → close()
```

### 3.2 Socket 编程基础

#### 3.2.1 什么是 Socket？

Socket 是网络通信的端点，由 **IP 地址 + 端口号** 唯一标识。

```
客户端 Socket: 192.168.1.100:54321
服务器 Socket: 192.168.1.200:8080
```

#### 3.2.2 Socket 类型

| 类型 | 说明 | 应用场景 |
|------|------|----------|
| SOCK_STREAM | TCP 协议 | HTTP、FTP、邮件 |
| SOCK_DGRAM | UDP 协议 | DNS、视频流、游戏 |

#### 3.2.3 Socket 编程步骤

**服务器端：**
1. `socket()`：创建 socket
2. `bind()`：绑定 IP + 端口
3. `listen()`：开始监听
4. `accept()`：接受连接
5. `recv()`/`send()`：收发数据
6. `close()`：关闭连接

**客户端：**
1. `socket()`：创建 socket
2. `connect()`：连接服务器
3. `send()`/`recv()`：收发数据
4. `close()`：关闭连接

### 3.3 代码实现

#### 3.3.1 创建 Socket

```cpp
// 创建 TCP socket
int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
if (listen_fd < 0) {
    perror("socket 创建失败");
    return 1;
}

// 参数说明：
// AF_INET: IPv4 协议
// SOCK_STREAM: TCP 协议（可靠、有序、双向）
// 0: 自动选择协议
```

#### 3.3.2 绑定地址

![image-20260603155907862](C:\Users\LENOVO\AppData\Roaming\Typora\typora-user-images\image-20260603155907862.png)

```cpp
struct sockaddr_in server_addr;
memset(&server_addr, 0, sizeof(server_addr));
server_addr.sin_family = AF_INET;                // IPv4
server_addr.sin_addr.s_addr = htonl(INADDR_ANY); // 监听所有网卡
server_addr.sin_port = htons(PORT);               // 监听端口

if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
    perror("bind 失败");
    close(listen_fd);
    return 1;
}
```

**字节序转换：**

- `htonl()`：host to network long（32 位）
- `htons()`：host to network short（16 位）
- `ntohl()`：network to host long
- `ntohs()`：network to host short

#### 3.3.3 监听连接

```cpp
// 开始监听
// 第二个参数 backlog：等待队列的最大长度
if (listen(listen_fd, 128) < 0) {
    perror("listen 失败");
    close(listen_fd);
    return 1;
}
```

#### 3.3.4 I/O 多路复用（select）

```cpp
fd_set read_fds;        // 可读文件描述符集合
int max_fd = listen_fd; // 当前最大的文件描述符

while (true) {
    // 每次循环都要重新设置
    FD_ZERO(&read_fds);            // 清空集合 
    FD_SET(listen_fd, &read_fds);  // 把监听 socket 加入集合

    // 等待任意一个文件描述符可读（阻塞）
    int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
    if (ready < 0) {
        perror("select 失败");
        break;
    }

    // 检查监听 socket 是否可读，这里使用的是短链接，每次接受完新连接，通信一次就关闭
    if (FD_ISSET(listen_fd, &read_fds)) {
        // 接受新连接
        int client_fd = accept(listen_fd, ...);
        // 处理客户端请求
        handle_client(client_fd);
    }
}
```

**select 函数说明：**
- 第一个参数：最大文件描述符 + 1
- 第二个参数：可读文件描述符集合
- 第三个参数：可写文件描述符集合
- 第四个参数：异常文件描述符集合
- 第五个参数：超时时间（nullptr 表示无限等待）

### 3.4 关键知识点

#### 3.4.1 监听 Socket vs 连接 Socket

| 类型 | 作用 | 生命周期 |
|------|------|----------|
| 监听 Socket | 接受新连接 | 服务器运行期间 |
| 连接 Socket | 与某个客户端通信 | 单次请求/响应 |

#### 3.4.2 阻塞 vs 非阻塞

- **阻塞**：函数调用后，线程挂起，直到条件满足，下处理机
- **非阻塞**：函数调用后，立即返回，需要轮询检查

#### 3.4.3 I/O 多路复用

| 方法 | 优点 | 缺点 |
|------|------|------|
| select | 跨平台支持 | 最大监听数有限（1024） |
| poll | 无最大限制 | 性能随监听数下降 |
| epoll | 高性能、无限制 | 仅 Linux 支持 |

---

## 4. Day2：HTTP 协议解析

### 4.1 HTTP 协议基础

#### 4.1.1 HTTP 请求格式

```
GET /index.html HTTP/1.1
Host: localhost:8080
User-Agent: Mozilla/5.0
Accept: text/html
Connection: keep-alive

请求体（POST 请求）
```

**结构：**
1. **请求行**：方法 + 路径 + 版本
2. **请求头**：Key: Value 格式
3. **空行**：分隔请求头和请求体
4. **请求体**：POST 请求的数据

#### 4.1.2 HTTP 响应格式

```
HTTP/1.1 200 OK
Content-Type: text/html
Content-Length: 1234
Connection: close

<html>...</html>
```

**结构：**
1. **状态行**：版本 + 状态码 + 状态文本
2. **响应头**：Key: Value 格式
3. **空行**：分隔响应头和响应体
4. **响应体**：实际内容

#### 4.1.3 HTTP 方法

| 方法 | 说明 | 幂等性 | 安全性 |
|------|------|--------|--------|
| GET | 获取资源 | 是 | 是 |
| POST | 提交数据 | 否 | 否 |
| PUT | 更新资源 | 是 | 否 |
| DELETE | 删除资源 | 是 | 否 |

**幂等性**：多次请求，结果相同
**安全性**：不会修改服务器资源

#### 4.1.4 常见状态码

| 状态码 | 说明 | 场景 |
|--------|------|------|
| 200 | OK | 请求成功 |
| 301 | Moved Permanently | 永久重定向 |
| 302 | Found | 临时重定向 |
| 400 | Bad Request | 请求格式错误 |
| 403 | Forbidden | 无权限访问 |
| 404 | Not Found | 资源不存在 |
| 405 | Method Not Allowed | 方法不允许 |
| 500 | Internal Server Error | 服务器内部错误 |

### 4.2 状态机解析器

#### 4.2.1 什么是状态机？

状态机是一种编程模式，按顺序处理不同的状态：

```
请求行 → 请求头 → 请求体 → 完成
   ↓        ↓        ↓       ↓
 状态1    状态2    状态3   状态4
```

#### 4.2.2 状态机实现

```cpp
// HTTP 请求状态
enum ParseState {
    REQUEST_LINE,  // 解析请求行
    HEADERS,       // 解析请求头
    BODY,          // 解析请求体
    FINISH         // 解析完成
};

// 解析函数
bool parse(const char* buffer, int len) {
    std::string data(buffer, len);
    std::istringstream stream(data);
    std::string line;

    while (std::getline(stream, line)) {
        switch (m_state) {
            case REQUEST_LINE:
                parse_request_line(line);
                break;
            case HEADERS:
                parse_header(line);
                break;
            case BODY:
                parse_body(line);
                break;
            case FINISH:
                return true;
        }
    }
    return m_state == FINISH;
}
```

#### 4.2.3 解析请求行

```cpp
bool parse_request_line(const std::string& line) {
    std::istringstream stream(line);
    std::string method, path, version;

    // 格式：GET /index.html HTTP/1.1
    stream >> method >> path >> version;

    // 解析方法
    if (method == "GET") {
        m_request.method = GET;
    } else if (method == "POST") {
        m_request.method = POST;
    }

    // 解析查询参数（GET 请求）
    size_t pos = path.find('?');
    if (pos != std::string::npos) {
        m_request.path = path.substr(0, pos);
        m_query_string = path.substr(pos + 1);
    }

    m_state = HEADERS;
    return true;
}
```

#### 4.2.4 解析请求头

```cpp
bool parse_header(const std::string& line) {
    // 空行表示请求头结束
    if (line.empty()) {
        return false;
    }

    // 格式：Key: Value
    size_t pos = line.find(':');
    if (pos == std::string::npos) {
        return false;
    }

    std::string key = line.substr(0, pos);
    std::string value = line.substr(pos + 1);

    // 去除前导空格
    while (!value.empty() && value[0] == ' ') {
        value = value.substr(1);
    }

    // 保存请求头
    m_request.headers[key] = value;

    // 检查 Content-Length
    if (key == "Content-Length") {
        m_request.content_length = std::stoi(value);
    }

    return true;
}
```

### 4.3 构建 HTTP 响应

```cpp
std::string build_response(int status_code, const std::string& content_type, 
                           const std::string& body) {
    std::string status_text;
    switch (status_code) {
        case 200: status_text = "OK"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
    }

    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
             << "Content-Type: " << content_type << "\r\n"
             << "Content-Length: " << body.size() << "\r\n"
             << "Connection: close\r\n"
             << "\r\n"
             << body;

    return response.str();
}
```

### 4.4 关键知识点

#### 4.4.1 GET vs POST

| 特性 | GET | POST |
|------|-----|------|
| 参数位置 | URL 查询字符串 | 请求体 |
| 参数长度 | 有限制（浏览器限制） | 无限制 |
| 缓存 | 可缓存 | 不缓存 |
| 安全性 | 参数暴露在 URL | 参数在请求体 |
| 幂等性 | 是 | 否 |

#### 4.4.2 Content-Type 常见类型

| 类型 | 说明 | 示例 |
|------|------|------|
| text/html | HTML 文档 | 网页 |
| text/plain | 纯文本 | 日志 |
| application/json | JSON 数据 | API 响应 |
| application/x-www-form-urlencoded | 表单数据 | POST 表单 |
| multipart/form-data | 文件上传 | 文件上传 |

---

## 5. Day3：线程池

### 5.1 为什么需要线程池？

#### 5.1.1 问题：单线程服务器

```
客户端1 → 服务器 → 处理请求1（耗时）→ 响应1
客户端2 → 服务器 → 等待... → 处理请求2 → 响应2
客户端3 → 服务器 → 等待... → 等待... → 处理请求3 → 响应3
```

**问题**：请求排队，响应慢

#### 5.1.2 解决方案：多线程

```
客户端1 → 服务器 → 线程1 → 处理请求1 → 响应1
客户端2 → 服务器 → 线程2 → 处理请求2 → 响应2
客户端3 → 服务器 → 线程3 → 处理请求3 → 响应3
```

**优点**：并发处理，响应快

#### 5.1.3 问题：频繁创建/销毁线程

- 创建线程：分配内存、初始化栈、设置调度
- 销毁线程：释放资源、清理状态
- 高并发时，频繁创建/销毁线程，开销很大

#### 5.1.4 解决方案：线程池

```
┌─────────────────────────────────────────────────────────┐
│                    线程池架构                            │
├─────────────────────────────────────────────────────────┤
│  主线程                                                   │
│      ↓ 添加任务                                          │
│  ┌─────────────────────────────────────────────────┐   │
│  │              任务队列                             │   │
│  │  ┌───┐ ┌───┐ ┌───┐ ┌───┐ ┌───┐                 │   │
│  │  │ 1 │ │ 2 │ │ 3 │ │ 4 │ │ 5 │ ...             │   │
│  │  └───┘ └───┘ └───┘ └───┘ └───┘                 │   │
│  └─────────────────────────────────────────────────┘   │
│      ↓ 取出任务                                          │
│  ┌─────────────────────────────────────────────────┐   │
│  │              工作线程                             │   │
│  │  线程1 │ 线程2 │ 线程3 │ ... │ 线程N            │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 5.2 生产者-消费者模型

#### 5.2.1 什么是生产者-消费者模型？

- **生产者**：生成任务，放入队列
- **消费者**：从队列取出任务，执行任务
- **缓冲区**：任务队列，平衡生产/消费速度

#### 5.2.2 为什么用这个模型？

1. **解耦**：生产者和消费者互不依赖
2. **异步**：生产者不用等待消费者完成
3. **平衡**：队列缓冲突发任务

#### 5.2.3 同步机制

| 机制 | 作用 | 操作 |
|------|------|------|
| 互斥锁 | 保护共享资源 | lock/unlock |
| 信号量 | 线程间通知 | wait/post |

**互斥锁**：同一时间只有一个线程能访问临界区
**信号量**：计数器，有任务时唤醒线程

### 5.3 代码实现

#### 5.3.1 线程池类定义

```cpp
class ThreadPool {
public:
    ThreadPool(int thread_count = 8, int max_requests = 10000);
    ~ThreadPool();
    bool append(std::function<void()> task);

private:
    static void* worker(void* arg);
    void run();

private:
    int m_thread_count;         // 线程数量
    int m_max_requests;         // 任务队列最大长度
    pthread_t* m_threads;       // 线程数组
    std::queue<std::function<void()>> m_task_queue; // 任务队列
    Locker m_queue_locker;      // 任务队列互斥锁
    Sem m_queue_sem;            // 任务队列信号量
    bool m_stop;                // 是否停止线程池
};
```

#### 5.3.2 构造函数

```cpp
ThreadPool(int thread_count = 8, int max_requests = 10000)
    : m_thread_count(thread_count)
    , m_max_requests(max_requests)
    , m_threads(nullptr)
    , m_stop(false) {

    // 创建线程数组
    m_threads = new pthread_t[m_thread_count];

    // 创建工作线程
    for (int i = 0; i < m_thread_count; i++) {
        // 线程函数必须是静态函数
        pthread_create(m_threads + i, nullptr, worker, this);
        // 分离线程，线程结束后自动释放资源
        pthread_detach(m_threads[i]);
    }
}
```

**为什么线程函数必须是静态函数？**
- 普通成员函数隐含 `this` 参数
- pthread_create 期望 `void* (*)(void*)` 签名
- 静态函数没有 `this`，通过参数传入

#### 5.3.3 添加任务

```cpp
bool append(std::function<void()> task) {
    // 加锁
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
```

#### 5.3.4 工作线程

```cpp
static void* worker(void* arg) {
    ThreadPool* pool = (ThreadPool*)arg;
    pool->run();
    return nullptr;
}

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
```

### 5.4 关键知识点

#### 5.4.1 std::function vs 函数指针

```cpp
// 函数指针（只能指向普通函数）
typedef void (*TaskFunc)(void* arg);

// std::function（可以存储任何可调用对象）
std::function<void()> task;

// 可以存储 lambda
task = []() { std::cout << "hello" << std::endl; };

// 可以存储绑定表达式
task = std::bind(&MyClass::func, this, arg);
```

#### 5.4.2 RAII（资源获取即初始化）

```cpp
class Locker {
    pthread_mutex_t m_mutex;
public:
    Locker() { pthread_mutex_init(&m_mutex, nullptr); }
    ~Locker() { pthread_mutex_destroy(&m_mutex); }
    bool lock() { return pthread_mutex_lock(&m_mutex) == 0; }
    bool unlock() { return pthread_mutex_unlock(&m_mutex) == 0; }
};
```

**RAII 原则**：
- 构造函数获取资源
- 析构函数释放资源
- 离开作用域时自动调用析构函数

#### 5.4.3 线程分离 vs 线程结合

```cpp
// 分离线程（推荐）
pthread_detach(thread_id);
// 线程结束后自动释放资源，不需要 join

// 结合线程
pthread_join(thread_id, nullptr);
// 阻塞等待线程结束，手动回收资源
```

---

## 6. 核心知识点总结

### 6.1 网络编程

| 知识点 | 说明 | 应用场景 |
|--------|------|----------|
| Socket 编程 | 网络通信端点 | 所有网络应用 |
| TCP 协议 | 可靠、有序、面向连接 | HTTP、FTP、邮件 |
| I/O 多路复用 | 一个线程监听多个连接 | 高并发服务器 |
| 字节序转换 | 大端/小端序 | 跨平台通信 |

### 6.2 HTTP 协议

| 知识点 | 说明 | 应用场景 |
|--------|------|----------|
| 请求格式 | 请求行 + 请求头 + 请求体 | Web 服务器 |
| 响应格式 | 状态行 + 响应头 + 响应体 | Web 服务器 |
| 状态码 | 200/404/500 等 | 错误处理 |
| Content-Type | 内容类型 | 响应构建 |

### 6.3 多线程编程

| 知识点 | 说明 | 应用场景 |
|--------|------|----------|
| 线程池 | 预先创建线程，重复使用 | 高并发服务器 |
| 生产者-消费者 | 任务队列 + 工作线程 | 异步处理 |
| 互斥锁 | 保护临界区 | 共享资源访问 |
| 信号量 | 线程间通知 | 任务调度 |

### 6.4 设计模式

| 模式 | 说明 | 应用场景 |
|------|------|----------|
| 状态机 | 按顺序处理不同状态 | HTTP 解析 |
| RAII | 资源获取即初始化 | 资源管理 |
| 回调函数 | 任务执行 | 线程池 |
| 单例模式 | 全局唯一实例 | 日志系统 |

---

## 7. 面试常见问题

### 7.1 网络编程

**Q1：TCP 三次握手过程？**

```
客户端 → SYN → 服务器
客户端 ← SYN+ACK ← 服务器
客户端 → ACK → 服务器
```

**Q2：TCP 四次挥手过程？**

```
客户端 → FIN → 服务器
客户端 ← ACK ← 服务器
客户端 ← FIN ← 服务器
客户端 → ACK → 服务器
```

**Q3：select、poll、epoll 的区别？**

| 特性 | select | poll | epoll |
|------|--------|------|-------|
| 最大连接数 | 1024 | 无限制 | 无限制 |
| 触发方式 | 水平触发 | 水平触发 | 水平/边缘触发 |
| 性能 | O(n) | O(n) | O(1) |
| 跨平台 | 支持 | 支持 | 仅 Linux |

**Q4：什么是 I/O 多路复用？**

用一个线程同时监听多个文件描述符，当任何一个有数据可读时，线程被唤醒处理。

**Q5：阻塞 I/O 和非阻塞 I/O 的区别？**

- **阻塞 I/O**：函数调用后，线程挂起，直到条件满足
- **非阻塞 I/O**：函数调用后，立即返回，需要轮询检查

### 7.2 HTTP 协议

**Q1：GET 和 POST 的区别？**

| 特性 | GET | POST |
|------|-----|------|
| 参数位置 | URL 查询字符串 | 请求体 |
| 参数长度 | 有限制 | 无限制 |
| 缓存 | 可缓存 | 不缓存 |
| 安全性 | 参数暴露 | 参数隐藏 |
| 幂等性 | 是 | 否 |

**Q2：HTTP 状态码 200、301、302、404、500 的含义？**

- 200：请求成功
- 301：永久重定向
- 302：临时重定向
- 404：资源不存在
- 500：服务器内部错误

**Q3：什么是 HTTP 长连接和短连接？**

- **短连接（HTTP/1.0）**：每次请求都建立新连接，请求完就关闭
- **长连接（HTTP/1.1）**：保持连接，可以发送多个请求

**Q4：Content-Type 有哪些常见类型？**

- text/html：HTML 文档
- text/plain：纯文本
- application/json：JSON 数据
- application/x-www-form-urlencoded：表单数据
- multipart/form-data：文件上传

### 7.3 多线程编程

**Q1：什么是线程池？有什么优点？**

线程池预先创建一组线程，重复使用，避免频繁创建/销毁线程。

**优点：**
1. 提高响应速度（线程已存在）
2. 降低资源消耗（重复使用）
3. 提高线程可管理性（统一管理）

**Q2：什么是生产者-消费者模型？**

- **生产者**：生成任务，放入队列
- **消费者**：从队列取出任务，执行任务
- **缓冲区**：任务队列，平衡生产/消费速度

**Q3：互斥锁和信号量的区别？**

| 特性 | 互斥锁 | 信号量 |
|------|--------|--------|
| 作用 | 保护共享资源 | 线程间通知 |
| 操作 | lock/unlock | wait/post |
| 状态 | 二值（0/1） | 计数（0~N） |
| 场景 | 临界区 | 生产者-消费者 |

**Q4：什么是死锁？如何避免？**

**死锁**：两个或多个线程互相等待对方释放资源，导致都无法继续执行。

**避免方法：**
1. 按顺序加锁
2. 设置超时时间
3. 使用 RAII 自动管理锁

**Q5：什么是 RAII？**

RAII（Resource Acquisition Is Initialization）：资源获取即初始化。

- 构造函数获取资源
- 析构函数释放资源
- 离开作用域时自动调用析构函数

**Q6：std::thread 和 pthread 的区别？**

| 特性 | std::thread | pthread |
|------|-------------|---------|
| 标准 | C++11 标准库 | POSIX 标准 |
| 跨平台 | 支持 | 主要 Unix/Linux |
| 异常处理 | 支持 | 不支持 |
| 易用性 | 更简单 | 更底层 |

### 7.4 项目相关

**Q1：为什么使用 select 而不是 epoll？**

- select 跨平台支持（Windows/Linux/macOS）
- 学习阶段，select 更简单易懂
- 实际项目中，Linux 推荐使用 epoll

**Q2：线程池的线程数量如何确定？**

- **CPU 密集型**：CPU 核心数 + 1
- **I/O 密集型**：CPU 核心数 * 2
- **经验值**：8-16 个线程

**Q3：如何处理高并发？**

1. I/O 多路复用（select/poll/epoll）
2. 线程池（多线程处理）
3. 非阻塞 I/O
4. 事件驱动模型

**Q4：如何保证线程安全？**

1. 互斥锁（保护临界区）
2. 原子操作（无锁编程）
3. 读写锁（读多写少场景）
4. 条件变量（线程间通信）

---

## 8. 代码清单

### 8.1 locker.h（线程同步封装）

```cpp
#ifndef LOCKER_H
#define LOCKER_H

#include <pthread.h>
#include <semaphore.h>
#include <stdexcept>

// 互斥锁类
class Locker {
public:
    Locker() {
        if (pthread_mutex_init(&m_mutex, nullptr) != 0) {
            throw std::runtime_error("互斥锁初始化失败");
        }
    }
    ~Locker() { pthread_mutex_destroy(&m_mutex); }
    bool lock() { return pthread_mutex_lock(&m_mutex) == 0; }
    bool unlock() { return pthread_mutex_unlock(&m_mutex) == 0; }
    pthread_mutex_t* get() { return &m_mutex; }
private:
    pthread_mutex_t m_mutex;
};

// 条件变量类
class Cond {
public:
    Cond() {
        if (pthread_cond_init(&m_cond, nullptr) != 0) {
            throw std::runtime_error("条件变量初始化失败");
        }
    }
    ~Cond() { pthread_cond_destroy(&m_cond); }
    bool wait(pthread_mutex_t* mutex) {
        return pthread_cond_wait(&m_cond, mutex) == 0;
    }
    bool signal() { return pthread_cond_signal(&m_cond) == 0; }
    bool broadcast() { return pthread_cond_broadcast(&m_cond) == 0; }
private:
    pthread_cond_t m_cond;
};

// 信号量类
class Sem {
public:
    Sem(int value = 0) {
        if (sem_init(&m_sem, 0, value) != 0) {
            throw std::runtime_error("信号量初始化失败");
        }
    }
    ~Sem() { sem_destroy(&m_sem); }
    bool wait() { return sem_wait(&m_sem) == 0; }
    bool post() { return sem_post(&m_sem) == 0; }
private:
    sem_t m_sem;
};

#endif // LOCKER_H
```

### 8.2 http_conn.h（HTTP 连接处理类）

```cpp
#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <string>
#include <map>
#include <sstream>

class HttpConn {
public:
    enum ParseState { REQUEST_LINE, HEADERS, BODY, FINISH };
    enum Method { GET, POST, PUT, DELETE, UNKNOWN };

    struct Request {
        Method method;
        std::string path;
        std::string version;
        std::map<std::string, std::string> headers;
        std::string body;
        int content_length;
    };

public:
    HttpConn() : m_state(REQUEST_LINE), m_request{} {
        m_request.content_length = 0;
    }

    bool parse(const char* buffer, int len) {
        std::string data(buffer, len);
        std::istringstream stream(data);
        std::string line;

        while (std::getline(stream, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            switch (m_state) {
                case REQUEST_LINE:
                    if (!parse_request_line(line)) return false;
                    break;
                case HEADERS:
                    if (!parse_header(line)) {
                        if (m_request.content_length > 0) {
                            m_state = BODY;
                        } else {
                            m_state = FINISH;
                        }
                    }
                    break;
                case BODY:
                    m_request.body += line;
                    if (m_request.body.size() >= m_request.content_length) {
                        m_state = FINISH;
                    }
                    break;
                case FINISH:
                    return true;
            }
        }
        return m_state == FINISH;
    }

    std::string build_response(int status_code, const std::string& content_type,
                               const std::string& body) {
        std::string status_text;
        switch (status_code) {
            case 200: status_text = "OK"; break;
            case 404: status_text = "Not Found"; break;
            case 405: status_text = "Method Not Allowed"; break;
        }

        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: close\r\n"
                 << "\r\n"
                 << body;
        return response.str();
    }

    Method get_method() const { return m_request.method; }
    const std::string& get_path() const { return m_request.path; }
    const std::string& get_body() const { return m_request.body; }

private:
    bool parse_request_line(const std::string& line) {
        std::istringstream stream(line);
        std::string method, path, version;
        stream >> method >> path >> version;

        if (method == "GET") m_request.method = GET;
        else if (method == "POST") m_request.method = POST;
        else m_request.method = UNKNOWN;

        m_request.path = path;
        m_request.version = version;

        size_t pos = path.find('?');
        if (pos != std::string::npos) {
            m_request.path = path.substr(0, pos);
            m_query_string = path.substr(pos + 1);
        }

        m_state = HEADERS;
        return true;
    }

    bool parse_header(const std::string& line) {
        if (line.empty()) return false;

        size_t pos = line.find(':');
        if (pos == std::string::npos) return false;

        std::string key = line.substr(0, pos);
        std::string value = line.substr(pos + 1);

        while (!value.empty() && value[0] == ' ') {
            value = value.substr(1);
        }

        m_request.headers[key] = value;

        if (key == "Content-Length") {
            m_request.content_length = std::stoi(value);
        }

        return true;
    }

private:
    ParseState m_state;
    Request m_request;
    std::string m_query_string;
};

#endif // HTTP_CONN_H
```

### 8.3 threadpool.h（线程池类）

```cpp
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <pthread.h>
#include <queue>
#include <functional>
#include "locker.h"

class ThreadPool {
public:
    ThreadPool(int thread_count = 8, int max_requests = 10000)
        : m_thread_count(thread_count)
        , m_max_requests(max_requests)
        , m_threads(nullptr)
        , m_stop(false) {

        m_threads = new pthread_t[m_thread_count];

        for (int i = 0; i < m_thread_count; i++) {
            pthread_create(m_threads + i, nullptr, worker, this);
            pthread_detach(m_threads[i]);
        }
    }

    ~ThreadPool() {
        m_stop = true;
        delete[] m_threads;
    }

    bool append(std::function<void()> task) {
        m_queue_locker.lock();

        if (m_task_queue.size() >= m_max_requests) {
            m_queue_locker.unlock();
            return false;
        }

        m_task_queue.push(task);
        m_queue_locker.unlock();
        m_queue_sem.post();

        return true;
    }

private:
    static void* worker(void* arg) {
        ThreadPool* pool = (ThreadPool*)arg;
        pool->run();
        return nullptr;
    }

    void run() {
        while (!m_stop) {
            m_queue_sem.wait();
            m_queue_locker.lock();

            if (m_task_queue.empty()) {
                m_queue_locker.unlock();
                continue;
            }

            std::function<void()> task = m_task_queue.front();
            m_task_queue.pop();
            m_queue_locker.unlock();

            if (task) {
                task();
            }
        }
    }

private:
    int m_thread_count;
    int m_max_requests;
    pthread_t* m_threads;
    std::queue<std::function<void()>> m_task_queue;
    Locker m_queue_locker;
    Sem m_queue_sem;
    bool m_stop;
};

#endif // THREADPOOL_H
```

---

## 9. 学习建议

### 9.1 如何使用这份笔记？

1. **面试前**：快速浏览"面试常见问题"
2. **复习时**：精读"核心知识点总结"
3. **编码时**：参考"代码清单"
4. **深入学习**：阅读"Day X"章节的详细解释

### 9.2 学习路径

```
基础阶段（Day0-Day3）
├── 环境搭建
├── Socket 编程
├── HTTP 协议
└── 多线程编程

进阶阶段（Day4-Day7）
├── 日志系统
├── 定时器
├── 数据库连接池
└── 性能优化

深入学习
├── epoll（Linux 高性能 I/O）
├── 协程（用户态线程）
├── 分布式系统
└── 微服务架构
```

### 9.3 推荐书籍

1. **《UNIX 网络编程》** - W. Richard Stevens
   - 网络编程圣经，深入理解 Socket 编程

2. **《Linux 高性能服务器编程》** - 游双
   - 实战导向，涵盖线程池、I/O 模型

3. **《C++ Primer》** - Stanley B. Lippman
   - C++ 基础，现代 C++ 特性

4. **《Linux 多线程服务端编程》** - 陈硕
   - 高并发服务器设计，muduo 网络库

### 9.4 实践建议

1. **手写代码**：不看笔记，自己实现一遍
2. **画图理解**：画出架构图、流程图
3. **调试跟踪**：用 gdb 调试，理解执行流程
4. **性能测试**：用 ab/wrk 测试并发性能
5. **源码阅读**：阅读 TinyWebServer、muduo 等开源项目

---

## 10. 总结

### 10.1 项目收获

1. **网络编程**：Socket 编程、TCP 协议、I/O 多路复用
2. **HTTP 协议**：请求解析、响应构建、状态机设计
3. **多线程编程**：线程池、生产者-消费者、同步机制
4. **工程能力**：代码组织、模块设计、错误处理

### 10.2 核心能力

- **编程能力**：C++ 编程、面向对象设计
- **系统编程**：Linux 系统调用、多线程编程
- **网络编程**：TCP/IP 协议栈、HTTP 协议
- **工程能力**：项目架构、代码管理、性能优化

### 10.3 下一步计划

- Day4：日志系统（异步写文件）
- Day5：定时器（非活动连接超时关闭）
- Day6：数据库连接池（登录/注册功能）
- Day7：整合 + 压测 + 复盘

---

**最后更新**：2026-06-02

**作者**：saber

**项目地址**：`/mnt/hgfs/my_webserver`

**开发环境**：Windows 11 + VMware Ubuntu + VS Code

---

> **备注**：这份笔记会随着项目进展持续更新，建议定期复习，加深理解。
