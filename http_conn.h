#ifndef HTTP_CONN_H
#define HTTP_CONN_H

#include <string>
#include <map>
#include <sstream>
#include <cstring>

// ============================================================
// HTTP 连接处理类 (Day2)
// 功能：使用状态机解析 HTTP 请求
// 状态机：按顺序解析 请求行 → 请求头 → 请求体
//状态机就是：按顺。序处理每个部分，
//每处理完一部分就进入下一个状态
// ============================================================

class HttpConn {
public:
    // HTTP 请求状态（状态机的三个状态）
    enum ParseState {
        REQUEST_LINE,  // 解析请求行
        HEADERS,       // 解析请求头
        BODY,          // 解析请求体
        FINISH         // 解析完成
    };

    // HTTP 请求方法
    enum Method {
        GET,
        POST,
        PUT,
        DELETE,
        UNKNOWN
    };

    // HTTP 请求结构
    struct Request {
        Method method;                              // 请求方法
        std::string path;                           // 请求路径
        std::string version;                        // HTTP 版本
        std::map<std::string, std::string> headers; // 请求头
        std::string body;                           // 请求体
        int content_length;                         // Content-Length
        bool keep_alive;                            // 是否长连接
    };

    // HTTP 响应结构
    struct Response {
        int status_code;                            // 状态码
        std::string status_text;                    // 状态文本
        std::map<std::string, std::string> headers; // 响应头
        std::string body;                           // 响应体
    };

public:
    HttpConn() : m_state(REQUEST_LINE), m_request{} {
        m_request.content_length = 0;
        m_request.keep_alive = false;  // 默认短连接
    }

    ~HttpConn() {}

    // ============================================================
    // 解析 HTTP 请求（状态机核心）
    // 参数：buffer - 接收到的数据
    //       len    - 数据长度
    // 返回：true - 解析成功，false - 解析失败
    // ============================================================
    bool parse(const char* buffer, int len) {
        std::string data(buffer, len);
        std::istringstream stream(data);
        std::string line;

        while (std::getline(stream, line)) {
            // 移除行尾的 \r
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }

            // 根据当前状态处理
            switch (m_state) {
                case REQUEST_LINE:
                    if (!parse_request_line(line)) {
                        return false;
                    }
                    break;

                case HEADERS:
                    if (!parse_header(line)) {
                        // 空行表示请求头结束
                        if (m_request.content_length > 0) {
                            m_state = BODY;
                        } else {
                            m_state = FINISH;
                        }
                    }
                    break;

                case BODY:
                    // 读取请求体
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

    // ============================================================
    // 解析请求行
    // 格式：GET /index.html HTTP/1.1
    // ============================================================
    bool parse_request_line(const std::string& line) {
        std::istringstream stream(line);
        std::string method, path, version;

        stream >> method >> path >> version;

        if (method.empty() || path.empty() || version.empty()) {
            return false;
        }

        // 解析方法
        if (method == "GET") {
            m_request.method = GET;
        } else if (method == "POST") {
            m_request.method = POST;
        } else if (method == "PUT") {
            m_request.method = PUT;
        } else if (method == "DELETE") {
            m_request.method = DELETE;
        } else {
            m_request.method = UNKNOWN;
        }

        m_request.path = path;
        m_request.version = version;

        // 解析查询参数（GET 请求）
        // GET 请求：/index.html?name=John&age=30
        size_t pos = path.find('?');
        if (pos != std::string::npos) {
            m_request.path = path.substr(0, pos);
            m_query_string = path.substr(pos + 1);
        }

        // 转换为状态 HEADERS
        m_state = HEADERS;
        return true;
    }

    // ============================================================
    // 解析请求头
    // 格式：Key: Value
    // ============================================================
    bool parse_header(const std::string& line) {
        // 空行表示请求头结束
        if (line.empty()) {
            return false;
        }

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

        // 检查 Connection 头
        if (key == "Connection") {
            // 去除前导空格
            while (!value.empty() && value[0] == ' ') {
                value = value.substr(1);
            }
            m_request.keep_alive = (value == "keep-alive");
        }

        return true;
    }

    // ============================================================
    // 获取解析结果
    // ============================================================
    const Request& get_request() const {
        return m_request;
    }

    Method get_method() const {
        return m_request.method;
    }

    const std::string& get_path() const {
        return m_request.path;
    }

    const std::string& get_version() const {
        return m_request.version;
    }

    const std::map<std::string, std::string>& get_headers() const {
        return m_request.headers;
    }

    const std::string& get_body() const {
        return m_request.body;
    }

    const std::string& get_query_string() const {
        return m_query_string;
    }

    bool is_keep_alive() const {
        return m_request.keep_alive;
    }

    bool is_finished() const {
        return m_state == FINISH;
    }

    // ============================================================
    // 构建 HTTP 响应
    // ============================================================
    std::string build_response(int status_code, const std::string& content_type,
                               const std::string& body, bool keep_alive = false) {
        std::string status_text;
        switch (status_code) {
            case 200: status_text = "OK"; break;
            case 404: status_text = "Not Found"; break;
            case 405: status_text = "Method Not Allowed"; break;
            case 500: status_text = "Internal Server Error"; break;
            default: status_text = "Unknown"; break;
        }

        std::ostringstream response;
        response << "HTTP/1.1 " << status_code << " " << status_text << "\r\n"
                 << "Content-Type: " << content_type << "\r\n"
                 << "Content-Length: " << body.size() << "\r\n"
                 << "Connection: " << (keep_alive ? "keep-alive" : "close") << "\r\n"
                 << "\r\n"
                 << body;

        return response.str();
    }

    // ============================================================
    // 重置解析器（复用连接）
    // ============================================================
    void reset() {
        m_state = REQUEST_LINE;
        m_request = Request{};
        m_request.content_length = 0;
        m_query_string.clear();
    }

private:
    ParseState m_state;     // 当前解析的请求状态
    Request m_request;      // 请求的解析结果
    std::string m_query_string; // 查询参数
};

#endif // HTTP_CONN_H
