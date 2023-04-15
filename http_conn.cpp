#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include "http_conn.h"

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
}

void HttpConn::close_conn() {
    if (m_sock_fd != -1) {
        remove_fd(m_epoll_fd, m_sock_fd);
        m_sock_fd = -1;
        m_user_cnt--;
    }
}

bool HttpConn::read() {
    printf("一次性读完\n");
    return true;
}

bool HttpConn::write() {
    printf("一次性写完\n");
    return true;
}

// called by workthread
void HttpConn::process() {
    // parse request
    printf("parse request\n");
    // send response
    printf("create response\n");
}