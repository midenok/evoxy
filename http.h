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
    uint32_t port = 80;
    uint32_t clength = 0;
    bool chunked = false;

    size_t skip_chunk;
    static const int max_marker = 16;
    char chunk_marker[max_marker];
    size_t marker_stored = 0;

    bool next_line();
    Status parse_request_line();
    Status parse_header_line();
    Status parse_chunks();
    HTTPParser(IOBuffer &input_buf_, IOBuffer &output_buf_);
    Status parse_head(buffer::string &recv_chunk);
    Status parse_body(buffer::string &recv_chunk);
};

#endif // __cd_http_h
