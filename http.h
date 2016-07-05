#ifndef __cd_http_h
#define __cd_http_h

#include <string>
#include "buffer_string.h"

class ProxyFrontend;
class IOBuffer;

class HTTPParser
{
public:
    enum Status {
        // Terminate connection
        TERMINATE = 0,
        // Continue current phase
        CONTINUE,
        // Proceed to next phase
        PROCEED
    };

private:
    typedef Status(HTTPParser::*parse_f)();
    parse_f parse_line;
    ProxyFrontend &frontend; // FIXME: duplicates input_buf on request
    IOBuffer &input_buf; /* ProxyFrontend buffer on request, ProxyBackend buffer on response */
    IOBuffer &output_buf; /* ProxyBackend buffer on request */
    buffer::string scan_buf;
    buffer::string scan_buf_store;
    buffer::string found_line;
    template <class STRING>
    bool get_header_value(STRING& value, size_t& cl);

    /* copy headers into ProxyBackend buffer */
    bool copy_line(const buffer::string &line);
    bool copy_found_line()
    {
        return copy_line(found_line);
    }
    bool copy_modified_headers();

public:
    buffer::string method;
    buffer::string request_uri;
    buffer::string http_version;
    buffer::string host;
    buffer::string via;
    buffer::string x_forwarded_for;

    const char* host_cstr;
    char host_terminator; // currently unused
    uint32_t port;
    uint32_t clength;
    bool chunked;
    bool no_transform;

    bool next_line();
    Status parse_request_line();
    Status parse_header_line();
    HTTPParser(ProxyFrontend &frontend, IOBuffer &input_buf_, IOBuffer &output_buf_);
    Status parse_head(buffer::string &recv_chunk);
    Status parse_body(buffer::string &recv_chunk);

    enum CRLFSearch
    {
        NO_SEARCH = 0,
        MARKER_CR_SEARCH = 1, // ++ must result in MARKER_LF_EXPECT
        MARKER_LF_EXPECT = 2,
        CHUNK_CR_EXPECT = 3,
        CHUNK_LF_EXPECT = 4,
        TRAILER_CR_SEARCH = 5, // ++ must result in TRAILER_LF_EXPECT
        TRAILER_LF_EXPECT = 6,
        TRAILER_CR2_EXPECT = 7,
        TRAILER_LF2_EXPECT = 8
    };

private:
    size_t skip_chunk;
    size_t marker_hoarder;
    bool body_end;

    CRLFSearch crlf_search;

public:
    // FIXME: reset() on RESPONSE_FINISHED
    void reset() // reset state to initial
    {
        port = 80;
        clength = 0;
        chunked = false;
        skip_chunk = 0;
        marker_hoarder = 0;
        crlf_search = NO_SEARCH;
        body_end = false;
        no_transform = false;
    }
};

#endif // __cd_http_h
