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
    bool read_expected = true;
    ev_io conn_watcher;

private:
    struct ev_loop *event_loop;
    ev_async async_watcher;
    bool async_task = false;

    virtual void read_callback() = 0;
    virtual void write_callback() = 0;

    void terminate()
    {
        if (conn_watcher.fd) {
            debug("terminating connection");
            ev_io_stop(event_loop, &conn_watcher);
            close(conn_watcher.fd);
            conn_watcher.fd = 0;
        }
    }

    void read_unexpected()
    {
        char buf[1];
        size_t recv_size = recv(conn_watcher.fd, buf, 1, 0);
        if (recv_size == -1) {
            switch (errno) {
                case ENOTCONN:
                    debug ("peer reset");
                    if (async_task)
                        terminate();
                    else
                        delete this;
                    return;
                case EAGAIN:
                    return;
                default:
                    throw Errno("recv");
            }
        }
        if (recv_size == 0)
            debug("peer shutdown");
        else
            debug("unexpected read!");
        if (async_task)
            terminate();
        else
            delete this;
        return;
    }

    static void
    conn_callback (EV_P_ ev_io *w, int revents)
    {
        OnEventLoop *self = (OnEventLoop *)w->data;
        if (revents & EV_READ) {
            if (self->read_expected) {
                self->read_callback();
            } else {
                self->read_unexpected();
            }
        }
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

public:
    OnEventLoop(struct ev_loop *event_loop_) :
        event_loop { event_loop_ }
    {
        debug("OnEventLoop created");
        ev_async_init(&async_watcher, async_callback);
        async_watcher.data = this;
    }

    void start_conn_watcher(int events = EV_READ)
    {
        if (fcntl(conn_watcher.fd, F_SETFL, fcntl(conn_watcher.fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
            throw Errno("fcntl");
        }
        ev_io_init(&conn_watcher, conn_callback, conn_watcher.fd, events);
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
    char output_buf[buf_size];
    buffer::string received;
    buffer::string output_data;
    ProxyFrontend &frontend;

public:
    ProxyBackend(ProxyFrontend& frontend_, struct ev_loop* event_loop_);

    bool connect(const char* output_end, const char* host, uint32_t port);

    void read_callback() override;

    void write_callback() override;
};


class ProxyFrontend :
    public OnEventLoop,
    public OnPool<ProxyFrontend>,
    public virtual non_copyable // because of references in HTTPParser
{
    friend class ProxyBackend;
protected:
    static const size_t buf_size = 4096;
    char input_buf[buf_size];
    buffer::string received;
    buffer::string output_data;
    ProxyBackend backend;
    HTTPParser parser;
    ssize_t sent_size = 0;

public:
    ProxyFrontend(struct ev_loop* event_loop_, int conn_fd);

    void read_callback() override;
    void write_callback() override;
};


class BackendOnPool :
    public ProxyBackend,
    public OnPool<BackendOnPool>
{};


#endif // __udtproxy_connection_h
