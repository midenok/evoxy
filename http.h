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
        HEAD_FINISHED
    };

private:
    typedef Status(HTTPParser::*parse_f)();
    parse_f parse_line;
    buffer::string &input_buf;
    IOBuffer &output_buf;
    buffer::string scan_buf;
    buffer::string scan_buf_store;
    buffer::string found_line;

    template <class STRING>
    bool get_header_value(STRING& value, size_t& cl);

    bool copy_found_line();

public:
    buffer::string start_line;
    buffer::string method;
    buffer::string request_uri;
    buffer::string http_version;
    buffer::string host;
    buffer::string content_length;
    buffer::istring transfer_encoding;

    const char* host_cstr;
    char host_terminator; // currently unused
    uint32_t port = 80;
    uint32_t clength = 0;
    bool chunked = false;

    bool next_line();
    Status parse_request_line();
    Status parse_header_line();

    HTTPParser(buffer::string &input_buf_, IOBuffer &output_buf_);
    Status
        operator()(buffer::string &recv_chunk);
};

#endif // __cd_http_h
