#include <iostream>
#include <string>
#include <sstream>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <cstring>
#include "http_conn.h"

using std::string;
using std::istringstream;
using std::ifstream;
using std::unordered_map;
using std::replace;

const string static_directory = "/home/xiaofei/Project/webserver/static";

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
    printf("current user: %d\n", m_user_cnt);

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
    //m_header_map.clear();

    m_keep_alive = false;

    // clear buffer
    bzero(m_read_buf, READ_BUFFER_SIZE);
    //bzero(m_write_buf, WRITE_BUFFER_SIZE);

}

void HttpConn::close_conn() {
    if (m_sock_fd != -1) {
        remove_fd(m_epoll_fd, m_sock_fd);
        m_sock_fd = -1;
        m_user_cnt--;
        printf("a user close connect\n");
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
    if (bytes_to_send == 0) {
        mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN|EPOLLONESHOT|EPOLLET);
        init();
        return true;
    }

    int cnt = 0;
    while (true) {
        cnt = writev(m_sock_fd, m_iv, m_iv_count);
        if (cnt <= -1) {
            if((errno==EAGAIN)||(errno==EINTR)){
                    mod_fd(m_epoll_fd, m_sock_fd, EPOLLOUT|EPOLLONESHOT|EPOLLET);
                    return true;
                }
                unmap();
                return false;
        }

        bytes_have_sent += cnt;
        bytes_to_send -= cnt;

        if (bytes_have_sent >= m_iv[0].iov_len) {
            m_iv[0].iov_len = 0;
            m_iv[1].iov_base = m_file_address+(bytes_have_sent - m_write_index);
            m_iv[1].iov_len = bytes_to_send;
        } else {
            m_iv[0].iov_base = m_write_buf + bytes_have_sent;
            m_iv[0].iov_len = m_iv[0].iov_len - cnt;
        }

        if (bytes_to_send <= 0) {
            unmap();
            mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN|EPOLLET|EPOLLONESHOT);

            if (m_header_map.find("Connection") != m_header_map.end()) {
                // if (m_header_map["Connection"] == "keep-alive") {
                //     init();
                //     return true;
                // }
                return false;
            } else {
                return false;
            }
        }
    }
}

/// @brief the entry of main state machine
/// @return the result of parsing request
HttpConn::HTTP_CODE HttpConn::process_read_request() {
    LINE_STATUS line_stat = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;

    char* text = 0;
    //printf("sbsbsbs\n");
    while ((m_check_state == CHECK_STATE_CONTENT && line_stat == LINE_OK)
            || (line_stat = parse_line()) == LINE_OK) {
                //printf("readreadread\n");
                text = get_line();
                m_start_line = m_check_index;
                //printf("get 1 http line: %s\n", text);

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
                            return do_request();
                        }
                        break;

                    case CHECK_STATE_CONTENT:
                        ret = parse_content(text);
                        if (ret == BAD_REQUEST) {
                            return BAD_REQUEST;
                        } else if (ret == GET_REQUEST) {
                            return do_request();
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
    string method{};
    string url{};
    string version{};
    istringstream req_stream(text);

    req_stream >> method >> url >> version;
    if (method == "GET") {
        m_method = GET;
    } else {
        return BAD_REQUEST;
    }

    string prefix = "/";
    if (url.compare(0, prefix.length(), prefix) == 0) {
        m_url = url;
    } else {
        return BAD_REQUEST;
    }

    prefix = "HTTP/";
    if (version.compare(0, prefix.length(), prefix) == 0) {
        m_version = version;
    } else {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER; // check_state: request_line -> header

    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_header(char* text) {
    //printf("the text is %s\n", text);
    string header(text);
    string key, value;
    //std::cout << "head is: " << header << std::endl;
    //std::cout << "head size: " << header.length() << std::endl;
    if (header.size() == 0) {
        //printf("end header\n");
        if (m_header_map.find("Content-Length") == m_header_map.end()) {
            // indicates that there is a request body
            //printf("no content\n");
            m_check_state = CHECK_STATE_CONTENT;
            //return NO_REQUEST;
            return GET_REQUEST;
        }

        // if no request body, get a complete request
        //printf("have content\n");
        return GET_REQUEST;
    }

    auto pos = header.find(":");
    if (pos != string::npos) {
        header.replace(pos, 1, " ");
    }
    //std::cout << header << std::endl;
    istringstream header_stream(header);
    header_stream >> key >> value;
    //std::cout << key << value << std::endl;
    if (key.empty() || value.empty()) {
        printf("empty\n");
        return BAD_REQUEST;
    }

    if (m_header_map.find(key) != m_header_map.end()) {
        return BAD_REQUEST;
    }
    //printf("hh\n");
    m_header_map.insert(std::make_pair(key, value));
    //printf("yy\n");
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::parse_content(char* text) {
    int content_length = 0;
    if (m_header_map.find("Content-Length") != m_header_map.end()) {
        content_length = std::stoi(m_header_map["Content-Length"]);
    }
    if (m_read_index >= (m_check_index + content_length)) {
        text[content_length] = '\0';
        printf("get request\n");
        return GET_REQUEST;
    }

    printf("parse_content finish\n");
    return NO_REQUEST;
}

HttpConn::HTTP_CODE HttpConn::do_request() {
    string request_file = static_directory + m_url;
    stat(const_cast<char*>(request_file.c_str()), &m_file_stat );
    int fd = open(request_file.c_str(), O_RDONLY);

    m_file_address = (char*)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    return FILE_REQUEST;
}

bool HttpConn::process_write_response(HTTP_CODE read_ret) {
    switch (read_ret) {
        case FILE_REQUEST:
            //printf("here for index html\n");
            add_default_res();
            //printf("after here for index html\n");
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_index;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;

            bytes_to_send = m_write_index + m_file_stat.st_size;

            return true;
    }
}

void HttpConn::add_default_res() {
    int len = 0;
    len += snprintf(m_write_buf + m_write_index, WRITE_BUFFER_SIZE - m_write_index,
                    "HTTP/1.1 200 OK\r\n");
    len += snprintf(m_write_buf + m_write_index + len, WRITE_BUFFER_SIZE - m_write_index - len,
                    "Content-Length: %ld\r\n", m_file_stat.st_size);

    if (m_url == "/Forever.jpg") {
        len += snprintf(m_write_buf + m_write_index + len, WRITE_BUFFER_SIZE - m_write_index - len,
                    "Content-Type: image/jpg\r\n");
    } else {
        len += snprintf(m_write_buf + m_write_index + len, WRITE_BUFFER_SIZE - m_write_index - len,
                    "Content-Type: text/html\r\n");
    }
    // len += snprintf(m_write_buf + m_write_index + len, WRITE_BUFFER_SIZE - m_write_index - len,
    //                 "Connection: Keep-alive");
    len += snprintf(m_write_buf + m_write_index + len, WRITE_BUFFER_SIZE - m_write_index - len,
                    "\r\n");
    m_write_index += len;
    printf("write buffer: %s\n", m_write_buf);
}

void HttpConn::add_status_line(int code, const char* title) {

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
    bool write_ret = process_write_response(read_ret);
    if (!write_ret) {
        close_conn();
    }
    mod_fd(m_epoll_fd, m_sock_fd, EPOLLOUT|EPOLLONESHOT|EPOLLET);
    printf("create response\n");
}

// clear memory mapping
void HttpConn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}