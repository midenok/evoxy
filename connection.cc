#include "pool.h"
#include "connection.h"

INIT_POOL(ProxyFrontend);
INIT_POOL(BackendOnPool);

ProxyFrontend::ProxyFrontend(struct ev_loop* event_loop_, int conn_fd):
    OnEventLoop(event_loop_, conn_fd),
    buffer({input_buf, buf_size}),
    backend(*this, event_loop_),
    parser(buffer, backend.buffer)
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

F::R: Client request head is received into ProxyFrontend buffer and simultaneously written to
      ProxyBackend buffer (with some headers modification).
F::R: After ProxyFrontend finishes receiving request head, it starts ProxyBackend EV_WRITE.
F::R: ProxyFrontend keeps receiving request body into its buffer, until buffer is full.
B::W: ProxyBackend keeps sending its buffer. When its buffer is empty, it swaps buffers with ProxyFrontend.
F::R: When ProxyFrontend finishes receiving request body, it stops its EV_READ and sets REQUEST_FINISHED status.
B::W: When both buffers are empty and status is REQUEST_FINISHED, ProxyBackend starts EV_READ.
B::R: ProxyBackend keeps receiving Server response into its buffer. After first data received
      it starts ProxyFrontend EV_WRITE.
F::W: ProxyFrontend keeps sending its buffer. When its buffer is empty, it swaps buffers with ProxyBackend.
B::R: When ProxyBackend finishes receiving response, it stops its EV_READ and sets RESPONSE_FINISHED status.
F::W: When both buffers are empty and status is RESPONSE_FINISHED, ProxyFrontend either:
    a) drops status, stops EV_WRITE and starts EV_READ in case of keepalive connection;
    b) terminates.

output buffer have: send_begin, send_size, write_begin, write_max
input buffer have: recv_begin, recv_max, read_begin, read_
*/

void ProxyFrontend::read_callback()
{
    buffer::string recv_chunk;
    IOBuffer::Status err = buffer.recv(conn_watcher.fd, recv_chunk);

    switch (err) {
    case IOBuffer::BUFFER_FULL:
        if (progress < REQUEST_HEAD_FINISHED) {
            error("Not enough buffer to read request head!");
            release();
        }
        return;
    case IOBuffer::SHUTDOWN:
    case IOBuffer::OTHER_ERROR:
        release();
    case IOBuffer::WOULDBLOCK:
        return;
    default:
        break;
    }

    HTTPParser::Status s;
    switch (progress) {
    case REQUEST_STARTED:
        s = parser.parse_head(recv_chunk);

        switch (s) {
        case HTTPParser::HEAD_FINISHED: // reached request end
            if (parser.host.empty()) {
                debug("No Host header in request!");
                release();
                return;
            }
            debug("Got request ", parser.request_uri);
            progress = ((parser.clength == 0 && !parser.chunked) ?
                REQUEST_FINISHED :
                REQUEST_HEAD_FINISHED);

            if (backend.connect(parser.host_cstr, parser.port)) {
                debug("Backend connection failed!");
                release();
                return;
            }

            if (!buffer.empty())
                recv_chunk = buffer;

            break;
        case HTTPParser::TERMINATE:
            error("Parsing HTTP request failed!");
            release();
            return;
        default:
            return;
        } // switch (HTTPParser::Status)

        if (progress == REQUEST_FINISHED)
            goto REQUEST_FINISHED;

    case REQUEST_HEAD_FINISHED:
        if (parser.chunked)
            s = parser.parse_body(recv_chunk);

    case REQUEST_FINISHED:
    REQUEST_FINISHED:
        // We can't disable READ, because any time client can tear connection.
        // In this case we need to tear backend ASAP!
        // stop_events(EV_READ);
        stop_all_events(); // FIXME: wrong!
    } // switch (progress)
}

void ProxyFrontend::write_callback()
{
    if (buffer.empty()) {
        if (backend.buffer.empty()) {
            if (progress == ProxyFrontend::RESPONSE_FINISHED) {
                debug("Response finished!");
                release();
                return;
                // TODO: restart in case of Keep-Alive
                // start_only_events(EV_READ);
            } else {
                debug("Spurious write!");
            }
            return;
        }
        buffer.reset();
        std::swap(buffer, backend.buffer);
    }
    IOBuffer::Status err = buffer.send(conn_watcher.fd);

    switch (err) {
    case IOBuffer::SHUTDOWN:
    case IOBuffer::OTHER_ERROR:
        release();
    case IOBuffer::WOULDBLOCK:
        return;
    default:
        break;
    }
}


ProxyBackend::ProxyBackend(ProxyFrontend& frontend_, struct ev_loop* event_loop_):
    OnEventLoop(event_loop_),
    buffer({ input_buf, buf_size }),
    frontend(frontend_)
{
    int sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (sock_fd < 0) {
        throw Errno("socket");;
    }

    conn_watcher.fd = sock_fd;
}

bool ProxyBackend::connect(const char* host, uint32_t port)
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

    start_conn_watcher(EV_WRITE);
    return false;
}

void ProxyBackend::write_callback()
{
    if (buffer.empty()) {
        if (frontend.buffer.empty()) {
            if (frontend.progress == ProxyFrontend::REQUEST_FINISHED) {
                start_only_events(EV_READ);
            } else {
                debug("Spurious write!");
            }
            return;
        }
        buffer.reset();
        std::swap(buffer, frontend.buffer);
    }
    IOBuffer::Status err = buffer.send(conn_watcher.fd);

    switch (err) {
    case IOBuffer::SHUTDOWN:
    case IOBuffer::OTHER_ERROR:
        frontend.release();
    case IOBuffer::WOULDBLOCK:
        return;
    default:
        break;
    }
}


void ProxyBackend::read_callback()
{
    buffer::string recv_chunk;
    IOBuffer::Status err = buffer.recv(conn_watcher.fd, recv_chunk);

    switch (err) {
    case IOBuffer::BUFFER_FULL:
        return;
    case IOBuffer::SHUTDOWN:
        // TODO: proper end of response detection
        stop_all_events();
        frontend.progress = ProxyFrontend::RESPONSE_FINISHED;
        return;
    case IOBuffer::OTHER_ERROR:
        frontend.release();
    case IOBuffer::WOULDBLOCK:
        return;
    default:
        break;
    }

    assert(frontend.progress >= ProxyFrontend::REQUEST_FINISHED);
    if (frontend.progress == ProxyFrontend::REQUEST_FINISHED) {
        frontend.progress = ProxyFrontend::RESPONSE_STARTED;
        frontend.start_only_events(EV_WRITE);
    }
}
