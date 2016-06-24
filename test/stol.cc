#include <buffer_string.h>
#include <cstdlib>
#include <iostream>

using namespace buffer;

int check_invocation = 0;

void check(const char * in, size_t base, long answer_num, size_t answer_pos = string::npos, int answer_errno = 0)
{
    string s(in);
    size_t pos = 0;
    ++check_invocation;

    long result = stol(s, &pos, base);

    if (answer_pos == string::npos)
        answer_pos = s.length();

    if (result != answer_num) {
        std::cerr << "Failed check " << check_invocation <<
            ": stol(\"" << in << "\", " << base << ") "
            "result: " << result << "; expected: " << answer_num << "\n";
        exit(1);
    }
    if (pos != answer_pos) {
        std::cerr << "Failed check " << check_invocation <<
            ": stol(\"" << in << "\", " << base << ") "
            "pos: " << pos << "; expected: " << answer_pos << "\n";
        exit(2);
    }
    if (errno != answer_errno) {
        std::cerr << "Failed check " << check_invocation <<
            ": stol(\"" << in << "\", " << base << ") "
            "errno: " << errno << "; expected: " << answer_errno << "\n";
        exit(3);
    }
}


int main()
{
    check("ff", 16, 0xff);
    check("1000", 16, 0x1000);
    check("-1", 10, -1);
    check("+-1", 10, 0, 1, EINVAL);
    check("", 10, 0, 0, EINVAL);
    check("a", 10, 0, 0, EINVAL);
    check("777abcdef", 16, 0x777abcdef);
    check("777abcdef", 10, 777, 3);
    check("7fffffffffffffff", 16, 0x7fffffffffffffff);
    check("8000000000000000", 16, 0x8000000000000000 - 1, 16, ERANGE);
    check("-7FfFfFfFfFfFfFfF", 16, -0x7fffffffffffffff);
    check("-8000000000000000", 16, LONG_MIN, 17);
    check("-8000000000000001", 16, LONG_MIN, 17, ERANGE);
    check("800000000000000000000", 16, LONG_MAX, 16, ERANGE);
    check("100000000000000000000", 16, LONG_MAX, 17, ERANGE);
    std::cout << "Passed " << check_invocation << " checks.\n";
}
