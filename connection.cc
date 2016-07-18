#include "pool.h"
#include "connection.h"

INIT_POOL(Proxy);

Proxy::Proxy(struct ev_loop* event_loop_, int conn_fd) :
    frontend_buffer({ buffer_holder[0], buf_size }),
    backend_buffer({ buffer_holder[1], buf_size }),
    parser(frontend_buffer, backend_buffer, conn_fd),
    frontend(event_loop_, conn_fd, *this),
    backend(event_loop_, *this)
{
#ifndef NDEBUG
    frontend_buffer.debug_prefix("F: ");
    backend_buffer.debug_prefix("B: ");
#endif
}

Proxy::Frontend::Frontend(
        struct ev_loop* event_loop_,
        int conn_fd,
        Proxy &proxy_) :
    OnEventLoop(event_loop_, conn_fd),
    proxy {proxy_},
    progress {proxy.progress},
    parser {proxy.parser},
    buffer {proxy.frontend_buffer},
    backend {proxy.backend}
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

F::R: Client request head is received into Proxy::Frontend buffer and simultaneously written to
      Backend buffer (with some headers modification).
F::R: After Proxy::Frontend finishes receiving request head, it starts Backend EV_WRITE.
F::R: Proxy::Frontend keeps receiving request body into its buffer, until buffer is full.
B::W: Backend keeps sending its buffer. When its buffer is empty, it swaps buffers with Proxy::Frontend.
F::R: When Proxy::Frontend finishes receiving request body, it stops its EV_READ and sets REQUEST_FINISHED status.
B::W: When both buffers are empty and status is REQUEST_FINISHED, Backend starts EV_READ.
B::R: Backend keeps receiving Server response into its buffer. After first data received
      it starts Proxy::Frontend EV_WRITE.
F::W: Proxy::Frontend keeps sending its buffer. When its buffer is empty, it swaps buffers with Backend.
B::R: When Backend finishes receiving response, it stops its EV_READ and sets RESPONSE_FINISHED status.
F::W: When both buffers are empty and status is RESPONSE_FINISHED, Proxy::Frontend either:
    a) drops status, stops EV_WRITE and starts EV_READ in case of keepalive connection;
    b) terminates.

output buffer have: send_begin, send_size, write_begin, write_max
input buffer have: recv_begin, recv_max, read_begin, read_
*/

bool
Proxy::Frontend::read_callback()
{
    buffer::string recv_chunk;
    // 'buffer' semantics is across multiple calls, recv_chunk points to last portion received
    IOBuffer::Status err = buffer.recv(conn_watcher.fd, recv_chunk);

    switch (err) {
    case IOBuffer::BUFFER_FULL:
        spurious_reads++;
        if (progress < REQUEST_HEAD_FINISHED) {
            error("F: not enough buffer to read request head!");
            proxy.release();
            return true;
        }
        stop_events(EV_READ);
        return false;
    case IOBuffer::SHUTDOWN:
    case IOBuffer::OTHER_ERROR:
        proxy.release();
        return true;
    case IOBuffer::WOULDBLOCK:
        return false;
    default:
        break;
    }

    HTTPParser::Status s;
    switch (progress) {
    case REQUEST_STARTED:
        s = parser.parse_head(recv_chunk);

        switch (s) {
        case HTTPParser::PROCEED: // reached head end
            if (parser.host.empty()) {
                debug("F: no Host header in request!");
                proxy.release();
                return true;
            }
        #ifndef NDEBUG
            {
                char cl[20] = "unset";
                if (parser.content_length != HTTPParser::cl_unset)
                    snprintf(cl, sizeof(cl), "%lu", parser.content_length);

                debug("F: got request to ", parser.host, ", URI: ", parser.request_uri,
                    " (cl: ", cl,
                    ", chunked: ", parser.chunked,
                    ", force_close: ", parser.force_close, ")");
            }
        #endif

            progress =
                parser.content_length == 0 ||
                parser.content_length == HTTPParser::cl_unset && !parser.chunked ?
                    REQUEST_FINISHED :
                    REQUEST_HEAD_FINISHED;

            debug("F: changed progress: ", progress);
            if (parser.keep_alive) { // this flag is set only on response
                in_addr new_ip = host_ip;
                if (parser.host != host) {
                    if (set_host(parser.host) || resolve_host(new_ip)) {
                        debug("F: host resolution failed!");
                        proxy.release();
                        return true;
                    }
                }
                if (parser.port != port || new_ip.s_addr != host_ip.s_addr) {
                    backend.shutdown();
                    host_ip = new_ip;
                    port = parser.port;
                    if (backend.connect(host_ip, port)) {
                        debug("F: backend connection failed!");
                        proxy.release();
                        return true;
                    }
                    debug("F: connected to ", host, ":", port);
                } else {
                    backend.start_only_events(EV_WRITE);
                }
            } else {
                port = parser.port;
                if (set_host(parser.host) ||
                    resolve_host(host_ip) ||
                    backend.connect(host_ip, port))
                {
                    debug("F: backend connection (or host resolution) failed!");
                    proxy.release();
                    return true;
                }
                debug("F: connected to ", host, ":", parser.port);
            }

            if (progress == REQUEST_FINISHED)
                goto REQUEST_FINISHED;

            if (recv_chunk.empty())
                return false;

            break;
        case HTTPParser::TERMINATE:
            error("F: parsing HTTP request failed!");
            proxy.release();
            return true;
        case HTTPParser::CONTINUE:
        default:
            return false;
        } // switch (HTTPParser::Status)

    case REQUEST_HEAD_FINISHED:
        s = parser.parse_body(recv_chunk);
        switch (s) {
        case HTTPParser::PROCEED: // reached body end
            progress = REQUEST_FINISHED;
            debug("F: changed progress: ", progress);
            backend.start_events(EV_WRITE);
            goto REQUEST_FINISHED;
        case HTTPParser::TERMINATE:
            error("F: parsing HTTP request body failed!");
            proxy.release();
            return true;
        case HTTPParser::CONTINUE:
        default:
            backend.start_events(EV_WRITE);
            return false;
        } // switch (HTTPParser::Status)

    case REQUEST_FINISHED:
    default:
        error("F: unexpected data on finished request!");
    REQUEST_FINISHED:
        // We can't disable READ, because any time client may tear connection.
        ;
    } // switch (progress)
    return false;
}

bool
Proxy::Frontend::write_callback()
{
    if (buffer.empty()) {
        if (backend.buffer.empty()) {
            if (progress == RESPONSE_FINISHED) {
                debug("F: Response finished!");
                if (parser.keep_alive) {
                    parser.restart_request(buffer);
                    buffer.reset();
                    backend.buffer.reset();
                    progress = REQUEST_STARTED;
                    debug("F: changed progress: ", progress);
                    start_only_events(EV_READ);
                    return false;
                }
                proxy.release();
                return true;
            }
            spurious_writes++;
            stop_events(EV_WRITE);
            return false;
        }
        buffer.reset();
        std::swap(buffer, backend.buffer);
        backend.start_events(EV_READ);
    }

    IOBuffer::Status err = buffer.send(conn_watcher.fd);

    switch (err) {
    case IOBuffer::SHUTDOWN:
    case IOBuffer::OTHER_ERROR:
        proxy.release();
        return true;
    case IOBuffer::WOULDBLOCK:
        return false;
    default:
        break;
    }
    return false;
}


void
Proxy::Frontend::set_error(const buffer::string &err, int err_no)
{
    buffer.reset();
    buffer.appendm(err, strerror(err_no), " (", err_no, ")");
    start_only_events(EV_WRITE);
}

bool Proxy::Frontend::resolve_host(in_addr &host_ip)
{
    struct addrinfo hints, *res;

    memset(&hints, 0, sizeof(hints));
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_family = AF_INET;

    int err = getaddrinfo(host_cstr, NULL, &hints, &res);
    if (err != 0) {
        error("getaddrinfo: ", gai_strerror(err));
        return true;
    }

    host_ip = ((sockaddr_in *) (res->ai_addr))->sin_addr;
    freeaddrinfo(res);
    return false;
}


Proxy::Backend::Backend(
        struct ev_loop* event_loop_,
        Proxy &proxy_) :
    OnEventLoop(event_loop_),
    proxy { proxy_ },
    progress { proxy.progress },
    parser { proxy.parser },
    buffer { proxy.backend_buffer },
    frontend { proxy.frontend }
{
    conn_watcher.fd = 0;
}

bool
Proxy::Backend::connect(in_addr ip, uint32_t port)
{
    conn_watcher.fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (conn_watcher.fd < 0) {
        throw Errno("socket");;
    }

    struct sockaddr_in serv_addr;
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    serv_addr.sin_addr = ip;

    int err = ::connect(conn_watcher.fd, (struct sockaddr *) &serv_addr, sizeof(serv_addr));
    if (err < 0 && errno != EINPROGRESS) {
        debug("connect: ", strerror(errno));
        return true;
    }

    // On connection error EV_READ is activated faster when you trying to write
    start_conn_watcher<connect_callback>(EV_READ|EV_WRITE);
    return false; // true means error
}

const buffer::string BAD_GATEWAY(
    "HTTP/1.1 502 Bad Gateway\r\n"
    "Connection: close\r\n"
    "Content-Type: text/plain\r\n"
    "\r\n"
);

bool
Proxy::Backend::error_callback(int err)
{
    debug("connect: ", strerror(err));
    if (progress != REQUEST_FINISHED) {
        proxy.release();
        return true;
    }
    progress = RESPONSE_FINISHED;
    debug("B: changed progress: ", progress);
    buffer.reset();
    frontend.set_error(BAD_GATEWAY, err);
    stop_all_events();
    return false;
}

bool
Proxy::Backend::write_callback()
{
    if (buffer.empty()) {
        if (frontend.buffer.empty()) {
            if (progress == REQUEST_FINISHED) {
                // maybe do this in read_callback() ?
                buffer.reset();
                progress = RESPONSE_STARTED;
                debug("B: changed progress: ", progress);
                start_only_events(EV_READ);
                parser.start_response();
            } else {
                spurious_writes++;
                stop_events(EV_WRITE);
            }
            return false;
        }
        buffer.reset();
        std::swap(buffer, frontend.buffer);
        frontend.start_events(EV_READ);
    }
    IOBuffer::Status err = buffer.send(conn_watcher.fd);

    switch (err) {
    case IOBuffer::SHUTDOWN:
    case IOBuffer::OTHER_ERROR:
        proxy.release();
        return true;
    case IOBuffer::WOULDBLOCK:
        return false;
    default:
        break;
    }
    return false;
}


bool
Proxy::Backend::read_callback()
{
    buffer::string recv_chunk;
    IOBuffer::Status err = buffer.recv(conn_watcher.fd, recv_chunk);

    switch (err) {
    case IOBuffer::BUFFER_FULL:
        spurious_reads++;
        stop_events(EV_READ);
        return false;
    case IOBuffer::SHUTDOWN:
        stop_all_events();
        // TODO: check protocol, content-length, etc. to notify if its illegal to shutdown now
        progress = RESPONSE_FINISHED;
        debug("B: changed progress: ", progress);
        frontend.start_events(EV_WRITE);
        return false;
    case IOBuffer::OTHER_ERROR:
        proxy.release();
        return true;
    case IOBuffer::WOULDBLOCK:
        return false;
    default:
        break;
    }

    assert(progress >= REQUEST_FINISHED);

    HTTPParser::Status s;
    switch (progress) {
    case RESPONSE_STARTED:
        // Frontend EV_WRITE is stopped, so we can parse head chunk by chunk easy,
        // until we finish the head (but limit is the same: buffer size) ...
        s = parser.parse_head(recv_chunk);

        switch (s) {
        case HTTPParser::PROCEED: // reached head end
        {
        #ifndef NDEBUG
            char cl[20] = "unset";
            if (parser.content_length != HTTPParser::cl_unset)
                snprintf(cl, sizeof(cl), "%lu", parser.content_length);

            debug("B: got response: ",
                parser.status_code, ' ', parser.reason_phrase,
                " (cl: ", cl,
                ", chunked: ", parser.chunked,
                ", keep-alive: ", parser.keep_alive, ")");
        #endif
            progress = parser.content_length == 0 ?
                RESPONSE_FINISHED :
                (parser.content_length == HTTPParser::cl_unset && !parser.chunked ?
                    (parser.keep_alive ? RESPONSE_FINISHED : RESPONSE_WAIT_SHUTDOWN) :
                    RESPONSE_HEAD_FINISHED);
            debug("B: changed progress: ", progress);

            // ... and start EV_WRITE when we finished the head.
            frontend.start_only_events(EV_WRITE);

            switch (progress) {
            case RESPONSE_FINISHED:
                goto RESPONSE_FINISHED;
            case RESPONSE_WAIT_SHUTDOWN:
                goto RESPONSE_WAIT_SHUTDOWN;
            default:
                break;
            }

            if (recv_chunk.empty())
                return false;

            break;
        }
        case HTTPParser::TERMINATE:
            error("B: parsing HTTP response failed!");
            proxy.release();
            return true;
        case HTTPParser::CONTINUE:
        default:
            return false;
        } // switch (HTTPParser::Status)

    case RESPONSE_HEAD_FINISHED:
        s = parser.parse_body(recv_chunk);
        switch (s) {
        case HTTPParser::PROCEED: // reached body end
            progress = RESPONSE_FINISHED;
            debug("B: changed progress: ", progress);
            frontend.start_events(EV_WRITE);
            goto RESPONSE_FINISHED;
        case HTTPParser::TERMINATE:
            error("B: parsing HTTP response body failed!");
            proxy.release();
            return true;
        case HTTPParser::CONTINUE:
        default:
            frontend.start_events(EV_WRITE);
            return false;
        } // switch (HTTPParser::Status)

    RESPONSE_WAIT_SHUTDOWN:
    case RESPONSE_WAIT_SHUTDOWN:
        // In case of non-persistent connection we just pass body of unknown size
        // to frontend until we receive connection shutdown.
        // TODO: maybe timeout it or put maxbody limit
        return false;

    case RESPONSE_FINISHED:
        error("B: unexpected data on finished response!");
    RESPONSE_FINISHED:
        // We can't disable READ, because any time backend may tear connection.
        ;
    } // switch (progress)
    return false;
}

#ifdef NDEBUG
const char * const IOBuffer::prefix = "";
#endif
