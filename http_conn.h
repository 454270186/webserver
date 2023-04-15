#ifndef _HTTP_CONN
#define _HTTP_CONN
#include <sys/epoll.h>

class HttpConn {
public:
    static int m_epoll_fd;
    static int m_user_cnt;

    HttpConn() {}
    HttpConn(int conn_fd, sockaddr_in& client_addr);
    ~HttpConn() {}

    void process(); // deal with client request
    void close_conn();
    bool read(); // non-blocking read
    bool write(); // non-blocking write

private:
    int m_sock_fd; // socket fd for this http connect
    sockaddr_in m_address;
};

#endif