#ifndef __cd_http_h
#define __cd_http_h

#include <string>
#include "buffer_string.h"

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
    buffer::string &input_buf;
    buffer::string output_buf;
    buffer::string scan_buf;
    buffer::string scan_buf_store;
    buffer::string found_line;

public:
    buffer::string start_line;
    buffer::string method;
    buffer::string request_uri;
    buffer::string http_version;
    buffer::string host;
    const char* host_cstr;
    uint32_t port = 80;
    char host_terminator;

    bool next_line();
    Status parse_request_line();
    Status parse_header_line();

public:
    HTTPParser(buffer::string &input_buf_, buffer::string output_buf_) :
        parse_line { &HTTPParser::parse_request_line },
        input_buf { input_buf_ },
        output_buf { output_buf_ } {}

    Status
        operator()(buffer::string recv_buf);

    buffer::string::pointer output_end()
    {
        return output_buf.begin();
    }
};

#endif // __cd_http_h
