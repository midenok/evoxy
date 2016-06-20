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
    buffer::string &full_buf;
    buffer::string scan_buf;
    buffer::string scan_buf_store;
    buffer::string found_line;
 
public:
    buffer::string start_line;
    buffer::string method;
    buffer::string request_uri;
    buffer::string http_version;
    buffer::string host_header;

public:
    HTTPParser(buffer::string &full_buf_);

    bool
        next_line();

    HTTPParser::Status parse_request_line();
    HTTPParser::Status parse_header_line();

    Status
        operator()(buffer::string recv_buf);
};

#endif // __cd_http_h
