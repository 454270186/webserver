#include <iostream>
#include <string>
#include <sstream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <cstring>
#include "http_conn.h"

using std::string;
using std::istringstream;

void set_non_block(int fd) {
    int flag = fcntl(fd, F_GETFL);
    flag |= O_NONBLOCK;
    fcntl(fd, F_SETFL, flag);
}

void add_fd(int epoll_fd, int fd, bool one_shot) {
    epoll_event epev;
    epev.events = EPOLLIN | EPOLLRDHUP;
    epev.data.fd = fd;

    if (one_shot) {
        epev.events |= EPOLLONESHOT;
    }

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &epev);
    set_non_block(fd);
}

void remove_fd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    close(fd);
}

void mod_fd(int epoll_fd, int fd, int event) {
    epoll_event epev;
    epev.events = event | EPOLLONESHOT | EPOLLRDHUP;
    epev.data.fd = fd;

    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &epev);
}

/*
    HttpConn
*/
HttpConn::HttpConn(int conn_fd, sockaddr_in& client_addr) : m_sock_fd(conn_fd), m_address(client_addr) {
    // set port reuse
    int reuse = 1;
    setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // add to epoll
    add_fd(m_epoll_fd, m_sock_fd, true);
    m_user_cnt++;

    init();
}

void HttpConn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_method = GET;

    m_read_index = 0;
    m_check_index = 0;
    m_start_line = 0;
    m_url = {};
    m_version = {};
    m_host = {};
    m_keep_alive = false;

    // clear buffer
    bzero(m_read_buf, READ_BUFFER_SIZE);
}

void HttpConn::close_conn() {
    if (m_sock_fd != -1) {
        remove_fd(m_epoll_fd, m_sock_fd);
        m_sock_fd = -1;
        m_user_cnt--;
    }
}

bool HttpConn::read() {
    if (m_read_index >= READ_BUFFER_SIZE) {
        std::cerr << "out of read buffer\n";
        return false;
    }

    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sock_fd, m_read_buf + m_read_index, READ_BUFFER_SIZE, 0);
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // no data to read
                break;
            }

            return false;
        } else if (bytes_read == 0) {
            // connection close
            return false;
        }

        m_read_index += bytes_read;
    }

    printf("read data: %s\n", m_read_buf);
    return true;
}

bool HttpConn::write() {
    printf("一次性写完\n");
    return true;
}

/// @brief the entry of main state machine
/// @return the result of parsing request
HttpConn::HTTP_CODE HttpConn::process_read_request() {
    LINE_STATUS line_stat = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    printf("sbsbsbs\n");
    while ((m_check_state == CHECK_STATE_CONTENT && line_stat == LINE_OK)
            || (line_stat = parse_line()) == LINE_OK) {
                text = get_line();
                m_start_line = m_check_index;
                printf("get 1 http line: %s\n", text);

                switch (m_check_state) {
                    case CHECK_STATE_REQUESTLINE:
                        ret = parse_request_line(text);
                        if (ret == BAD_REQUEST) {
                            return BAD_REQUEST;
                        }
                        break;
                    
                    case CHECK_STATE_HEADER:
                        ret = parse_header(text);
                        if (ret == BAD_REQUEST) {
                            return BAD_REQUEST;
                        } else if (ret == GET_REQUEST) {
                            do_request();
                        }

                        break;

                    case CHECK_STATE_CONTENT:
                        ret = parse_content(text);
                        if (ret == BAD_REQUEST) {
                            return BAD_REQUEST;
                        } else if (ret == GET_REQUEST) {
                            do_request();
                        }

                        line_stat = LINE_OPEN;
                        break;

                    default:
                        return INTERNAL_ERROR;
                }
            }

    return NO_REQUEST;
}

/// @brief parse one line ending with \r\n
/// @return line status
HttpConn::LINE_STATUS HttpConn::parse_line() {
    char temp = 0;
    for (; m_check_index < m_read_index; m_check_index++) {
        temp = m_read_buf[m_check_index];
        if (temp == '\r') {
            if ((m_check_index + 1) == m_read_index) {
                return LINE_BAD;
            } else if (m_read_buf[m_check_index+1] == '\n') {
                m_read_buf[m_check_index++] = '\0';
                m_read_buf[m_check_index++] = '\0';
                return LINE_OK;
            }
            printf("1 bu ok\n");
            return LINE_BAD;
        } else if (temp == '\n') {
            if ((m_check_index > 1) && (m_read_buf[m_check_index-1] == '\r')) {
                m_read_buf[m_check_index-1] = '\0';
                m_read_buf[m_check_index++] = '\0';
                return LINE_OK;
            }
            printf("2 bu ok\n");
            return LINE_BAD;
        }
    }
    printf("open\n");
    return LINE_OPEN;
}

/// @brief parse http request [METHOD URL HTTP-Version]
/// @param text 
/// @return HTTP_CODE
/// @example GET / HTTP/1.1
HttpConn::HTTP_CODE HttpConn::parse_request_line(char* text) {
    //string req_line(text);
    string method;
    string url;
    string version;
    istringstream req_stream(text);

    req_stream >> method >> url >> version;

    std::cout << method << url << version << std::endl;
    exit(0);
    return GET_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_header(char* text) {

}

HttpConn::HTTP_CODE HttpConn::parse_content(char* text) {

}

HttpConn::HTTP_CODE HttpConn::do_request() {

}

// called by workthread
void HttpConn::process() {
    // parse request
    HTTP_CODE read_ret = process_read_request();
    if (read_ret == NO_REQUEST) {
        mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN);
        return;
    }

    // send response
    printf("create response\n");
}