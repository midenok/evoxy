#include <iostream>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <unistd.h>
#include "udtproxy.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <ev.h>

#include "threads.h"
#include "pool.h"
#include "http.h"
#include "util.h"

ThreadPool thread_pool;

class ConnectionCtx : public OnPool<ConnectionCtx>
{
    static const size_t buf_size = 4096;
    struct ev_loop *event_loop;
    ev_io conn_watcher;
    ev_async async_watcher;
    char full_buf[buf_size + 1];
    buffer::string received;
    HTTPParser parser;
    bool read_expected = true;
    ssize_t sent_size = 0;
    bool async_task = false;

    /*
        1. load whole HTTP head into static buffer;
        2. while loading parse it into entities (method, URI, headers);
        3. when head parsing is finished (got CRLFCRLF sequence), check Host header
    */

    void read_conn()
    {
        size_t free_size = buf_size - received.size();
        size_t recv_size = recv(conn_watcher.fd, const_cast<char*>(received.end()), buf_size - received.size(), 0);
        if (recv_size == 0) {
            debug ("peer shutdown");
            delete this;
            return;
        }
        if (recv_size == -1) {
            switch (errno) {
                case ENOTCONN:
                    debug ("peer reset");
                    delete this;
                    return;
                case EAGAIN:
                    return;
                default:
                    throw Errno("recv");
            }
        }
        HTTPParser::Status s = parser({ received.end(), recv_size });

        received.grow(recv_size);
        assert (received.size() <= buf_size);

        switch(s) {
            case HTTPParser::TERMINATE:
                delete this;
                return;
            case HTTPParser::PROCEED: // reached request end
                // debug("got request service ", parser.service);
                read_expected = false;

                /* For fast request we do processing inside accept thread.
                    In this example there is no processing at all, we just activate
                    response sending. */
                ev_io_stop(event_loop, &conn_watcher);
                conn_watcher.events = EV_READ | EV_WRITE;
                ev_io_start(event_loop, &conn_watcher);

                return;
            default:
                break;
        }

        if (received.size() >= buf_size) {
            // TODO: mmap-based ring buffer (see https://github.com/willemt/cbuffer)
            error("Not enough buffer!");
            delete this;
            return;
        }
    }

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

    void write_conn()
    {
#if 0
        ssize_t send_sz = send(conn_watcher.fd, RESPONSE.data() + sent_size, RESPONSE.size() - sent_size, 0);
        sent_size += send_sz;
        if (sent_size == RESPONSE.size()) {
            debug("sent reply");
            delete this;
            return;
        }
#endif
    }

    static void
    conn_callback (EV_P_ ev_io *w, int revents)
    {
        ConnectionCtx *self = (ConnectionCtx *)w->data;
        if (revents & EV_READ) {
            if (self->read_expected) {
                self->read_conn();
            } else {
                self->read_unexpected();
            }
        }
        if (revents & EV_WRITE)
            self->write_conn();
    }

    static void
    async_callback (EV_P_ ev_async *w, int revents)
    {
        ConnectionCtx *self = (ConnectionCtx *)w->data;
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

public:
    ConnectionCtx(struct ev_loop *event_loop_, int conn_fd) :
        received(full_buf, buffer::string::size_type(0)),
        event_loop{event_loop_},
        parser(received)
    {
        debug("ConnectionCtx created");
        if (fcntl(conn_fd, F_SETFL, fcntl(conn_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
            throw Errno("fcntl");
        }
        ev_io_init (&conn_watcher, conn_callback, conn_fd, EV_READ);
        ev_async_init (&async_watcher, async_callback);
        conn_watcher.data = this;
        async_watcher.data = this;
        ev_io_start(event_loop, &conn_watcher);
    }
    ConnectionCtx(const ConnectionCtx&) = delete;
    ~ConnectionCtx()
    {
        terminate();
        debug("ConnectionCtx destroying");
    }
};

INIT_POOL(ConnectionCtx);

using std::unique_ptr;

class AcceptTask : public Task
{
    /* Because AcceptTask is done inside event loop thread, processing must be fast enough
        to provide responsive frontend. This may be:
        1. read initial data;
        2. validate application protocol;
        3. fast answer and finish connection.
       Otherwise, if longer processing is required, additional task should created and routed to worker thread. */

    static const int MAX_LISTEN_QUEUE = SOMAXCONN;
    int listen_fd;
    // OPTIMIZE: addr can be shared
    struct sockaddr_in addr;

    // libev entities
    struct ev_loop *event_loop;
    ev_io accept_watcher;
    unique_ptr<Pool<ConnectionCtx> > pool;

    void
    accept_conn()
    {
        debug("AcceptTask incoming connection!");
        struct sockaddr_in peer_addr;
        socklen_t addr_len = sizeof (peer_addr);
        int conn_fd = accept(listen_fd, (sockaddr *)&peer_addr, &addr_len);
        if (conn_fd == -1) {
            if (errno != EAGAIN) {
                throw Errno("accept");
            }
            // something ugly happened: we should get valid conn_fd here (because of read event)
            error("Warning: unexpected EAGAIN!");
            return;
        }
        debug("got connection!");
        new (*pool) ConnectionCtx(event_loop, conn_fd);
    }

    static void
    accept_callback (EV_P_ ev_io *w, int revents)
    {
        ((AcceptTask *)w->data)->accept_conn();
    }

public:
    static size_t
    pool_size(size_t capacity)
    {
        decltype(pool)::element_type::memsize(capacity);
    }

    AcceptTask(size_t conn_capacity) :
        pool(new Pool<ConnectionCtx>(conn_capacity))
    {
        debug("AcceptTask created");
        // listen socket setup
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == -1) {
            throw Errno("socket");;
        }
        int sock_opt = 1;
        // SO_REUSEPORT allows multiple sockets with same ADDRESS:PORT. Linux does load-balancing of incoming connections.
        // See long explanation in SO-14388706.
        #ifdef SO_REUSEPORT
        if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEPORT, (char *) &sock_opt, sizeof(sock_opt)) == -1) {
            throw Errno("setsockopt");
        }
        #endif
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(OPT_VALUE_PORT);
        debug("Listening on ", inet_ntoa(addr.sin_addr), ":", OPT_VALUE_PORT);
        if (bind(listen_fd, (struct sockaddr *) &addr, sizeof(addr)) != 0) {
            throw Errno("bind");;
        }
        if (fcntl(listen_fd, F_SETFL, fcntl(listen_fd, F_GETFL, 0) | O_NONBLOCK) == -1) {
            throw Errno("fcntl");
        }
        if (listen(listen_fd, MAX_LISTEN_QUEUE) != 0) {
            throw Errno("listen");
        }
        // libev setup
        event_loop = ev_loop_new(EVBACKEND_EPOLL);
        if (!event_loop) {
            event_loop = ev_loop_new(EVBACKEND_POLL);
            if (!event_loop) {
                event_loop = ev_loop_new(EVBACKEND_SELECT);
                if (!event_loop) {
                    throw std::runtime_error("libev: failed to start event loop!");
                }
                debug("libev: selected backend SELECT");
            } else {
                debug("libev: selected backend POLL");
            }
        } else {
            debug("libev: selected backend EPOLL");
        }
        ev_io_init (&accept_watcher, accept_callback, listen_fd, EV_READ);
        accept_watcher.data = this;
    }
    virtual ~AcceptTask()
    {
        if (event_loop)
            ev_loop_destroy(event_loop);
    }
    AcceptTask(AcceptTask &&src) :
        listen_fd{src.listen_fd},
        addr{src.addr},
        event_loop{src.event_loop},
        accept_watcher{src.accept_watcher},
        pool(std::move(src.pool))
    {
        debug("AcceptTask moved from ", &src);
        accept_watcher.data = this;
        src.listen_fd = 0;
        src.event_loop = nullptr;
    }
    virtual void execute()
    {
        ev_io_start(event_loop, &accept_watcher);
        socklen_t addr_len = sizeof(addr);
        int conn_fd = accept(listen_fd, (struct sockaddr *) &addr, &addr_len);
        if (conn_fd == -1) {
            if (errno != EAGAIN) {
                throw Errno("accept");
            }
        } else {
            ev_invoke(event_loop, &accept_watcher, EV_READ);
        }
        debug("running event loop...");
        ev_run(event_loop, 0);
    }
};

void
daemonize()
{
    static const int nochdir = 1;
    static const int noclose = ENABLED_OPT(VERBOSE);
    static const char *dir = "/var/tmp";

    if (chdir(dir))
        throw Errno("chdir ", dir);

    if (daemon(nochdir, noclose))
        throw Errno("daemon");
}

int
main(int argc, char ** argv)
{
    int res = optionProcess(&udtproxyOptions, argc, argv);
    res = ferror(stdout);
    if (res != 0) {
        cerror("optionProcess", "output error writing to stdout!");
        return res;
    }

    #ifdef SO_REUSEPORT
    if (!HAVE_OPT(ACCEPT_THREADS))
        OPT_VALUE_ACCEPT_THREADS = std::thread::hardware_concurrency();
    #else
    if (HAVE_OPT(ACCEPT_THREADS))
        cerror("SO_REUSEPORT is unsupported! --accept-threads was set to 1");
    OPT_VALUE_ACCEPT_THREADS = 1;
    #endif

    if (!HAVE_OPT(WORKER_THREADS))
        OPT_VALUE_WORKER_THREADS = OPT_VALUE_ACCEPT_THREADS;

    // main thread is also accept thread, thus decreasing spawning
    int accept_pool_sz = OPT_VALUE_ACCEPT_THREADS - 1;

    if (ENABLED_OPT(DAEMONIZE))
        daemonize();

    thread_pool.spawn_threads(accept_pool_sz + OPT_VALUE_WORKER_THREADS);

    cdebug("main", "Running ", OPT_VALUE_ACCEPT_THREADS, " "
           "accept threads; pool size: ", AcceptTask::pool_size(OPT_VALUE_ACCEPT_CAPACITY) / 1024, " kb; "
           "total pool size: ", AcceptTask::pool_size(OPT_VALUE_ACCEPT_CAPACITY) * OPT_VALUE_ACCEPT_THREADS / 1024, " kb.");

    try
    {
        for (int i = 0; i < accept_pool_sz; ++i) {
            AcceptTask accept_task(OPT_VALUE_ACCEPT_CAPACITY);
            thread_pool.add_task(accept_task);
        }

        AcceptTask accept_task(OPT_VALUE_ACCEPT_CAPACITY);
        accept_task.execute();
    } catch(std::bad_alloc &) {
        std::cerr << "Not enough memory!\n";
        return 10;
    } catch(std::exception &ex) {
        std::cerr << ex.what() << "\n";
        return 100;
    }

    return res;
}
