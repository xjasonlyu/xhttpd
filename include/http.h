#ifndef _HTTPCONNECTION_H_
#define _HTTPCONNECTION_H_

#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>

#include "mutex.h"

#define OK_200_TITLE "OK"
#define ERROR_400_TITLE "Bad Request"
#define ERROR_400_form "Your request has bad syntax or is inherently impossible to satisfy.\n"
#define ERROR_403_TITLE "Forbidden"
#define ERROR_403_form "You do not have permission to get file from this server.\n"
#define ERROR_404_TITLE "Not Found"
#define ERROR_404_form "The requested file was not found on this server.\n"
#define ERROR_500_TITLE "Internal Error"
#define ERROR_500_form "There was an unusual problem serving the requested file.\n"

#define MAX_FD (1 << 16)
#define MAX_EVENT_NUMBER (8 << 10)

#define BUFFER_SIZE (2 << 10)
#define FILENAME_LEN 0xFF

// HTTP Methods
enum METHOD {
    GET,
    POST,
    HEAD,
    PUT,
    DELETE,
    TRACE,
    OPTIONS,
    CONNECT,
    PATCH
};

// HTTP State
enum CHECK_STATE {
    CHECK_STATE_REQUESTLINE,
    CHECK_STATE_HEADER,
    CHECK_STATE_CONTENT
};

// HTTP Code
enum HTTP_CODE {
    NO_REQUEST,
    GET_REQUEST,
    BAD_REQUEST,
    NO_RESOURCE,
    FORBIDDEN_REQUEST,
    FILE_REQUEST,
    INTERNAL_ERROR,
    CLOSED_CONNECTION
};

// LINE Status
enum LINE_STATUS {
    LINE_OK,
    LINE_BAD,
    LINE_OPEN
};

class HTTPServer {
  public:
    HTTPServer(const char *, int, const char *);
    ~HTTPServer();

  public:
    int serve_forever();

  private:
    void show_error(int, const char *);

  private:
    int epoll_fd;
    int listen_fd;

    char doc_root[FILENAME_LEN];
    struct sockaddr_in address;
};

class HTTPConn {
  public:
    HTTPConn() {}
    ~HTTPConn() {}

  public:
    void init(int sock_fd, const sockaddr_in &addr);

    void close_conn(bool real_close = true);

    void process();

    bool read();

    bool write();

  private:
    void init();

    void show_request(const char *);

    bool process_write(HTTP_CODE ret);

    HTTP_CODE process_read();

    HTTP_CODE parse_request_line(char *text);

    HTTP_CODE parse_headers(char *text);

    HTTP_CODE parse_content(char *text);

    HTTP_CODE do_request();

    char *get_line() { return m_read_buf + m_start_line; }

    LINE_STATUS parse_line();

    void unmap();

    bool add_response(const char *format, ...);

    bool add_content(const char *content);

    bool add_status_line(int status, const char *title);

    bool add_headers(int content_length);

    bool add_content_length(int content_length);

    bool add_linger();

    bool add_blank_line();

  public:
    static int m_epoll_fd;
    static int m_conn_count;

    const char *doc_root;

  private:
    int m_sock_fd;
    sockaddr_in m_address;

    char m_read_buf[BUFFER_SIZE];
    int m_read_idx;
    int m_checked_idx;
    int m_start_line;
    char m_write_buf[BUFFER_SIZE];
    int m_write_idx;

    CHECK_STATE m_check_state;
    METHOD m_method;

    char m_real_file[FILENAME_LEN];
    char *m_url;
    char *m_version;
    char *m_host;
    int m_content_length;
    bool m_linger;

    char *m_file_address;
    struct stat m_file_stat;
    struct iovec m_iv[2];
    int m_iv_count;
};

#endif
