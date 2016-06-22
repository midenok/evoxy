#include "pool.h"
#include "connection.h"

INIT_POOL(ProxyFrontend);
INIT_POOL(BackendOnPool);

ProxyFrontend::ProxyFrontend(struct ev_loop* event_loop_, int conn_fd):
    OnEventLoop(event_loop_, conn_fd),
    received(input_buf, buffer::string::size_type(0)),
    backend(*this, event_loop_),
    parser(received, { backend.output_buf, backend.buf_size })
{
}

/*
1. load whole HTTP head into static buffer;
2. while loading parse it into entities (method, URI, headers);
3. when head parsing is finished (got CRLFCRLF sequence), check Host header

When data is transferred to another link (e.g. from Frontend to Backend) further processing is stopped until
positive response (see stop_events(), start_events()).

Copying is done only on headers (because they must be changed for proxied requests).
Data is transferred uncopied. Both buffers should be united for data copy.
*/
void ProxyFrontend::read_callback()
{
    size_t free_size = buf_size - received.size();
    if (free_size == 0) {
        error("Not enough buffer!");
        release();
        return;
    }
    size_t recv_size = recv(conn_watcher.fd, const_cast<char*>(received.end()), free_size, 0);
    if (recv_size == 0) {
        debug("peer shutdown");
        release();
        return;
    }
    if (recv_size == -1) {
        switch (errno) {
        case ENOTCONN:
            debug("peer reset");
            release();
            return;
        case EWOULDBLOCK:
            return;
        default:
            error("recv: ", strerror(errno));
            release();
            return;
        }
    }
    HTTPParser::Status s = parser({ received.end(), recv_size });

    received.grow(recv_size);
    assert(received.size() <= buf_size);

    switch (s) {
    case HTTPParser::PROCEED: // reached request end
        if (parser.host.empty()) {
            debug("No Host header in request!");
            release();
            return;
        }
        debug("Got request ", parser.request_uri);
        stop_events(EV_READ);

        output_data = backend.received;
        if (backend.connect(parser.output_end(), parser.host_cstr, parser.port)) {
            debug("Backend connection failed!");
            release();
            return;
        }

        return;
    case HTTPParser::TERMINATE:
        error("Parsing HTTP request failed!");
        release();
        return;
    default:
        break;
    }

    if (received.size() >= buf_size) {
        // TODO: mmap-based ring buffer (see https://github.com/willemt/cbuffer)
        error("Not enough buffer!");
        release();
        return;
    }
}

// TODO: code is mostly duplicated!
void ProxyFrontend::write_callback()
{
    // do we need getsockopt(conn_watcher.fd, SOL_SOCKET, SO_ERROR, ...) ?
#ifndef NDEBUG
    if (output_data.empty()) {
        debug("Warning: spurious write!");
        return;
    }
#endif
    ssize_t sent_size = send(conn_watcher.fd, output_data.data(), output_data.size(), MSG_NOSIGNAL);
    if (sent_size < 0) {
        switch (errno) {
        case ECONNRESET:
        case ENOTCONN:
            debug("peer reset");
            release();
            return;
        case EWOULDBLOCK:
            return;
        default:
            error("send: ", strerror(errno));
            release();
            return;
        }
    }

    if (sent_size == 0)
        return; // unexpected

    output_data.shrink_front(sent_size);
    if (output_data.empty()) {
        stop_events(EV_WRITE);
    }
}


ProxyBackend::ProxyBackend(ProxyFrontend& frontend_, struct ev_loop* event_loop_):
    OnEventLoop(event_loop_),
    received(input_buf, buffer::string::size_type(0)),
    frontend(frontend_)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd < 0) {
        throw Errno("socket");;
    }

    conn_watcher.fd = sock_fd;
}

bool ProxyBackend::connect(const char* output_end, const char* host, uint32_t port)
{
    struct sockaddr_in serv_addr;
    struct addrinfo hints, *res;

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    int err = getaddrinfo(host, NULL, &hints, &res);
    if (err != 0) {
        debug("getaddrinfo: ", gai_strerror(err));
        return true;
    }

    serv_addr.sin_addr = ((sockaddr_in *)(res->ai_addr))->sin_addr;
    freeaddrinfo(res);

    err = ::connect(conn_watcher.fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (err < 0 && errno != EINPROGRESS) {
        debug("connect: ", strerror(errno));
        return true;
    }

    output_data.assign(output_buf, output_end);
    start_conn_watcher(EV_WRITE);
    return false;
}

void ProxyBackend::write_callback()
{
    // do we need getsockopt(conn_watcher.fd, SOL_SOCKET, SO_ERROR, ...) ?
#ifndef NDEBUG
    if (output_data.empty()) {
        debug("Warning: spurious write!");
        return;
    }
#endif
    ssize_t sent_size = send(conn_watcher.fd, output_data.data(), output_data.size(), MSG_NOSIGNAL);
    if (sent_size < 0) {
        switch (errno) {
        case ECONNRESET:
        case ENOTCONN:
            debug("peer reset");
            frontend.release();
            return;
        case EWOULDBLOCK:
            return;
        default:
            error("send: ", strerror(errno));
            frontend.release();
            return;
        }
    }

    if (sent_size == 0)
        return; // unexpected

    output_data.shrink_front(sent_size);
    if (output_data.empty()) {
        stop_events(EV_WRITE);
        start_events(EV_READ);
    }
}

void ProxyBackend::read_callback()
{
    assert(frontend.output_data.begin() != nullptr);
    bool start_frontend_write = false;
    if (frontend.output_data.empty()) {
        received.assign(input_buf, buffer::string::size_type(0));
        start_frontend_write = true;
    }
    size_t free_size = buf_size - received.size();
    if (free_size == 0) {
        debug("Input buffer is full!");
        return;
    }
    size_t recv_size = recv(conn_watcher.fd, const_cast<char*>(received.end()), free_size, 0);
    if (recv_size == 0) {
        debug("peer shutdown");
        stop_events(EV_READ);
        // FIXME: indicate connection status
        return;
    }
    if (recv_size == -1) {
        switch (errno) {
        case ENOTCONN:
            debug("peer reset");
            stop_events(EV_READ);
            // FIXME: indicate connection status
            return;
        case EWOULDBLOCK:
            return;
        default:
            error("recv: ", strerror(errno));
            frontend.release();
            return;
        }
    }

    received.grow(recv_size);
    assert(received.size() <= buf_size);
    frontend.output_data.grow(recv_size);
    if (start_frontend_write) {
        frontend.start_events(EV_WRITE);
    }
}