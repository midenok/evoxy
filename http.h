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
        PROCEED
    };

private:
    typedef Status(HTTPParser::*parse_f)();
    parse_f parse_line;
    IOBuffer *input_buf = nullptr; /* Proxy::Frontend buffer on request, Proxy::Backend buffer on response */
    IOBuffer *output_buf = nullptr; /* Proxy::Backend buffer on request */
    buffer::string scan_buf;
    buffer::string scan_buf_store;
    buffer::string found_line;

    /* via header, with space at beginning, CRLF terminated */
    char local_addr_buf[18]; // space: 1, ip: 15, CRLF: 2
    buffer::string local_address;

    /* x-forwarded-for header, CRLF terminated */
    char peer_addr_buf[17]; // ip: 15, CRLF: 2
    buffer::string peer_address;

    template <class STRING>
    bool get_header_value(STRING& value, size_t& cl);

    /* copy headers into Proxy::Backend buffer */
    bool copy_line(const buffer::string &line);
    bool copy_found_line()
    {
        return copy_line(found_line);
    }
    bool copy_modified_headers();

public:
    /* Request properties */
    buffer::string method;
    buffer::string request_uri;
    buffer::istring host;
    buffer::string via;
    buffer::string x_forwarded_for;

    bool no_transform;
    uint32_t port;

    /* Response properties */ // TODO: put into union with Request properties
    buffer::string status_code;
    buffer::string reason_phrase;
    bool keep_alive = false; // is not reset
    bool force_close = false; // is not reset
    unsigned request_version = 0; // is not reset
    unsigned response_version = 0; // is not reset

    /* Common properties */
    buffer::string http_version;
    size_t content_length;
    static const size_t cl_unset = -1;
    bool chunked;

    HTTPParser(IOBuffer &input_buf_, IOBuffer &output_buf_, int conn_fd);
    void parse_http_version(unsigned& version);
    Status parse_request_line();
    Status parse_response_line();
    Status parse_request_head();
    Status parse_response_head();
    Status parse_head(buffer::string &recv_chunk);
    Status parse_body(buffer::string &recv_chunk);
    bool next_line();


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

    // reset state to initial
    void reset()
    {
        port = 80;
        content_length = cl_unset;
        chunked = false;
        skip_chunk = 0;
        marker_hoarder = cl_unset;
        crlf_search = NO_SEARCH;
        body_end = false;
        no_transform = false;
    }

 public:
    void restart_request(IOBuffer &buffer)
    {
        reset();
        found_line.clear();
        parse_line = &HTTPParser::parse_request_line;
        input_buf = &buffer;
    }

    void start_response()
    {
        reset();
        found_line.clear();
        parse_line = &HTTPParser::parse_response_line;
        // NB: vague buffer swap logic is prone to errors
        input_buf = output_buf;
    }
};

#endif // __cd_http_h
