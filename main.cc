#include <iostream>
#include <cstdio>
#include "evoxy.h"

#include <sys/socket.h>
#include <arpa/inet.h>
#include <ev.h>

#include "threads.h"
#include "pool.h"
#include "util.h"
#include "connection.h"
#include "cache.h"

ThreadPool thread_pool;


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
    typedef Pool<Proxy> ConnectionPool;
    unique_ptr<ConnectionPool> pool;
    unique_ptr<NameCacheOnPool> name_cache;

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
        debug("Got connection from ", inet_ntoa(peer_addr.sin_addr));
        try {
            new (*pool) Proxy(event_loop, conn_fd, name_cache.get());
        } catch (std::bad_alloc) {
            error("Memory pool is empty! Discarding connection from ", inet_ntoa(peer_addr.sin_addr));
            shutdown(conn_fd, SHUT_RDWR);
            close(conn_fd);
        }
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
        pool(new ConnectionPool(conn_capacity))
    {
        debug("AcceptTask created");

        if (OPT_VALUE_NAME_CACHE) {
            name_cache.reset(new NameCacheOnPool(OPT_VALUE_NAME_CACHE, OPT_VALUE_CACHE_LIFETIME));
        }

        // listen socket setup
        listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) {
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
        pool(std::move(src.pool)),
        name_cache(std::move(src.name_cache))
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
    int res = optionProcess(&evoxyOptions, argc, argv);
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

    cdebug("Running ", OPT_VALUE_ACCEPT_THREADS, " "
           "accept threads; pool size: ", AcceptTask::pool_size(OPT_VALUE_ACCEPT_CAPACITY) / 1024, " kb; "
           "total pool size: ", AcceptTask::pool_size(OPT_VALUE_ACCEPT_CAPACITY) * OPT_VALUE_ACCEPT_THREADS / 1024, " kb.");

    if (OPT_VALUE_NAME_CACHE)
        cdebug("Using name cache of ", OPT_VALUE_NAME_CACHE, " capacity, lifetime ", OPT_VALUE_CACHE_LIFETIME, " secs");

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
