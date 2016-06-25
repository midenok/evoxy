#include <algorithm>
#include <cassert>
#include "http.h"
#include "util.h"
#include "connection.h"

const std::string CRLF("\r\n");
const std::string WSP("\t ");
const std::string LWSP("\t \r\n");
const std::string HOST("host");
const std::string CONTENT_LENGTH("content-length");
const std::string TRANSFER_ENCODING("transfer-encoding");
const std::string CHUNKED("chunked");
const std::string MARKER_TERMINATORS(";\r");

HTTPParser::HTTPParser(IOBuffer &input_buf_, IOBuffer &output_buf_) :
    parse_line { &HTTPParser::parse_request_line },
    input_buf { input_buf_ },
    output_buf { output_buf_ }
{
    reset();
}

bool HTTPParser::copy_found_line()
{
    if (found_line.size() > output_buf.free_size()) {
        error("Not enough space in output buffer!");
        return true;
    }

    output_buf.grow(found_line.size());
    found_line.copy(output_buf);
    output_buf.shrink_front(found_line.size());
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
        debug("Wrong request line: no HTTP-Version!");
        return TERMINATE;
    }

    http_version.assign(&found_line[sp2], found_line.end() - CRLF.size());
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
        if (copy_found_line())
            return TERMINATE;
        input_buf.assign(found_line.end(), input_buf.end());
        output_buf.assign(output_buf.buffer_begin(), output_buf.end());
        return PROCEED;
    }

    size_t cl = found_line.find(':');
    if (cl == buffer::string::npos) {
        debug("Wrong header line: no colon char!");
        return TERMINATE;
    }

    // TODO: modify headers in output
    if (copy_found_line())
        return TERMINATE;

    // TODO: optimization: eliminate uppercasing of static strings
    buffer::istring name(found_line.begin(), cl);

    if (name == HOST) {
        if (get_header_value(host, cl))
            return TERMINATE;

        cl = host.find(':');
        if (cl != buffer::string::npos) {
            host.assign(host.begin(), &host[cl]);
            if (++cl < host.size()) {
                buffer::string port_(&host[cl], host.end());
                port = buffer::stol(port_);
            }
        }
        // fix host terminator to make getaddrinfo happy
        assert(host.end() < found_line.end());
        host_terminator = *host.end();
        *const_cast<char*>(host.end()) = 0;
        host_cstr = host.begin();
    }
    else if (name == CONTENT_LENGTH) {
        buffer::string content_length;
        if (get_header_value(content_length, cl))
            return TERMINATE;

        clength = buffer::stol(content_length);
    }
    else if (name == TRANSFER_ENCODING) {
        buffer::istring transfer_encoding;
        if (get_header_value(transfer_encoding, cl))
            return TERMINATE;

        if (transfer_encoding == CHUNKED) {
            chunked = true;
        }
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
        if (res != CONTINUE)
            return res;
    }
    return CONTINUE;
}


HTTPParser::Status HTTPParser::parse_body(buffer::string& recv_chunk)
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
            if (body_end)
                return PROCEED;
            crlf_search = NO_SEARCH;
            recv_chunk.shrink_front(1);
            continue;
        case NO_SEARCH:
        default:
            break;
        }

        if (skip_chunk >= recv_chunk.size()) {
            skip_chunk -= recv_chunk.size();
            return CONTINUE;
        }

        if (skip_chunk > 0) {
            assert(marker_hoarder == 0);
            recv_chunk.shrink_front(skip_chunk);
            skip_chunk = 0;
            crlf_search = CHUNK_CR_EXPECT;
            continue;
        }

        // Now we are at the start (or in the middle) of chunk marker and need to find CRLF
        // to actually start skipping. But we have situation different (and worse)
        // than in parse_head()! Now the buffer is not permanent: it may be taken by ParseBackend
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
