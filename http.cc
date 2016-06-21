#include <algorithm>
#include <cassert>
#include "http.h"
#include "util.h"

const std::string CRLF("\r\n");
const std::string WSP("\t ");
const std::string LWSP("\t \r\n");
const std::string HOST("host");
const std::string UA("user-agent");

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

    if (found_line.size() > output_buf.size()) {
        error("Not enough space in output buffer!");
        return TERMINATE;
    }

    found_line.copy(output_buf);
    output_buf.shrink_front(found_line.size());
    return CONTINUE;
}

HTTPParser::Status
HTTPParser::parse_header_line()
{
    if (found_line.size() > output_buf.size()) {
        error("Not enough space in output buffer!");
        return TERMINATE;
    }

    if (found_line.size() == CRLF.size()) {
        // found CRLFCRLF sequence
        found_line.copy(output_buf);
        output_buf.shrink_front(found_line.size());
        return PROCEED;
    }

    size_t cl = found_line.find(':');
    if (cl == buffer::string::npos) {
        debug("Wrong header line: no colon char!");
        return TERMINATE;
    }

    found_line.copy(output_buf);
    output_buf.shrink_front(found_line.size());

    buffer::istring name(found_line.begin(), cl);

    if (name != HOST) {
        return CONTINUE;
    }
    ++cl;
    if (&found_line[cl] >= found_line.end() - CRLF.size()) {
        debug("Wrong header line: no value!");
        return TERMINATE;
    }

    size_t val = found_line.find_first_not_of(LWSP, cl);
    if (val == buffer::string::npos) {
        debug("Wrong header line: no value (2)!");
        return TERMINATE;
    }
    host.assign(&found_line[val], found_line.end() - CRLF.size());
    cl = host.find(':');
    if (cl != buffer::string::npos) {
        host.assign(host.begin(), &host[cl]);
        if (++cl < host.size()) {
            buffer::string port_(&host[cl], host.end());
            port = buffer::stoi(port_);
        }
    }
    // fix host terminator to make getaddrinfo happy
    host_terminator = *host.end();
    *const_cast<char*>(host.end()) = 0;
    host_cstr = host.begin();
    return CONTINUE;
}

HTTPParser::Status HTTPParser::operator()(buffer::string recv_buf)
{
    assert(!recv_buf.empty());
    
    if (!scan_buf_store.empty()) {
        scan_buf.assign(scan_buf_store.begin(), recv_buf.end());
        scan_buf_store.clear();
    } else if (recv_buf.begin() > &input_buf[CRLF.size() - 1]) {
        // Position scan_buf to (recv_buf - CRLF.size() + 1):
        // full_buf already contains more than (CRLF.size() - 1) bytes,
        // we shift back recv_buf by this value. This is done for case
        // when beginning of recv_buf contains tail of CRLF (CRLF was split by 2 chunks).
        scan_buf.assign(&recv_buf[1 - CRLF.size()], recv_buf.end());
    } else {
        scan_buf.assign(input_buf.begin(), recv_buf.end());
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
