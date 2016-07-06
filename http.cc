#include <algorithm>
#include <cassert>
#include "http.h"
#include "util.h"
#include "connection.h"
#include <arpa/inet.h>

const std::string CRLF("\r\n");
const std::string WSP("\t ");
const std::string LWSP("\t \r\n");
const std::string CHUNKED("chunked");
const std::string NO_TRANSFORM("no-transform");
const std::string MARKER_TERMINATORS(";\r");

enum class Header;

struct KnownHeaders
{
    static const
    std::vector<std::string>
        names;

    static
    Header find(buffer::istring& field);

    static
    const std::string& get (Header h)
    {
        return names[int(h)];
    }

private:
    static const char * _names[];
    static const size_t _count;
    static void assert_count();
};

enum class Header
{
    CACHE_CONTROL = 0,
    CONTENT_LENGTH,
    HOST,
    TRANSFER_ENCODING,
    VIA,
    X_FORWARDED_FOR,
    unknown /* must be the last element */
};

const char * KnownHeaders::_names[] = {
    /* must be in order of enum! */
        "cache-control",
        "content-length",
        "host",
        "transfer-encoding",
        "via",
        "x-forwarded-for"
};

const size_t KnownHeaders::_count = sizeof(_names) / sizeof(_names[0]);

Header KnownHeaders::find(buffer::istring& field)
{
    for (int i = 0; i < names.size(); ++i) {
        if (names[i] == field) {
            return Header(i);
        }
    }
    return Header::unknown;
}

void KnownHeaders::assert_count()
{
    static_assert(_count == size_t (Header::unknown),
        "KnownHeaders: enum and vector mismatch!");
}

const std::vector<std::string>
KnownHeaders::names(_names, _names + _count);



HTTPParser::HTTPParser(IOBuffer &input_buf_, IOBuffer &output_buf_, int conn_fd) :
    parse_line { &HTTPParser::parse_request_line },
    input_buf { input_buf_ },
    output_buf { output_buf_ }
{
    reset();
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);
    if (getsockname(conn_fd, (sockaddr *) &addr, &addr_len))
        throw Errno("getsockname");
    strncpy(local_addr_buf + 1, inet_ntoa(addr.sin_addr), sizeof(local_addr_buf) - 2);
    local_addr_buf[0] = ' ';
    local_addr_buf[sizeof(local_addr_buf) - 2] = 0;
    size_t len = strlen(local_addr_buf);
    local_addr_buf[len++] = '\r';
    local_addr_buf[len++] = '\n';
    local_address.assign(local_addr_buf, len);

    addr_len = sizeof(addr);
    if (getpeername(conn_fd, (sockaddr *) &addr, &addr_len))
        throw Errno("getpeername");
    strncpy(peer_addr_buf, inet_ntoa(addr.sin_addr), sizeof(peer_addr_buf) - 1);
    peer_addr_buf[sizeof(peer_addr_buf) - 1] = 0;
    len = strlen(peer_addr_buf);
    peer_addr_buf[len++] = '\r';
    peer_addr_buf[len++] = '\n';
    peer_address.assign(peer_addr_buf, len);
}

bool HTTPParser::copy_line(const buffer::string &line)
{
    if (line.size() > output_buf.free_size()) {
        error("Not enough space in output buffer!");
        return true;
    }

    output_buf.grow(line.size());
    line.copy(output_buf);
    output_buf.shrink_front(line.size());
    return false;
}

bool HTTPParser::copy_modified_headers()
{
    static const std::string via_h = KnownHeaders::get(Header::VIA) + ": ";
    static const std::string xforw_h = KnownHeaders::get(Header::X_FORWARDED_FOR) + ": ";
    static const std::string comma = ", ";

    if (via.empty()) {
        if (!no_transform) {
            if (copy_line(via_h) ||
                copy_line(http_version) ||
                copy_line(local_address))
                return true;
        }
    } else {
        if (copy_line(via))
            return true;
        if (!no_transform) {
            if (copy_line(comma) ||
                copy_line(http_version) ||
                copy_line(local_address))
                return true;
        }
    }

    if (x_forwarded_for.empty()) {
        if (!no_transform) {
            if (copy_line(xforw_h) ||
                copy_line(peer_address))
                return true;
        }
    } else {
        if (copy_line(via))
            return true;
        if (!no_transform) {
            if (copy_line(comma) ||
                copy_line(peer_address))
                return true;
        }
    }
    return false;
}

bool HTTPParser::next_line()
{
    // scan_buf may be empty (see last comment in this loop)
    while (scan_buf.size()) {
        size_t crlf = scan_buf.find(CRLF);
        if (crlf == buffer::string::npos) {
            return false;
        }
        size_t crlf_end = crlf + CRLF.size();
        if (!found_line.empty() && found_line.end() != &scan_buf[crlf]) {
            // Second condition (found_line.end() != &scan_buf[crlf]) is 
            // for CRLFCRLF case. Otherwise, we will miss it by 
            // (crlf_end == scan_buf.size()) fork below.

            // We already got request line and now do some
            // special handling of header lines (for multi-line headers).
            if (crlf_end == scan_buf.size()) {
                // We are at the end of scan_buf, don't accept this line yet,
                // because header can continue on next line.
                scan_buf_store = scan_buf;
                return false;
            }

            if (WSP.find(scan_buf[crlf_end]) != std::string::npos) {
                // CRLF is followed by WSP means that header is continued on next line.
                scan_buf.assign(&scan_buf[crlf_end + 1], scan_buf.end());
                continue;
            }
        }
        found_line.assign(found_line.empty() ? input_buf.begin() : found_line.end(), &scan_buf[crlf_end]);
        // Can make scan_buf empty when request line ends exactly on chunk boundary:
        scan_buf.assign(&scan_buf[crlf_end], scan_buf.end());
        return true;
    }
    return false;
}

HTTPParser::Status
HTTPParser::parse_request_line()
{
    size_t sp1 = found_line.find(' ');
    if (sp1 == buffer::string::npos) {
        debug("Wrong request line: no space after Method!");
        return TERMINATE;
    }

    method.assign(found_line.begin(), sp1);
    
    ++sp1;
    if (&found_line[sp1] == found_line.end()) {
        debug("Wrong request line: no Request-URI!");
        return TERMINATE;
    }

    size_t sp2 = found_line.find(' ', sp1);
    if (sp2 == buffer::string::npos) {
        debug("Wrong request line: no space after Request-URI!");
        return TERMINATE;
    }

    request_uri.assign(&found_line[sp1], &found_line[sp2]);
    ++sp2;
    if (&found_line[sp2] >= found_line.end() - CRLF.size()) {
        debug("Wrong request line: no Protocol!");
        return TERMINATE;
    }

    size_t sl = found_line.find('/', sp2);
    if (sl == buffer::string::npos) {
        debug("Wrong request line: no slash in Protocol!");
        return TERMINATE;
    }

    ++sl;
    if (&found_line[sl] >= found_line.end() - CRLF.size()) {
        debug("Wrong request line: no Protocol Version!");
        return TERMINATE;
    }

    http_version.assign(&found_line[sl], found_line.end() - CRLF.size());
    parse_line = &HTTPParser::parse_header_line;

    if (copy_found_line())
        return TERMINATE;

    return CONTINUE;
}

template <class STRING>
bool
HTTPParser::get_header_value(STRING& value, size_t& cl)
{
    ++cl;
    if (&found_line[cl] >= found_line.end() - CRLF.size()) {
        debug("Wrong header line: no value!");
        return true;
    }

    size_t val = found_line.find_first_not_of(LWSP, cl);
    if (val == STRING::npos) {
        debug("Wrong header line: no value (2)!");
        return true;
    }
    value.assign(&found_line[val], found_line.end() - CRLF.size());
    return false;
}

HTTPParser::Status
HTTPParser::parse_header_line()
{
    if (found_line.size() == CRLF.size()) {
        // found CRLFCRLF sequence

        if (copy_modified_headers())
            return TERMINATE;

        if (copy_found_line())
            return TERMINATE;

        input_buf.assign(found_line.end(), input_buf.end());
        output_buf.assign(output_buf.buffer_begin(), output_buf.end());
        return PROCEED;
    }

    size_t colon = found_line.find(':');
    if (colon == buffer::string::npos) {
        debug("Wrong header line: no colon char!");
        return TERMINATE;
    }

    // Optimization: eliminate uppercasing of static strings
    buffer::istring name(found_line.begin(), colon);
    Header header = KnownHeaders::find(name);

    switch (header) {
    case Header::HOST:
    {
        if (copy_found_line())
            return TERMINATE;

        if (get_header_value(host, colon))
            return TERMINATE;

        colon = host.find(':');
        if (colon != buffer::string::npos) {
            if (colon + 1 < host.size()) {
                buffer::string port_(&host[colon + 1], host.end());
                port = buffer::stol(port_);
            }
            host.assign(host.begin(), &host[colon]);
        }
        // fix host terminator to make getaddrinfo happy
        assert(host.end() < found_line.end());
        host_terminator = *host.end();
        *const_cast<char*>(host.end()) = 0;
        host_cstr = host.begin();
    }
    break;
    case Header::CONTENT_LENGTH:
    {
        if (copy_found_line())
            return TERMINATE;

        buffer::string content_length;
        if (get_header_value(content_length, colon))
            return TERMINATE;

        clength = buffer::stol(content_length);
    }
    break;
    case Header::TRANSFER_ENCODING:
    {
        if (copy_found_line())
            return TERMINATE;

        buffer::istring transfer_encoding;
        if (get_header_value(transfer_encoding, colon))
            return TERMINATE;

        if (transfer_encoding == CHUNKED) {
            chunked = true;
        }
    }
    break;
    case Header::CACHE_CONTROL:
    {
        if (copy_found_line())
            return TERMINATE;

        buffer::istring cache_control;
        if (get_header_value(cache_control, colon))
            return TERMINATE;

        if (cache_control == NO_TRANSFORM) {
            no_transform = true;
        }
        break;
    }
    case Header::VIA:
        via = found_line;
        break;
    case Header::X_FORWARDED_FOR:
        x_forwarded_for = found_line;
        break;
    case Header::unknown:
    default:
        if (copy_found_line())
            return TERMINATE;
        break;
    }

    return CONTINUE;
}


HTTPParser::Status HTTPParser::parse_head(buffer::string &recv_chunk)
{
    assert(!recv_chunk.empty());
    
    if (!scan_buf_store.empty()) {
        scan_buf.assign(scan_buf_store.begin(), recv_chunk.end());
        scan_buf_store.clear();
    } else if (recv_chunk.begin() > &input_buf[CRLF.size() - 1]) {
        // Position scan_buf to (recv_buf - CRLF.size() + 1):
        // full_buf already contains more than (CRLF.size() - 1) bytes,
        // we shift back recv_buf by this value. This is done for case
        // when beginning of recv_buf contains tail of CRLF (CRLF was split by 2 chunks).
        scan_buf.assign(&recv_chunk[1 - CRLF.size()], recv_chunk.end());
    } else {
        scan_buf.assign(input_buf.begin(), recv_chunk.end());
    }

    if (scan_buf.size() < CRLF.size())
        return CONTINUE;

    while (next_line()) {
        Status res = (this->*parse_line)();
        if (res != CONTINUE) {
            recv_chunk = input_buf;
            return res;
        }
    }
    return CONTINUE;
}


HTTPParser::Status HTTPParser::parse_body(buffer::string &recv_chunk)
{
    assert(!recv_chunk.empty());
    while (!recv_chunk.empty()) {
        size_t cr;
        switch (crlf_search) {
        case MARKER_CR_SEARCH:
            cr = recv_chunk.find_first_of('\r');
            if (cr == buffer::string::npos)
                return CONTINUE;
            if (cr == recv_chunk.size() - 1) {
                crlf_search = MARKER_LF_EXPECT;
                return CONTINUE;
            }
            if (recv_chunk[cr + 1] == '\n') {
                recv_chunk.shrink_front(cr + 2);
                goto found_marker_end;
            }
            recv_chunk.shrink_front(cr + 1);
            continue;
        case MARKER_LF_EXPECT:
            if (recv_chunk[0] == '\n') {
                recv_chunk.shrink_front(1);
            found_marker_end:
                if (marker_hoarder == 0) {
                    crlf_search = CHUNK_CR_EXPECT;
                    body_end = true;
                } else {
                    crlf_search = NO_SEARCH;
                    skip_chunk = marker_hoarder;
                    marker_hoarder = 0;
                }
                continue;
            }
            recv_chunk.shrink_front(1);
            crlf_search = MARKER_CR_SEARCH;
            continue;
        case CHUNK_CR_EXPECT:
            if (recv_chunk[0] != '\r') {
                if (body_end) {
                    // Got trailer headers, need CRLFCRLF to get to actual message end.
                    recv_chunk.shrink_front(1);
                    crlf_search = TRAILER_CR_SEARCH;
                    continue;
                }
                debug("Wrong chunk terminator: not CRLF (CR not matched)!");
                return TERMINATE;
            }
            crlf_search = CHUNK_LF_EXPECT;
            recv_chunk.shrink_front(1);
            continue;
        case CHUNK_LF_EXPECT:
            if (recv_chunk[0] != '\n') {
                debug("Wrong chunk terminator: not CRLF (LF not matched)!");
                return TERMINATE;
            }
            if (body_end) {
                assert(recv_chunk.size() == 1);
                return PROCEED;
            }
            crlf_search = NO_SEARCH;
            recv_chunk.shrink_front(1);
            continue;
        case TRAILER_CR_SEARCH:
            cr = recv_chunk.find_first_of('\r');
            if (cr == buffer::string::npos)
                return CONTINUE;
            recv_chunk.shrink_front(cr + 1);
            crlf_search = TRAILER_LF_EXPECT;
            continue;
        case TRAILER_LF_EXPECT:
            if (recv_chunk[0] != '\n') {
                recv_chunk.shrink_front(1);
                crlf_search = TRAILER_CR_SEARCH;
                continue;
            }
            recv_chunk.shrink_front(1);
            crlf_search = TRAILER_CR2_EXPECT;
            continue;
        case TRAILER_CR2_EXPECT:
            if (recv_chunk[0] != '\r') {
                recv_chunk.shrink_front(1);
                crlf_search = TRAILER_CR_SEARCH;
                continue;
            }
            recv_chunk.shrink_front(1);
            crlf_search = TRAILER_LF2_EXPECT;
            continue;
        case TRAILER_LF2_EXPECT:
            if (recv_chunk[0] != '\n') {
                recv_chunk.shrink_front(1);
                crlf_search = TRAILER_CR_SEARCH;
                continue;
            }
            assert(recv_chunk.size() == 1);
            return PROCEED;
        case NO_SEARCH:
        default:
            break;
        }

        if (skip_chunk >= recv_chunk.size()) {
            skip_chunk -= recv_chunk.size();
            if (skip_chunk == 0) {
                crlf_search = CHUNK_CR_EXPECT;
            }
            return CONTINUE;
        }

        if (skip_chunk > 0) {
            assert(marker_hoarder == 0);
            recv_chunk.shrink_front(skip_chunk);
            skip_chunk = 0;
            crlf_search = CHUNK_CR_EXPECT;
            continue;
        }

        if (marker_hoarder && (recv_chunk[0] == '\r' || recv_chunk[0] == ';')) {
            crlf_search = MARKER_CR_SEARCH;
            continue;
        }

        // Now we are at the start (or in the middle) of chunk marker and need to find CRLF
        // to actually start skipping. But we have situation different (and worse)
        // than in parse_head()! Now the buffer is not permanent: it may be taken for output
        // at any time! So the worst case scenario is: CR in the end of one buffer goes
        // away to ParseBackend and LF comes in another buffer. The more complication is:
        // actual marker also may be split by buffer boundaries. So, we need to collect it to some
        // dedicated place if we can't acknowledge its end in current recv_chunk.

        size_t digits;
        long marker_part = buffer::stol(recv_chunk, &digits, 16);

        if (errno) {
            debug("Wrong chunk marker: ", strerror(errno));
            return TERMINATE;
        }

        if (recv_chunk.size() > digits && recv_chunk[digits] != ';' && recv_chunk[digits] != '\r') {
            debug("Wrong chunk marker: wrong size terminator");
            return TERMINATE;
        }

        if (marker_hoarder) {
            unsigned bits = digits << 3; // bits to shift
            if (marker_hoarder > SIZE_MAX >> bits) {
                debug("Wrong chunk marker: too big!");
                return TERMINATE;
            }
            marker_hoarder <<= bits;
            marker_hoarder += marker_part;
        } else {
            marker_hoarder = marker_part;
        }

        if (digits == recv_chunk.size()) {
            return CONTINUE;
        }

        crlf_search = MARKER_CR_SEARCH;
        recv_chunk.shrink_front(digits);
    } // while (!recv_chunk.empty())
    return CONTINUE;
}
