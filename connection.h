#pragma once
#ifndef __udtproxy_connection_h
#define __udtproxy_connection_h

#include <ev.h>
#include <sys/socket.h>
#include <algorithm>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>

#include <netdb.h>
#include <sys/unistd.h>

#include "pool.h"
#include "buffer_string.h"
#include "http.h"
#include "util.h"

class OnEventLoop :
    public virtual non_copyable
{
protected:
    ev_io conn_watcher;

private:
    struct ev_loop *event_loop;
    ev_async async_watcher;
    bool async_task = false;

    virtual void read_callback() = 0;
    virtual void write_callback() = 0;
    virtual void error_callback(int err) = 0;

    void terminate()
    {
        if (conn_watcher.fd) {
            debug("terminating connection");
            ev_io_stop(event_loop, &conn_watcher);
            close(conn_watcher.fd);
            conn_watcher.fd = 0;
        }
    }

    typedef void(*callback_f)(EV_P_ ev_io *w, int revents);

    static void
    conn_callback (EV_P_ ev_io *w, int revents)
    {
        OnEventLoop *self = (OnEventLoop *)w->data;
        if (revents & EV_READ)
            self->read_callback();
        if (revents & EV_WRITE)
            self->write_callback();
    }

    static void
    async_callback (EV_P_ ev_async *w, int revents)
    {
        OnEventLoop *self = (OnEventLoop *)w->data;
        self->async_task = false;
        ev_async_stop(self->event_loop, &self->async_watcher);
        if (self->conn_watcher.fd == 0) {
            delete self;
            return;
        }
        ev_io_stop(self->event_loop, &self->conn_watcher);
        self->conn_watcher.events = EV_READ | EV_WRITE;
        ev_io_start(self->event_loop, &self->conn_watcher);
    }

protected:
    void start_events(int events = 0)
    {
        if (events) {
            if (conn_watcher.events)
                ev_io_stop(event_loop, &conn_watcher);
            conn_watcher.events |= events;
        }
        ev_io_start(event_loop, &conn_watcher);
    }

    void stop_events(int events = 0)
    {
        ev_io_stop(event_loop, &conn_watcher);
        if (events) {
            conn_watcher.events &= ~events;
            if (conn_watcher.events)
                ev_io_start(event_loop, &conn_watcher);
        }
    }

    void start_only_events(int events)
    {
        if (conn_watcher.events)
            ev_io_stop(event_loop, &conn_watcher);
        conn_watcher.events = events;
        ev_io_start(event_loop, &conn_watcher);
    }

    void stop_all_events()
    {
        ev_io_stop(event_loop, &conn_watcher);
        conn_watcher.events = 0;
    }

    void check_socket()
    {
        socklen_t optlen = sizeof(int);
        int sockerr;
        int err = getsockopt(conn_watcher.fd, SOL_SOCKET, SO_ERROR, &sockerr, &optlen);
        if (err < 0)
            throw Errno("getsockopt");

        if (sockerr) {
            error_callback(sockerr);
        } else {
            ev_io_stop(event_loop, &conn_watcher);
            ev_io_init(&conn_watcher, conn_callback, conn_watcher.fd, EV_WRITE);
            ev_io_start(event_loop, &conn_watcher);
        }
    }

    static void
        connect_callback(EV_P_ ev_io *w, int revents)
    {
         OnEventLoop *self = (OnEventLoop *)w->data;
         self->check_socket();
    }

public:
    OnEventLoop(struct ev_loop *event_loop_) :
        event_loop { event_loop_ }
    {
        debug("OnEventLoop created");
        ev_async_init(&async_watcher, async_callback);
        async_watcher.data = this;
    }

    template <callback_f CALLBACK = conn_callback>
    void start_conn_watcher(int events = EV_READ)
    {
        if (fcntl(conn_watcher.fd, F_SETFL, fcntl(conn_watcher.fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
            throw Errno("fcntl");
        }
        ev_io_init(&conn_watcher, CALLBACK, conn_watcher.fd, events);
        conn_watcher.data = this;
        ev_io_start(event_loop, &conn_watcher);
    }

    OnEventLoop(struct ev_loop *event_loop_, int conn_fd, int events = EV_READ) :
        OnEventLoop(event_loop_)
    {
        conn_watcher.fd = conn_fd;
        start_conn_watcher(events);
    }

    virtual ~OnEventLoop()
    {
        terminate();
        debug("OnEventLoop destroying");
    }
};


class IOBuffer : public buffer::string
{
    buffer::string buffer;

public:
    enum Status
    {
        OK,
        BUFFER_FULL,
        SHUTDOWN,
        WOULDBLOCK,
        OTHER_ERROR
    };

    IOBuffer(buffer::string buffer_) :
        buffer::string(buffer_.begin(), size_type(0)),
        buffer { buffer_ }
    {}

    void reset()
    {
        assign(buffer.begin(), size_type(0));
    }

    void clear()
    {
        reset();
    }

    size_type free_size()
    {
        return buffer.end() - end();
    }

    buffer::string::pointer buffer_begin()
    {
        return buffer.begin();
    }

    IOBuffer& append(buffer::string &add)
    {
        size_t count = std::min(add.size(), free_size());
        add.copy(const_cast<char *>(end()), count);
        grow(count);
        return *this;
    }

    IOBuffer& append(const char * add)
    {
        buffer::string str(add);
        return append(str);
    }

    IOBuffer& append(int num)
    {
        char buf[sizeof(int) * 3 + 1];
        size_t n = snprintf(buf, sizeof(int) * 3, "%d", num);
        buffer::string str(buf, n);
        return append(str);
    }

    template<typename ... Any>
    IOBuffer& appendm(Any ... args)
    {
        int dummy[sizeof...(Any)] = { (append(args), 0)... };
        return *this;
    }

    Status recv(int fd, buffer::string &recv_chunk)
    {
        size_type free_size = IOBuffer::free_size();
        if (free_size == 0) {
            debug("buffer full");
            return BUFFER_FULL;
        }
        ssize_t recv_size = ::recv(fd, const_cast<char*>(end()), free_size, 0);
        if (recv_size == 0) {
            debug("peer shutdown");
            return SHUTDOWN;
        }
        if (recv_size < 0) {
            switch (errno) {
            case EWOULDBLOCK:
                return WOULDBLOCK;
            case ECONNRESET:
            case ENOTCONN:
                debug("peer reset");
                return OTHER_ERROR;
            default:
                error("recv: ", strerror(errno));
                return OTHER_ERROR;
            }
        }
        recv_chunk.assign(end(), recv_size);
        grow(recv_size);
        assert(end() <= buffer.end());
        return OK;
    }

    Status send(int fd)
    {
        ssize_t sent_size = ::send(fd, data(), size(), MSG_NOSIGNAL);
        if (sent_size < 0) {
            switch (errno) {
            case EWOULDBLOCK:
                return WOULDBLOCK;
            case ECONNRESET:
            case ENOTCONN:
                debug("peer reset");
                return OTHER_ERROR;
            default:
                error("send: ", strerror(errno));
                return OTHER_ERROR;
            }
        }

        if (sent_size == 0)
            return WOULDBLOCK; // unexpected

        shrink_front(sent_size);
        return OK;
    }
};


class ProxyFrontend;

class ProxyBackend :
    public OnEventLoop,
    public virtual non_copyable // because of references
{
    friend class ProxyFrontend;
protected:
    // TODO: test with buf_size = 1, 2, 3, etc.
    static const size_t buf_size = 4096;
    char input_buf[buf_size];
    IOBuffer buffer;
    ProxyFrontend &frontend;

public:
    ProxyBackend(ProxyFrontend& frontend_, struct ev_loop* event_loop_);

    bool connect(const char* host, uint32_t port);

    void read_callback() override;
    void write_callback() override;
    void error_callback(int err) override;
};


class ProxyFrontend :
    public OnEventLoop,
    public OnPool<ProxyFrontend>,
    public virtual non_copyable // because of references in HTTPParser
{
    friend class ProxyBackend;

    enum Progress
    {
        REQUEST_STARTED = 0,
        REQUEST_HEAD_FINISHED,
        REQUEST_FINISHED,
        RESPONSE_STARTED,
        RESPONSE_HEAD_FINISHED,
        RESPONSE_FINISHED
    };

protected:
    static const size_t buf_size = 4096;
    char input_buf[buf_size];
    IOBuffer buffer;
    ProxyBackend backend;
    HTTPParser parser;
    ssize_t sent_size = 0;
    Progress progress = REQUEST_STARTED;

public:
    ProxyFrontend(struct ev_loop* event_loop_, int conn_fd);

    void read_callback() override;
    void write_callback() override;
    void error_callback(int) override
    {}

    void set_error(buffer::string &err, int err_no);
};


class BackendOnPool :
    public ProxyBackend,
    public OnPool<BackendOnPool>
{};


#endif // __udtproxy_connection_h
