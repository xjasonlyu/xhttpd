#include "http.h"
#include "threadpool.h"

inline int set_nonblocking(int fd) {
    int opt = fcntl(fd, F_GETFL);
    return fcntl(fd, F_SETFL, opt | O_NONBLOCK);
}

static void add_fd(int epoll_fd, int fd, bool one_shot) {
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    if (one_shot) {
        event.events |= EPOLLONESHOT;
    }
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    set_nonblocking(fd);
}

static void remove_fd(int epoll_fd, int fd) {
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, 0);
    close(fd);
}

static void mod_fd(int epoll_fd, int fd, int ev) {
    epoll_event event;
    event.data.fd = fd;
    event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;
    epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &event);
}

/*
    class HTTPServer
*/

HTTPServer::HTTPServer(const char *host, int port, const char *path) {
    // address init
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, host, &(address.sin_addr));
    address.sin_port = htons(port);

    // serve dir
    strncpy(doc_root, path, FILENAME_LEN);

    // init message
    printf("*) HTTPD serve %s and listen at %s:%d\n", doc_root, host, port);
}

HTTPServer::~HTTPServer() {
    // release resources
    close(epoll_fd);
    close(listen_fd);
}

void HTTPServer::show_error(int conn_fd, const char *info) {
    printf("error: %s", info);
    send(conn_fd, info, strlen(info), 0);
    close(conn_fd);
}

int HTTPServer::serve_forever() {
    HTTPConn *conns;
    threadpool<HTTPConn> *pool = NULL;
    // create threadpool
    try {
        pool = new threadpool<HTTPConn>;
    } catch (...) {
        return 1;
    }

    conns = new HTTPConn[MAX_FD];
    assert(conns);

    for (int i = 0; i < MAX_FD; i++)
        (conns + i)->doc_root = doc_root;

    int conn_count = 0;

    listen_fd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listen_fd >= 0);

    int ret = 0, flag = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &flag, sizeof(flag));
    ret = bind(listen_fd, (struct sockaddr *)&address, sizeof(address));
    assert(ret >= 0);

    ret = listen(listen_fd, 20);
    assert(ret >= 0);

    epoll_event events[MAX_EVENT_NUMBER];
    int epoll_fd = epoll_create(5);
    assert(epoll_fd != -1);
    add_fd(epoll_fd, listen_fd, false);
    HTTPConn::m_epoll_fd = epoll_fd;

    while (true) {
        int number = epoll_wait(epoll_fd, events, MAX_EVENT_NUMBER, -1);
        if ((number < 0) && (errno != EINTR)) {
            printf("epoll failure\n");
            break;
        }

        for (int i = 0; i < number; ++i) {
            int sock_fd = events[i].data.fd;
            if (sock_fd == listen_fd) {
                struct sockaddr_in client_address;
                socklen_t client_addrlength = sizeof(client_address);
                int conn_fd = accept(listen_fd, (struct sockaddr *)&client_address, &client_addrlength);
                if (conn_fd < 0) {
                    printf("errno is: %d\n", errno);
                    continue;
                }
                if (HTTPConn::m_conn_count >= MAX_FD) {
                    show_error(conn_fd, "Internal server busy");
                    continue;
                }

                conns[conn_fd].init(conn_fd, client_address);
            } else if (events[i].events & (EPOLLRDHUP | EPOLLHUP | EPOLLERR)) {
                conns[sock_fd].close_conn();
            } else if (events[i].events & EPOLLIN) {
                if (conns[sock_fd].read()) {
                    pool->append(conns + sock_fd);
                } else {
                    conns[sock_fd].close_conn();
                }
            } else if (events[i].events & EPOLLOUT) {
                if (!conns[sock_fd].write()) {
                    conns[sock_fd].close_conn();
                }
            } else {
            }
        }
    }

    delete pool;
    delete[] conns;

    return 0;
}

/*
    class HTTPConn
*/

// static variable init
int HTTPConn::m_conn_count = 0;
int HTTPConn::m_epoll_fd = -1;

void HTTPConn::close_conn(bool real_close) {
    if (real_close && (m_sock_fd != -1)) {
        //mod_fd( m_epoll_fd, m_sock_fd, EPOLLIN );
        remove_fd(m_epoll_fd, m_sock_fd);
        m_sock_fd = -1;
        --m_conn_count;
    }
}

void HTTPConn::init(int sock_fd, const sockaddr_in &addr) {
    m_sock_fd = sock_fd;
    m_address = addr;
    int error = 0;
    socklen_t len = sizeof(error);
    getsockopt(m_sock_fd, SOL_SOCKET, SO_ERROR, &error, &len);
    int reuse = 1;
    setsockopt(m_sock_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    add_fd(m_epoll_fd, sock_fd, true);
    m_conn_count++;

    init();
}

void HTTPConn::init() {
    m_check_state = CHECK_STATE_REQUESTLINE;
    m_linger = false;

    m_method = GET;
    m_url = nullptr;
    m_version = nullptr;
    m_content_length = 0;
    m_host = nullptr;
    m_start_line = 0;
    m_checked_idx = 0;
    m_read_idx = 0;
    m_write_idx = 0;
    memset(m_read_buf, 0, BUFFER_SIZE);
    memset(m_write_buf, 0, BUFFER_SIZE);
    memset(m_real_file, 0, FILENAME_LEN);
}

LINE_STATUS HTTPConn::parse_line() {
    char temp;
    for (; m_checked_idx < m_read_idx; ++m_checked_idx) {
        temp = m_read_buf[m_checked_idx];
        if (temp == '\r') {
            if ((m_checked_idx + 1) == m_read_idx) {
                return LINE_OPEN;
            } else if (m_read_buf[m_checked_idx + 1] == '\n') {
                m_read_buf[m_checked_idx++] = 0;
                m_read_buf[m_checked_idx++] = 0;
                return LINE_OK;
            }

            return LINE_BAD;
        } else if (temp == '\n') {
            if ((m_checked_idx > 1) && (m_read_buf[m_checked_idx - 1] == '\r')) {
                m_read_buf[m_checked_idx - 1] = 0;
                m_read_buf[m_checked_idx++] = 0;
                return LINE_OK;
            }
            return LINE_BAD;
        }
    }

    return LINE_OPEN;
}

bool HTTPConn::read() {
    if (m_read_idx >= BUFFER_SIZE) {
        return false;
    }

    int bytes_read = 0;
    while (true) {
        bytes_read = recv(m_sock_fd, m_read_buf + m_read_idx, BUFFER_SIZE - m_read_idx, 0);
        if (bytes_read == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break;
            }
            return false;
        } else if (bytes_read == 0) {
            return false;
        }

        m_read_idx += bytes_read;
    }
    return true;
}

HTTP_CODE HTTPConn::parse_request_line(char *text) {
    m_url = strpbrk(text, " \t");
    if (!m_url) {
        return BAD_REQUEST;
    }
    *m_url++ = 0;

    char *method = text;
    if (strcasecmp(method, "GET") == 0) {
        m_method = GET;
    } else {
        // GET only for now
        return BAD_REQUEST;
    }

    m_url += strspn(m_url, " \t");
    m_version = strpbrk(m_url, " \t");
    if (!m_version) {
        return BAD_REQUEST;
    }
    *m_version++ = 0;
    m_version += strspn(m_version, " \t");
    if (strcasecmp(m_version, "HTTP/1.1") != 0) {
        return BAD_REQUEST;
    }

    if (strncasecmp(m_url, "http://", 7) == 0) {
        m_url += 7;
        m_url = strchr(m_url, '/');
    }

    if (!m_url || m_url[0] != '/') {
        return BAD_REQUEST;
    }

    m_check_state = CHECK_STATE_HEADER;
    return NO_REQUEST;
}

HTTP_CODE HTTPConn::parse_headers(char *text) {
    if (text[0] == 0) {
        if (m_method == HEAD) {
            return GET_REQUEST;
        }

        if (m_content_length != 0) {
            m_check_state = CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }

        return GET_REQUEST;
    } else if (strncasecmp(text, "Connection:", 11) == 0) {
        text += 11;
        text += strspn(text, " \t");
        if (strcasecmp(text, "keep-alive") == 0) {
            m_linger = true;
        }
    } else if (strncasecmp(text, "Content-Length:", 15) == 0) {
        text += 15;
        text += strspn(text, " \t");
        m_content_length = atol(text);
    } else if (strncasecmp(text, "Host:", 5) == 0) {
        text += 5;
        text += strspn(text, " \t");
        m_host = text;
    } else {
        // ignore other headers
        // printf("oop! unknow header %s\n", text);
    }

    return NO_REQUEST;
}

HTTP_CODE HTTPConn::parse_content(char *text) {
    if (m_read_idx >= (m_content_length + m_checked_idx)) {
        text[m_content_length] = 0;
        return GET_REQUEST;
    }

    return NO_REQUEST;
}

void HTTPConn::show_request(const char *text) {
    time_t t;
    char ts[0x30];

    time(&t); // get current time
    strcpy(ts, ctime(&t));
    strtok(ts, "\n"); // remove newline

    printf("%s:%d - - [%s] \"%s\"\n", inet_ntoa(m_address.sin_addr), ntohs(m_address.sin_port), ts, text);
}

HTTP_CODE HTTPConn::process_read() {
    LINE_STATUS line_status = LINE_OK;
    HTTP_CODE ret = NO_REQUEST;
    char *text = NULL;

    while (((m_check_state == CHECK_STATE_CONTENT) && (line_status == LINE_OK)) || ((line_status = parse_line()) == LINE_OK)) {
        text = get_line();
        m_start_line = m_checked_idx;
        // printf("got 1 http line: %s\n", text);

        switch (m_check_state) {
        case CHECK_STATE_REQUESTLINE: {
            // show HTTP request info
            show_request(text);

            ret = parse_request_line(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            }
            break;
        }
        case CHECK_STATE_HEADER: {
            ret = parse_headers(text);
            if (ret == BAD_REQUEST) {
                return BAD_REQUEST;
            } else if (ret == GET_REQUEST) {
                return do_request();
            }
            break;
        }
        case CHECK_STATE_CONTENT: {
            ret = parse_content(text);
            if (ret == GET_REQUEST) {
                return do_request();
            }
            line_status = LINE_OPEN;
            break;
        }
        default:
            return INTERNAL_ERROR;
        }
    }

    return NO_REQUEST;
}

HTTP_CODE HTTPConn::do_request() {
    int len = strlen(doc_root);

    strcpy(m_real_file, doc_root);
    strncpy(m_real_file + len, m_url, FILENAME_LEN - len - 1);

    if (stat(m_real_file, &m_file_stat) < 0) {
        return NO_RESOURCE;
    }

    if (!(m_file_stat.st_mode & S_IROTH)) {
        return FORBIDDEN_REQUEST;
    }

    if (S_ISDIR(m_file_stat.st_mode)) {
        return BAD_REQUEST;
    }

    int fd = open(m_real_file, O_RDONLY);
    m_file_address = (char *)mmap(0, m_file_stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    return FILE_REQUEST;
}

void HTTPConn::unmap() {
    if (m_file_address) {
        munmap(m_file_address, m_file_stat.st_size);
        m_file_address = 0;
    }
}

bool HTTPConn::write() {
    int temp = 0;
    int bytes_have_send = 0;
    int bytes_to_send = m_write_idx;
    if (bytes_to_send == 0) {
        mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN);
        init();
        return true;
    }

    while (1) {
        temp = writev(m_sock_fd, m_iv, m_iv_count);
        if (temp <= -1) {
            if (errno == EAGAIN) {
                mod_fd(m_epoll_fd, m_sock_fd, EPOLLOUT);
                return true;
            }
            unmap();
            return false;
        }

        bytes_to_send -= temp;
        bytes_have_send += temp;
        if (bytes_to_send <= 0) {
            unmap();
            if (m_linger) {
                init();
                mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN);
                return true;
            } else {
                mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN);
                return false;
            }
        }
    }
}

bool HTTPConn::add_response(const char *format, ...) {
    if (m_write_idx >= BUFFER_SIZE) {
        return false;
    }
    va_list arg_list;
    va_start(arg_list, format);
    int len = vsnprintf(m_write_buf + m_write_idx, BUFFER_SIZE - 1 - m_write_idx, format, arg_list);
    if (len >= (BUFFER_SIZE - 1 - m_write_idx)) {
        return false;
    }
    m_write_idx += len;
    va_end(arg_list);
    return true;
}

bool HTTPConn::add_status_line(int status, const char *title) {
    return add_response("%s %d %s\r\n", "HTTP/1.1", status, title);
}

bool HTTPConn::add_headers(int content_len) {
    add_content_length(content_len);
    add_linger();
    add_blank_line();
    return true;
}

bool HTTPConn::add_content_length(int content_len) {
    return add_response("Content-Length: %d\r\n", content_len);
}

bool HTTPConn::add_linger() {
    return add_response("Connection: %s\r\n", (m_linger == true) ? "keep-alive" : "close");
}

bool HTTPConn::add_blank_line() {
    return add_response("%s", "\r\n");
}

bool HTTPConn::add_content(const char *content) {
    return add_response("%s", content);
}

bool HTTPConn::process_write(HTTP_CODE ret) {
    switch (ret) {
    case INTERNAL_ERROR: {
        add_status_line(500, ERROR_500_TITLE);
        add_headers(strlen(ERROR_500_form));
        if (!add_content(ERROR_500_form)) {
            return false;
        }
        break;
    }
    case BAD_REQUEST: {
        add_status_line(400, ERROR_400_TITLE);
        add_headers(strlen(ERROR_400_form));
        if (!add_content(ERROR_400_form)) {
            return false;
        }
        break;
    }
    case NO_RESOURCE: {
        add_status_line(404, ERROR_404_TITLE);
        add_headers(strlen(ERROR_404_form));
        if (!add_content(ERROR_404_form)) {
            return false;
        }
        break;
    }
    case FORBIDDEN_REQUEST: {
        add_status_line(403, ERROR_403_TITLE);
        add_headers(strlen(ERROR_403_form));
        if (!add_content(ERROR_403_form)) {
            return false;
        }
        break;
    }
    case FILE_REQUEST: {
        add_status_line(200, OK_200_TITLE);
        if (m_file_stat.st_size != 0) {
            add_headers(m_file_stat.st_size);
            m_iv[0].iov_base = m_write_buf;
            m_iv[0].iov_len = m_write_idx;
            m_iv[1].iov_base = m_file_address;
            m_iv[1].iov_len = m_file_stat.st_size;
            m_iv_count = 2;
            return true;
        }
        const char *OK_string = "it's Empty!";
        add_headers(strlen(OK_string));
        if (!add_content(OK_string))
            return false;
    }
    default:
        return false;
    }

    m_iv[0].iov_base = m_write_buf;
    m_iv[0].iov_len = m_write_idx;
    m_iv_count = 1;
    return true;
}

void HTTPConn::process() {
    HTTP_CODE read_ret = process_read();
    if (read_ret == NO_REQUEST) {
        mod_fd(m_epoll_fd, m_sock_fd, EPOLLIN);
        return;
    }

    bool write_ret = process_write(read_ret);
    if (!write_ret) {
        close_conn();
    }

    mod_fd(m_epoll_fd, m_sock_fd, EPOLLOUT);
}
