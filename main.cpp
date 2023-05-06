#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <cassert>
#include <iostream>
#include <functional>
#include <memory>
#include <unordered_map>
#include "threadpool.h"
#include "http_conn.h"

#define MAX_FD 65535
#define MAX_EPOLL_NUMBER 10000

using std::function;
using std::unique_ptr;
using std::vector;
using std::unordered_map;

int HttpConn::m_epoll_fd = -1;
int HttpConn::m_user_cnt = 0;

// register signal handler
void add_sig(int sig, void( handler )(int)) {
    struct sigaction sa;
    memset(&sa, '\0', sizeof(sa));
    sa.sa_handler = handler;
    sigfillset(&sa.sa_mask);
    assert(sigaction(sig, &sa, NULL) != -1);
}

extern void add_fd(int epoll_fd, int fd, bool one_shot);
extern void remove_fd(int epoll_fd, int fd);
extern void mod_fd(int epoll_fd, int fd, int event);

int main(int argc, char** argv) {
    if (argc <= 1) {
        std::cout << "Usage: " << basename(argv[0]) << " <port number>" << std::endl;
        exit(0);
    }

    int port = std::stoi(argv[1]);

    add_sig(SIGPIPE, SIG_IGN);

    // initialize threadpool
    unique_ptr<Threadpool> pool;
    try {
        pool = std::make_unique<Threadpool>();
    } catch(...) {
        return 1;
    }

    // store client connection
    unordered_map<int, HttpConn> users;

    // socket
    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    int reuse = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    // bind
    struct sockaddr_in soaddr;
    soaddr.sin_family = AF_INET;
    soaddr.sin_addr.s_addr = INADDR_ANY;
    soaddr.sin_port = htons(port);
    bind(listenfd, (struct sockaddr*)&soaddr, sizeof(soaddr));

    // listen
    listen(listenfd, 5);

    // epoll
    epoll_event* ep_events = new epoll_event[MAX_EPOLL_NUMBER];
    int epoll_fd = epoll_create(10);

    // add listen fd to epoll
    add_fd(epoll_fd, listenfd, false);
    HttpConn::m_epoll_fd = epoll_fd;

    while (true) {
        int cnt = epoll_wait(HttpConn::m_epoll_fd, ep_events, MAX_EPOLL_NUMBER, -1);
        if (cnt < 0 && errno == EINTR) {
            std::cerr << "epoll_wait failed\n";
            exit(-1);
        }

        for (int i = 0; i < cnt; i++) {
            int sockfd = ep_events[i].data.fd;
            if (sockfd == listenfd) {
                struct sockaddr_in client_addr;
                socklen_t client_addr_len = sizeof(client_addr);

                int conn_fd = accept(sockfd, (struct sockaddr*)&client_addr, &client_addr_len);
                if (conn_fd == -1) {
                    std::cerr << "accept failed\n";
                    continue;
                }

                if (HttpConn::m_user_cnt >= MAX_FD) {
                    close(conn_fd);
                    continue;
                }
                printf("a new user connect\n");
                users.insert(std::make_pair(conn_fd, HttpConn(conn_fd, client_addr)));
            } else if (ep_events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                users[sockfd].close_conn();
            } else if (ep_events[i].events & EPOLLIN) {
                //mod_fd(HttpConn::m_epoll_fd, sockfd, EPOLLIN); // change to lt
                if (users[sockfd].read()) {
                    printf("something to read\n");
                    function<void()> task = [&](){ users[sockfd].process(); };
                    pool->append(task);
                } else {
                    users[sockfd].close_conn();
                }
            } else if (ep_events[i].events & EPOLLOUT) {
                printf("write!\n");
                if (!users[sockfd].write()) {
                    users[sockfd].close_conn(); 
                }
            }
        }
    }

    close(listenfd);
    close(epoll_fd);
    delete [] ep_events;

    return 0;
}