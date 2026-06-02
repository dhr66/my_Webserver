#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "locker.h"
#include "http_conn.h"
#include "threadpool.h"

// ============================================================
// 最简 Web 服务器 (Day1)
// 功能：接收 HTTP 请求，返回静态网页
// 核心流程：socket → bind → listen → select → accept → 收发数据
// ============================================================

// 服务器配置
const int PORT = 8080;           // 监听端口
const int MAX_EVENTS = 1024;     // select 最大监听数
const int BUFFER_SIZE = 4096;    // 读写缓冲区大小

// 函数声明
void handle_client(int client_fd);
std::string read_file(const std::string& path);

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
    ThreadPool* pool = nullptr;
    try {
        pool = new ThreadPool(8);
        std::cout << "[线程池] 已创建 8 个工作线程" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[错误] 线程池创建失败: " << e.what() << std::endl;
        close(listen_fd);
        return 1;
    }

    // ========================================================
    // 第四步：使用 select 实现 I/O 多路复用
    // select 可以同时监听多个文件描述符
    // 当任何一个有数据可读时，select 返回
    // ========================================================
    fd_set read_fds;        // 可读文件描述符集合
    int max_fd = listen_fd; // 当前最大的文件描述符

    while (true) {
        // 每次循环都要重新设置，因为 select 会修改这个集合
        FD_ZERO(&read_fds);            // 清空集合
        FD_SET(listen_fd, &read_fds);  // 把监听 socket 加入集合

        // 等待任意一个文件描述符可读（阻塞）
        // 最后一个参数 nullptr 表示无限等待
        int ready = select(max_fd + 1, &read_fds, nullptr, nullptr, nullptr);
        if (ready < 0) {
            perror("select 失败");
            break;
        }

        // ====================================================
        // 第五步：检查哪些文件描述符就绪
        // ====================================================

        // 如果监听 socket 可读，说明有新连接到来
        if (FD_ISSET(listen_fd, &read_fds)) {
            struct sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);

            // accept 接受连接，返回一个新的 socket 用于通信
            int client_fd = accept(listen_fd, (struct sockaddr*)&client_addr, &client_len);
            if (client_fd < 0) {
                perror("accept 失败");
                continue;
            }

            std::cout << "[新连接] 客户端: "
                      << inet_ntoa(client_addr.sin_addr) << ":"
                      << ntohs(client_addr.sin_port) << std::endl;

            // 使用线程池处理客户端请求（异步）
            // 创建任务：捕获 client_fd，调用 handle_client
            std::function<void()> task = [client_fd]() {
                handle_client(client_fd);
            };

            // 添加任务到线程池
            if (!pool->append(task)) {
                std::cerr << "[错误] 线程池任务队列已满，拒绝连接" << std::endl;
                close(client_fd);
            }
        }
    }

    // 清理线程池
    delete pool;
    close(listen_fd);
    return 0;
}

// ============================================================
// 处理客户端请求（使用 HttpConn 类）
// 读取 HTTP 请求，解析请求行，返回对应的资源
// ============================================================
void handle_client(int client_fd) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    // 读取客户端发送的数据
    int bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (bytes_read <= 0) {
        close(client_fd);
        return;
    }

    // 打印收到的请求（调试用）
    std::cout << "[请求内容]\n" << buffer << std::endl;

    // ========================================================
    // 使用 HttpConn 类解析 HTTP 请求
    // ========================================================
    HttpConn http_conn;
    if (!http_conn.parse(buffer, bytes_read)) {
        std::string response = http_conn.build_response(400, "text/plain", "Bad Request");
        send(client_fd, response.c_str(), response.size(), 0);
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
                "<html><body><h1>404 Not Found</h1><p>页面不存在</p></body></html>");
            send(client_fd, response.c_str(), response.size(), 0);
        } else {
            // 文件存在，返回 200
            std::string response = http_conn.build_response(200, "text/html", content);
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
                "收到 POST 数据：\n" + body);
        } else {
            response = http_conn.build_response(404, "text/plain", "API 不存在");
        }

        send(client_fd, response.c_str(), response.size(), 0);
    }
    // ========================================================
    // 其他方法
    // ========================================================
    else {
        std::string response = http_conn.build_response(405, "text/plain", "Method Not Allowed");
        send(client_fd, response.c_str(), response.size(), 0);
    }

    // 关闭连接（HTTP/1.0 短连接）
    close(client_fd);
    std::cout << "[完成] 响应已发送" << std::endl;
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

