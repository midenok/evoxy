#ifndef __cd_http_h
#define __cd_http_h

#include <string>
#include "buffer_string.h"

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
        HEAD_FINISHED,
        BODY_FINISHED
    };

private:
    typedef Status(HTTPParser::*parse_f)();
    parse_f parse_line;
    IOBuffer &input_buf;
    IOBuffer &output_buf;
    buffer::string scan_buf;
    buffer::string scan_buf_store;
    buffer::string found_line;
    template <class STRING>
    bool get_header_value(STRING& value, size_t& cl);

    bool copy_found_line();

public:
    buffer::string method;
    buffer::string request_uri;
    buffer::string http_version;
    buffer::string host;

    const char* host_cstr;
    char host_terminator; // currently unused
    uint32_t port;
    uint32_t clength;
    bool chunked;

    bool next_line();
    Status parse_request_line();
    Status parse_header_line();
    Status parse_chunks();
    HTTPParser(IOBuffer &input_buf_, IOBuffer &output_buf_);
    Status parse_head(buffer::string &recv_chunk);
    Status parse_body(buffer::string &recv_chunk);

private:
    size_t skip_chunk;
    size_t marker_hoarder;

    enum MarkerSearch
    {
        NO_SEARCH = 0,
        CR_SEARCH = 1,
        LF_SEARCH = 2
    } marker_end_search;

public:
    // FIXME: reset() on RESPONSE_FINISHED
    void reset() // reset state to initial
    {
        port = 80;
        clength = 0;
        chunked = false;
        skip_chunk = 0;
        marker_hoarder = 0;
        marker_end_search = NO_SEARCH;
    }
};

#endif // __cd_http_h
