#ifndef _HTTP_CONN
#define _HTTP_CONN
#include <sys/epoll.h>
#include <arpa/inet.h>
#include <string>

using std::string;

class HttpConn {
public:
    static int m_epoll_fd;
    static int m_user_cnt;
    static const int READ_BUFFER_SIZE = 2048;
    static const int WRITE_BUFFER_SIZE = 2048;
    
    /*
        State Machine
    */
    // HTTP Request METHOD
    enum METHOD { GET = 0, POST, PUT, DELETE };

    /*
        解析客户端请求时，主状态机的状态
        CHECK_STATE_REQUESTLINE:当前正在分析请求行
        CHECK_STATE_HEADER:当前正在分析头部字段
        CHECK_STATE_CONTENT:当前正在解析请求体
    */
    enum CHECK_MAIN_STATE { CHECK_STATE_REQUESTLINE = 0, CHECK_STATE_HEADER, CHECK_STATE_CONTENT };

    /*
        服务器处理HTTP请求的可能结果，报文解析的结果
        NO_REQUEST          :   请求不完整，需要继续读取客户数据
        GET_REQUEST         :   表示获得了一个完成的客户请求
        BAD_REQUEST         :   表示客户请求语法错误
        NO_RESOURCE         :   表示服务器没有资源
        FORBIDDEN_REQUEST   :   表示客户对资源没有足够的访问权限
        FILE_REQUEST        :   文件请求,获取文件成功
        INTERNAL_ERROR      :   表示服务器内部错误
        CLOSED_CONNECTION   :   表示客户端已经关闭连接了
    */
    enum HTTP_CODE { NO_REQUEST, GET_REQUEST, BAD_REQUEST, NO_RESOURCE, FORBIDDEN_REQUEST, FILE_REQUEST, INTERNAL_ERROR, CLOSED_CONNECTION };

    // 从状态机的三种可能状态，即行的读取状态，分别表示
    // 1.读取到一个完整的行 2.行出错 3.行数据尚且不完整
    enum LINE_STATUS { LINE_OK = 0, LINE_BAD, LINE_OPEN };

public:
    HttpConn() {}
    HttpConn(int conn_fd, sockaddr_in& client_addr);
    ~HttpConn() {}
    
    void process(); // deal with client request
    
    bool read(); // non-blocking read
    bool write(); // non-blocking write
    
    void close_conn();


private:
    void init();

private:
    int m_check_index{0};
    int m_start_line{0};
    CHECK_MAIN_STATE m_check_state; // the state of main state machine

    string m_url{};
    string m_version{};
    METHOD m_method;
    string m_host;
    bool m_keep_alive{false};

    HTTP_CODE process_read_request();
    HTTP_CODE parse_request_line(char* text);
    HTTP_CODE parse_header(char* text);
    HTTP_CODE parse_content(char* text);

    LINE_STATUS parse_line();
    inline char* get_line() { return m_read_buf + m_start_line; }

    HTTP_CODE do_request();

private:
    int m_sock_fd; // socket fd for this http connect
    sockaddr_in m_address;

    char m_read_buf[READ_BUFFER_SIZE];
    int m_read_index{0};

    char m_write_buf[WRITE_BUFFER_SIZE];
};

#endif