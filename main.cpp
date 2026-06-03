#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/epoll.h>    // epoll 头文件
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cerrno>         // errno
#include "locker.h"
#include "http_conn.h"
#include "threadpool.h"

// ============================================================
// Web 服务器 (Day4 - epoll 版本)
// 功能：接收 HTTP 请求，返回静态网页
// 核心流程：socket → bind → listen → epoll → accept → 收发数据
// ============================================================

// 服务器配置
const int PORT = 8080;           // 监听端口
const int MAX_EVENTS = 1024;     // epoll 最大监听数
const int BUFFER_SIZE = 4096;    // 读写缓冲区大小

// epoll 文件描述符（全局变量，方便清理）
int g_epfd = -1;

// 线程池指针（全局变量，方便事件处理函数访问）
ThreadPool* g_pool = nullptr;

// 函数声明
void handle_client(int client_fd);
std::string read_file(const std::string& path);

// ============================================================
// 设置文件描述符为非阻塞模式
// 为什么需要非阻塞？
//   ET 模式只通知一次，必须一次读完，如果用阻塞 IO，读到没数据时会卡住
//   非阻塞 IO 读到没数据时返回 EAGAIN，不会卡住
// ============================================================
int setnonblocking(int fd) {
    int old_option = fcntl(fd, F_GETFL);      // 获取旧的标志位
    int new_option = old_option | O_NONBLOCK;  // 加上非阻塞标志
    fcntl(fd, F_SETFL, new_option);            // 设置新的标志位
    return old_option;                         // 返回旧的标志位（备用）
}

// ============================================================
// Reactor 模式的核心：事件处理函数
// 主线程只负责"通知"，工作线程负责"干活"
// ============================================================

// 处理新连接事件（主线程执行）
void on_new_connection(int listen_fd) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    // accept 接受连接
    int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
    if (client_fd < 0) {
        perror("accept 失败");
        return;
    }

    std::cout << "[新连接] 客户端: "
              << inet_ntoa(client_addr.sin_addr) << ":"
              << ntohs(client_addr.sin_port) << std::endl;

    // 设置非阻塞模式
    setnonblocking(client_fd);

    // 注册到 epoll，使用 ET 模式
    struct epoll_event event;
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = client_fd;

    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, client_fd, &event) < 0) {
        perror("epoll_ctl 添加客户端失败");
        close(client_fd);
    }
}

// 处理数据到达事件（工作线程执行）
void on_data_arrival(int client_fd) {
    // 使用线程池处理
    std::function<void()> task = [client_fd]() {
        // handle_client 内部会循环处理多个请求（长连接）
        handle_client(client_fd);
    };

    if (!g_pool->append(task)) {
        std::cerr << "[错误] 线程池任务队列已满" << std::endl;
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
    }
}

int main() {
    // ========================================================
    // 第一步：创建 socket（创建一个通信端点）
    // AF_INET: IPv4 协议
    // SOCK_STREAM: TCP 协议（可靠、有序、双向）
    // ========================================================
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket 创建失败");
        return 1;
    }

    // 设置 SO_REUSEADDR，避免 "地址已被使用" 错误
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // ========================================================
    // 第二步：绑定地址（告诉操作系统：我要监听这个 IP + 端口）
    // ========================================================
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

    // ========================================================
    // 第三步：开始监听（告诉操作系统：我准备好接受连接了）
    // 第二个参数是 backlog：等待队列的最大长度
    // ========================================================
    if (listen(listen_fd, 128) < 0) {
        perror("listen 失败");
        close(listen_fd);
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  Web 服务器已启动" << std::endl;
    std::cout << "  监听端口: " << PORT << std::endl;
    std::cout << "  访问地址: http://localhost:" << PORT << std::endl;
    std::cout << "========================================" << std::endl;

    // ========================================================
    // 创建线程池（8 个工作线程）
    // ========================================================
    try {
        g_pool = new ThreadPool(8);
        std::cout << "[线程池] 已创建 8 个工作线程" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[错误] 线程池创建失败: " << e.what() << std::endl;
        close(listen_fd);
        return 1;
    }

    // ========================================================
    // 第四步：创建 epoll 实例
    // epoll_create 创建一个 epoll 实例，返回文件描述符
    // 参数 1 只是给内核一个提示，现在已废弃，填大于 0 的数就行
    // ========================================================
    g_epfd = epoll_create(1);
    if (g_epfd < 0) {
        perror("epoll_create 失败");
        delete g_pool;
        close(listen_fd);
        return 1;
    }
    std::cout << "[epoll] 创建成功，epfd = " << g_epfd << std::endl;

    // ========================================================
    // 第五步：把监听 socket 注册到 epoll
    // epoll_ctl 用于添加/修改/删除要监控的文件描述符
    // EPOLLIN 表示关心"可读"事件（有新连接到来）
    // 监听 socket 用 LT（水平触发），确保所有连接都能 accept
    // ========================================================
    struct epoll_event event;
    event.events = EPOLLIN;        // LT 模式（不用 EPOLLET）
    event.data.fd = listen_fd;     // 哪个文件描述符

    if (epoll_ctl(g_epfd, EPOLL_CTL_ADD, listen_fd, &event) < 0) {
        perror("epoll_ctl 失败");
        close(g_epfd);
        delete g_pool;
        close(listen_fd);
        return 1;
    }
    std::cout << "[epoll] 监听 socket 已注册" << std::endl;

    // ========================================================
    // 第六步：Reactor 事件循环
    // 主线程只负责事件分发，不处理业务逻辑
    // ========================================================
    struct epoll_event events[MAX_EVENTS];  // 存储就绪事件的数组

    while (true) {
        // 阻塞等待事件，-1 表示无限等待
        int ready = epoll_wait(g_epfd, events, MAX_EVENTS, -1);
        if (ready < 0) {
            perror("epoll_wait 失败");
            break;
        }

        // ====================================================
        // 第七步：事件分发
        // 主线程只负责"通知"，具体处理交给工作线程
        // ====================================================
        for (int i = 0; i < ready; i++) {
            int sockfd = events[i].data.fd;

            // 事件类型 1：新连接到达
            if (sockfd == listen_fd) {
                on_new_connection(listen_fd);
            }
            // 事件类型 2：数据到达
            else {
                on_data_arrival(sockfd);
            }
        }
    }

    // 清理资源
    close(g_epfd);
    delete g_pool;
    close(listen_fd);
    return 0;
}

// ============================================================
// 处理客户端请求（使用 HttpConn 类）
// 读取 HTTP 请求，解析请求行，返回对应的资源
//
// ET 模式下必须循环读取，直到返回 EAGAIN
// 为什么？ET 只通知一次，如果没读完，不会再通知你了
// ============================================================
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // ET 模式：循环读取，直到返回 EAGAIN（数据读完了）
    int total_read = 0;
    while (true) {
        int bytes_read = recv(client_fd, buffer + total_read,
                              sizeof(buffer) - total_read - 1, 0);

        if (bytes_read < 0) {
            // EAGAIN 表示数据读完了（非阻塞 IO 的正常情况）
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;  // 读完了，退出循环
            }
            // 其他错误
            perror("recv 失败");
            epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
            return;
        }

        if (bytes_read == 0) {
            // 对端关闭连接
            std::cout << "[连接关闭] 客户端断开" << std::endl;
            epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);
            close(client_fd);
            return;
        }

        // 成功读到数据
        total_read += bytes_read;

        // 防止缓冲区溢出
        if (total_read >= sizeof(buffer) - 1) {
            break;
        }
    }

    // 没读到数据
    if (total_read <= 0) {
        return;
    }

    // 打印收到的请求（调试用）
    std::cout << "[请求内容]\n" << buffer << std::endl;

    // ========================================================
    // 使用 HttpConn 类解析 HTTP 请求
    // ========================================================
    HttpConn http_conn;
    if (!http_conn.parse(buffer, total_read)) {
        std::string response = http_conn.build_response(400, "text/plain", "Bad Request");
        send(client_fd, response.c_str(), response.size(), 0);
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
        return;
    }

    // 获取解析结果
    HttpConn::Method method = http_conn.get_method();
    std::string path = http_conn.get_path();

    std::cout << "[解析] 方法=" << (method == HttpConn::GET ? "GET" : "POST")
              << " 路径=" << path << std::endl;

    // ========================================================
    // 处理 GET 请求
    // ========================================================
    bool keep_alive = http_conn.is_keep_alive();

    if (method == HttpConn::GET) {
        // 默认路径为 index.html
        if (path == "/") {
            path = "/index.html";
        }

        // 拼接文件路径（相对于 root 目录）
        std::string file_path = "root" + path;

        // 读取文件内容
        std::string content = read_file(file_path);

        if (content.empty()) {
            // 文件不存在，返回 404
            std::string response = http_conn.build_response(404, "text/html",
                "<html><body><h1>404 Not Found</h1><p>页面不存在</p></body></html>",
                keep_alive);
            send(client_fd, response.c_str(), response.size(), 0);
        } else {
            // 文件存在，返回 200
            std::string response = http_conn.build_response(200, "text/html",
                content, keep_alive);
            send(client_fd, response.c_str(), response.size(), 0);
        }
    }
    // ========================================================
    // 处理 POST 请求
    // ========================================================
    else if (method == HttpConn::POST) {
        // 处理 POST 请求
        std::string body = http_conn.get_body();
        std::string response;

        // 简单的 POST 处理示例
        if (path == "/api/submit") {
            // 返回接收到的数据
            response = http_conn.build_response(200, "text/plain",
                "收到 POST 数据：\n" + body, keep_alive);
        } else {
            response = http_conn.build_response(404, "text/plain",
                "API 不存在", keep_alive);
        }

        send(client_fd, response.c_str(), response.size(), 0);
    }
    // ========================================================
    // 其他方法
    // ========================================================
    else {
        std::string response = http_conn.build_response(405, "text/plain",
            "Method Not Allowed", keep_alive);
        send(client_fd, response.c_str(), response.size(), 0);
    }

    // ========================================================
    // 长连接处理
    // 如果是 keep-alive，不关闭连接，等 epoll 下次通知
    // 如果是 close，关闭连接
    // ========================================================
    if (keep_alive) {
        std::cout << "[长连接] 保持连接，等待下一次请求" << std::endl;
    } else {
        // 关闭连接（HTTP/1.0 短连接）
        epoll_ctl(g_epfd, EPOLL_CTL_DEL, client_fd, nullptr);
        close(client_fd);
        std::cout << "[短连接] 响应已发送，连接关闭" << std::endl;
    }
}

// ============================================================
// 读取文件内容
// ============================================================
std::string read_file(const std::string& path) {
    std::ifstream file(path, std::ios::in | std::ios::binary);
    if (!file.is_open()) {
        return "";
    }

    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

