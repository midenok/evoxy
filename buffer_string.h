#ifndef buffer_string_h
#define buffer_string_h

#include <ostream>
#include <iterator>
#include <locale>
#include <climits>

namespace buffer
{
template <typename CharT>
struct CharTraits : public std::char_traits<CharT>
{
    static
    int
    compare(const CharT* str1, const CharT* str2, size_t size)
    throw ();

    static
    CharT*
    copy(CharT* str1, const CharT* str2, size_t size)
    throw ();
};

template <typename CharT>
struct ci_char_traits : public std::char_traits<CharT>
{
    static bool eq(CharT c1, CharT c2)
    {
        return std::toupper(c1) == std::toupper(c2);
    }

    static bool lt(CharT c1, CharT c2)
    {
        return std::toupper(c1) < std::toupper(c2);
    }

    static int compare(const CharT* s1, const CharT* s2, size_t n)
    {
        while (n-- != 0) {
            if (std::toupper(*s1) < std::toupper(*s2)) return -1;
            if (std::toupper(*s1) > std::toupper(*s2)) return 1;
            ++s1;
            ++s2;
        }
        return 0;
    }

    static const CharT* find(const CharT* s, int n, CharT a)
    {
        auto const ua(std::toupper(a));
        while (n-- > 0) {
            if (std::toupper(*s) == ua)
                return s;
            s++;
        }
        return nullptr;
    }
};


/**
 * This class is designed to be a reference to the substring of somewhere
 * allocated string. It holds the pointer to the beginning and the length
 * of that substring and that's why can't live longer than the referred
 * string and its content.
 * Because of this the typedefs below use const char and const wchar_t
 * types.
 */
template <typename CharT,
          typename Traits = std::char_traits<
              typename std::remove_const<CharT>::type>>
class basic_string
{
public:
    // typedefs
    typedef std::size_t size_type;
    typedef CharT* pointer;
    typedef const CharT* const_pointer;
    typedef CharT& reference;
    typedef const CharT& const_reference;
    typedef CharT value_type;
    typedef typename std::remove_const<value_type>::type
    noconst_value_type;
    typedef std::basic_string<noconst_value_type> std_basic_string;
    typedef std::reverse_iterator<const_pointer> const_reverse_iterator;
    typedef std::reverse_iterator<pointer> reverse_iterator;
    static const size_type npos = std_basic_string::npos;

    // Constructors

    /**
     * Constructor from std::basic_string<...> (std::string, std::wstring
     * for example). Create BufferString covering the whole string.
     * @param str input BufferString for coverage
     */
    template <typename BasicStringTraits, typename Allocator>
    basic_string(
        const std::basic_string<noconst_value_type, BasicStringTraits,
                                Allocator>& str)
    throw () : begin_(str.data()),
               length_(str.size())
    { }

    /**
     * Constructor
     * @param ptr beginning of the BufferString
     * @param count size of the BufferString
     */
    basic_string(pointer ptr, size_type count)
    throw () : begin_(ptr),
               length_(count)
    { }

    /**
     * Constructor
     * @param begin pointer to the beginning of the string
     * @param end pointer to the element beyond the last of that string
     */
    basic_string(pointer begin, pointer end)
    throw () : begin_(begin)
    {
        length_ = end - begin;
    }

    /**
     * Constructor with zero-terminated c-string.
     * WARNING! This constructor is explicit because it searches the
     * terminating zero as the end of passed the string.
     * Make sure that you want this function to be called.
     * Also there is no other way to assign a string with unspecified
     * length to the object but using of this constructor.
     * @param ptr beginning pointer of the string
     */
    explicit
    basic_string(pointer ptr)
    throw () : begin_(ptr)
    {
        length_ = Traits::length(begin_);
    }

    /**
     * Construct an empty BufferString.
     */
    explicit
    basic_string()
    throw () : begin_(0),
               length_(0)
    { }


    /**
     * Get pointer to the content of a BufferString as an array of characters.
     * @return pointer to begin of array.
     */
    const_pointer
    data() const
    throw ();

    /**
     * @return length of BufferString
     */
    size_type
    length() const
    throw ();

    /**
     * @return the current number of elements in a BufferString.
     */
    size_type
    size() const
    throw ();

    /**
     * @return the maximum number of characters a string could contain.
     */
    size_type
    max_size() const
    throw ();

    /**
     * Tests whether the substring contains characters or not.
     * @return substring emptiness status
     */
    bool
    empty() const
    throw ();

    /**
     * Returns a const reference to the element at a specified location
     * in the BufferString.
     * @param pos specified index in a BufferString.
     * @return const reference to BufferString character.
     */
    const_reference
    at(size_type pos) const
    throw ();

    /**
     * Returns a reference to the element at a specified location
     * in the BufferString.
     * @param pos specified index in a BufferString.
     * @return reference to BufferString character.
     */
    reference
    at(size_type pos)
    throw ();

    /**
     * Get begin pointer of BufferString, on empty BufferString equal 0.
     * @return a const iterator addressing the first element in the string.
     */
    const_pointer
    begin() const
    throw ();

    /**
     * Get begin pointer of BufferString, on empty BufferString equal 0.
     * @return an iterator addressing the first element in the string.
     */
    pointer
    begin()
    throw ();

    /**
     * @return a const iterator that addresses the location succeeding
     * the last element in a string.
     */
    const_pointer
    end() const
    throw ();

    /**
     * @return an iterator that addresses the location succeeding
     * the last element in a string.
     */
    pointer
    end()
    throw ();

    /**
     * Determines the effective length rlen of the strings to compare as
     * the smallest of size() and str.size(). The function then compares
     * the two strings by calling
     * char_traits<CharType>::compare(data(), str.data(), rlen).
     *
     * Compares a string with a specified string to determine
     * if the two strings are equal or if one is lexicographically
     * less than the other.
     * @param str BufferString to compare with this
     * @return A negative value if the operand string is less than
     * the parameter string;
     * zero if the two strings are equal;
     * or a positive value if the operand string is greater than the
     * parameter string.
     *   Condition              Return result
     *    size() < str.size()   < 0
     *    size() == str.size()  0
     *    size() > str.size()   > 0
     */
    int
    compare(const basic_string& str) const
    throw ();

    /**
     * Compare *this with substring of BufferString.
     * @param pos1 position in *this to begin comparison from.
     * @param count1 maximum number of elements of *this to be compared with.
     * @param str comparable BufferString.
     * @return an integer less than, equal to, or greater than zero
     * if the requested part of *this is found,
     * respectively, to be less than, to match, or to be greater than str.
     */
    int
    compare(size_type pos1, size_type count1, const basic_string& str) const
    throw ();

    /**
     * Compare substring of *this with substring of BufferString.
     * @param pos1 position in *this to begin comparison from.
     * @param count1 maximum number of elements of *this to be compared with.
     * @param str comparable BufferString.
     * @param pos2 position in str to begin comparison from.
     * @param count2 maximum number of elements of str to be compared with.
     * @return an integer less than, equal to, or greater than zero
     * if part of *this is found,
     * respectively, to be less than, to match, or be greater
     * than the part of str.
     */
    int
    compare(size_type pos1, size_type count1, const basic_string& str,
            size_type pos2, size_type count2) const
    throw ();

    /**
     * Compare with zero-terminated C-string. Doesn't calculate string
     * length before comparison.
     * @param str comparable C-string.
     * @return an integer less than, equal to, or greater than zero
     * if *this is found, respectively, to be less than,
     * to match, or be greater than zero-terminated string.
     */
    int
    compare(const_pointer str) const
    throw ();

    /**
     * Compare substring of *this with zero-terminated C-string.
     * Doesn't calculate str length before comparison.
     * @param pos1 position in *this to begin comparison from.
     * @param count1 maximum number of elements of *this to be compared with.
     * @param str comparable C-string.
     * @return an integer less than, equal to, or greater than zero
     * if the requested part of *this is found, respectively, to be less than,
     * to match, or be greater than the zero-terminated string.
     */
    int
    compare(size_type pos1, size_type count1, const_pointer str) const
    throw ();

    /**
     * Compare substring of *this with count2 elements of
     * unterminated C-string.
     * @param pos1 position in *this to begin comparison from.
     * @param count1 maximum number of elements of *this to be compared with.
     * @param str comparable C-string.
     * @param count2 maximum characters of str to compare with.
     * @return an integer less than, equal to, or greater than zero
     * if the requested part of *this is found, respectively, to be less than,
     * to match, or be greater than string str with length count2.
     */
    int
    compare(size_type pos1, size_type count1, const_pointer str,
            size_type count2) const
    throw ();

    /**
     * Compare BufferString on equal with C-string.
     * @param str zero-terminated string to be compared with this.
     * @return true if BufferString equal str, else return false
     */
    bool
    equal(const_pointer str) const
    throw ();

    /**
     * Compare basic_string on equal with other basic_string.
     * SubStrings can contain internals zero.
     * @param str basic_string to be compared with this.
     * @return true if this equal str, else return false
     */
    bool
    equal(const basic_string& str) const
    throw ();

    /**
     * Copies at most a specified number of characters from an indexed
     * position in a source string to a target character array.
     * @param ptr target array
     * @param count the number of characters to be copied, at most,
     * from the source string.
     * @param pos the beginning position in the source string from which
     * copies are to be made.
     * @return the number of characters actually copied.
     */
    size_type
    copy(noconst_value_type* ptr, size_type count = npos, size_type pos = 0) const
    throw ();

    size_type
    copy(basic_string& dst, size_type count = npos, size_type pos = 0) const
        throw()
    {
        return copy(const_cast<noconst_value_type*>(dst.begin()), count, pos);
    }

    // Finders
    /**
     * Search in forward direction for first occurrence
     * of specified character.
     * @param ch character to search
     * @param pos start position for search
     * @return position in BufferString if the character found,
     * npos if not.
     */
    size_type
    find(value_type ch, size_type pos = 0) const
    throw ();

    size_type
    find(const_pointer ptr, size_type pos = 0) const
    throw ();

    /**
     * Find position of a C substring.
     * Starting from pos, searches forward for the first count characters
     * in ptr within this BufferString. If found, returns the index where it
     * begins.  If not found, returns npos.
     * @param ptr C string to locate.
     * @param pos Index of character to search from.
     * @param count Number of characters from ptr to search for.
     * @return Index of start of first occurrence.
     */
    size_type
    find(const_pointer ptr, size_type pos, size_type count) const
    throw ();

    /**
     * Find position of a BufferString.
     * Starting from pos, searches forward for value of str within
     * this BufferString. If found, returns the index where it begins.
     * If not found, returns npos.
     * @param str BufferString to locate.
     * @param pos Index of character to search from (default 0).
     * @return Index of start of first occurrence.
     */
    size_type
    find(const basic_string& str, size_type pos = 0) const
    throw ();

    /**
     * Find last position of a character
     * Searches a string in a backward direction for the first occurrence
     * of a character.
     * @param ch character to locate.
     * @param pos index of character to search back from (default end).
     * @return index of last occurrence if found, npos if not found.
     */
    size_type
    rfind(value_type ch, size_type pos = npos) const
    throw ();

    size_type
    rfind(const_pointer ptr, size_type pos = npos) const
    throw ();

    /**
     * Find last position of a C substring.
     * Starting from pos, searches backward for the first count
     * characters in ptr within this string. If found, returns the index
     * where it begins. If not found, returns npos.
     * @param ptr C string to locate.
     * @param pos Index of character to search back from.
     * @param count Number of characters from ptr to search for.
     * @return Index of start of last occurrence.
     */
    size_type
    rfind(const_pointer ptr, size_type pos, size_type count) const
    throw ();

    /**
     * Find last position of a BufferString.
     * Starting from pos, searches backward for value of str within
     * this BufferString. If found, returns the index where it begins. If not
     * found, returns npos.
     * @param str BufferString to locate.
     * @param pos Index of character to search back from (default end).
     * @return Index of start of last occurrence.
     */
    size_type
    rfind(const basic_string& str, size_type pos = npos) const
    throw ();

    /**
     * Searches through a string for the first character that matches
     * any element of a specified string.
     */
    size_type
    find_first_of(value_type ch, size_type pos = 0) const
    throw ();

    size_type
    find_first_of(const_pointer ptr, size_type pos = 0) const
    throw ();

    size_type
    find_first_of(const_pointer ptr, size_type pos, size_type count) const
    throw ();

    size_type
    find_first_of(const basic_string& str, size_type pos = 0) const
    throw ();

    /**
     * Searches through a string for the first character that is not
     * any element of a specified string.
     */
    size_type
    find_first_not_of(value_type ch, size_type pos = 0) const
    throw ();

    size_type
    find_first_not_of(const_pointer ptr, size_type pos = 0) const
    throw ();

    size_type
    find_first_not_of(const_pointer ptr, size_type pos, size_type count) const
    throw ();

    size_type
    find_first_not_of(const basic_string& str, size_type pos = 0) const
    throw ();

    /**
     * Searches through a string for the last character that is an
     * element of a specified string.
     */
    size_type
    find_last_of(value_type ch, size_type pos = npos) const
    throw ();

    size_type
    find_last_of(const_pointer ptr, size_type pos = npos) const
    throw ();

    size_type
    find_last_of(const_pointer ptr, size_type pos, size_type count) const
    throw ();

    size_type
    find_last_of(const basic_string& str, size_type pos = npos) const
    throw ();

    /**
     * Searches through a string for the last character that is not any
     * element of a specified string.
     */
    size_type
    find_last_not_of(value_type ch, size_type pos = npos) const
    throw ();

    size_type
    find_last_not_of(const_pointer ptr, size_type pos = npos) const
    throw ();

    size_type
    find_last_not_of(const_pointer ptr, size_type pos, size_type count) const
    throw ();

    size_type
    find_last_not_of(const basic_string& str, size_type pos = npos) const
    throw ();

    /**
     *  Returns an iterator to the first element in a reversed string.
     */
    const_reverse_iterator
    rbegin() const
    throw ();

    /**
     * non-const version
     * @return an iterator to the first element in a reversed string.
     */
    reverse_iterator
    rbegin()
    throw ();

    /**
     * Returns an iterator that points just beyond the last element
     * in a reversed string.
     */
    const_reverse_iterator
    rend() const
    throw ();

    reverse_iterator
    rend()
    throw ();

    /**
     * Copies a substring of at most some number of characters from
     * a string beginning from a specified position.
     * @param pos index of first character to store into basic_string.
     * @param count number of elements to store in basic_string.
     * @return basic_string object that coverage part of original
     * object.
     */
    basic_string
    substr(size_type pos = 0, size_type count = npos) const
    throw ();

    /**
     * Assigns new range character to be the contents of a stored
     * BufferString.
     * @param ptr source char sequence to save start position.
     * @param count number of elements in BufferString.
     * @return reference on self
     */
    basic_string&
    assign(pointer ptr, size_type count)
    throw ();

    /**
     * Assigns new range character to be the contents of a stored
     * BufferString.
     * @param begin pointer to the beginning of the string
     * @param end pointer to the element beyond the last of that string
     * @return reference on self
     */
    basic_string&
    assign(pointer begin, pointer end)
    throw ();

    /**
     * Assigns new range character to be the contents of a stored
     * BufferString. Get BufferString from existing BufferString
     * @param str BufferString source BufferString
     * @param pos index of first character to store into BufferString
     * @param count number of elements to store in BufferString.
     * @return reference on self
     */
    basic_string&
    assign(const basic_string& str, size_type pos, size_type count)
    throw ();

    /**
     * Assigns BufferString from source BufferString
     * @param str source BufferString
     * @return reference on self
     */
    basic_string&
    assign(const basic_string& str)
    throw ();

    /**
     * Makes BufferString empty.
     */
    void
    clear()
    throw ();

    void
    resize(size_type count)
    throw ()
    {
        length_ = count;
    }

    void
    grow(size_type count)
    throw ()
    {
        length_ += count;
    }

    void
    grow_front(size_type count)
    throw ()
    {
        begin_ -= count;
        length_ += count;
    }

    void
    shrink(size_type count)
    throw ()
    {
        // count = std::min(count, length_);
        length_ -= count;
    }

    void
    shrink_front(size_type count)
    {
        // count = std::min(count, length_);
        begin_ += count;
        length_ -= count;
    }

    /**
     * Removes a count number of elements in a BufferString from front.
     * Actually moves the beginning of the substring forward on the specified
     * amount of elements leaving the end pointer intact.
     * @param count number of elements to be removed. npos value means
     * remove all.
     * @return reference on self.
     */
    basic_string&
    erase_front(size_type count = npos)
    throw ();

    /**
     * Removes a count number of elements in a BufferString from back.
     * Actually moves the end of the substring backward on the specified
     * amount of elements leaving the beginning pointer intact.
     * @param count number of elements to be removed. npos value means
     * remove all.
     * @return reference on self.
     */
    basic_string&
    erase_back(size_type count = npos)
    throw ();

    /**
     * Exchange the contents of two strings.
     * @param right object to exchange with *this.
     */
    void
    swap(basic_string& right)
    throw ();

    // Operators
    /**
     * Assign this BufferString to point on right std::basic_string<...>
     * content.
     * @param str source std::basic_string<...>
     * @return reference on self
     */
    template <typename BasicStringTraits, typename Allocator>
    basic_string&
    operator =(const std::basic_string<noconst_value_type,
                                       BasicStringTraits, Allocator>& str) throw ();

    /**
     * Provides a const reference to the character with a specified
     * index in a BufferString.
     * @param pos specified index in a BufferString.
     * @return const reference to BufferString character.
     */
    const_reference
    operator [](size_type pos) const
    throw ();

    /**
     * Provides a reference to the character with a specified
     * index in a BufferString.
     * @param pos specified index in a BufferString.
     * @return reference to BufferString character.
     */
    reference
    operator [](size_type pos)
    throw ();

    /**
     * @return std_basic_string (i.e. std::basic_string<noconst_value_type>)
     * object created on range
     */
    std_basic_string
    str() const
    throw ();

    /**
     * Assigns itself to std::string
     * @param str string to assign to
     */
    template <typename BasicStringTraits, typename Allocator>
    void
    assign_to(std::basic_string<noconst_value_type, BasicStringTraits,
                                Allocator>& str) const
    throw ();

    /**
     * Append itself to the end of std::string
     * @param str string to append to
     */
    template <typename BasicStringTraits, typename Allocator>
    void
    append_to(std::basic_string<noconst_value_type, BasicStringTraits,
                                Allocator>& str) const
    throw ();

private:
    /**
     * @param pos specified index of begin sequence in a BufferString.
     * @param count number of elements that asked for availability.
     * @return number of elements that available in string
     * with this pos and count.
     */
    size_type
    get_available_length_(size_type pos, size_type count) const
    throw ();

    /**
     * Check awareness and shift begin pointer
     * @param position request shift in elements.
     * @param error_func used to create exception if incorrect
     * position requested.
     * @return shifted pointer on position
     */
    pointer
    begin_plus_position_(size_type position) const throw();

    pointer begin_;
    size_type length_;
};

/**
 * Auxiliary names and definitions for the implementation aims
 */
namespace Helper
{
// This names of incomplete classes will shown in compile errors
class ComparanceWithZeroPointerIsProhibited;
class UseDefaultConstructorToCreateEmptySubString;

/**
 * Short easy to use synonym of type
 * ComparanceWithZeroPointerIsProhibited
 */
ComparanceWithZeroPointerIsProhibited
pointers_case()
throw ();

/**
 * Short easy to use synonym of type
 * UseDefaultConstructorToCreateEmptySubString
 */
UseDefaultConstructorToCreateEmptySubString
constructor_case()
throw ();
}

/**
 * Output the range to an ostream. Elements are outputted
 * in a sequence without separators.
 * @param ostr basic_ostream to out value of substr
 * @param substr BufferString to be output
 * @return ostr.
 */

template <typename CharT, typename Traits>
std::basic_ostream<typename basic_string<CharT, Traits>::
    noconst_value_type>&
operator <<(std::basic_ostream<
                typename basic_string<CharT, Traits>::
                noconst_value_type>& ostr,
            const basic_string<CharT, Traits>& substr)
throw ();

///////////////////////////////////////////////////////////////////////
//Additional operators and methods
/**
 * Comparison on equality
 * @param substr BufferString to be compared
 * @param str zero terminated C-string to compare its value
 * with BufferString
 * @return true if equal values, false if not.
 */
template <typename CharT, typename Traits>
bool
operator ==(const basic_string<CharT, Traits>& substr,
            typename basic_string<CharT, Traits>::const_pointer str)
throw (typename basic_string<CharT, Traits>::LogicError);

/**
 * Comparison on equality
 * @param str zero terminated C-string to compare its value
 * with BufferString
 * @param substr BufferString to be compared
 * @return true if equal values, false if not.
 */
template <typename CharT, typename Traits>
bool
operator ==(typename basic_string<CharT, Traits>::
            const_pointer str, const basic_string<CharT, Traits>&
            substr)
throw (typename basic_string<CharT, Traits>::LogicError);

/**
 * Comparison on equality with other BufferString. Compare
 * memory entities.
 * @param left_substr left side of equality expression
 * @param right_substr BufferString to compare with this
 * @return true if equal values, false if not.
 */
template <typename CharT, typename Traits>
bool
operator ==(const basic_string<CharT, Traits>& left_substr,
            const basic_string<CharT, Traits>& right_substr)
throw ();

/**
 * Comparison on equality with std::basic_string<...>. Compare
 * memory entities.
 * @param substr BufferString to be compared
 * @param str std_basic_string to compare with this
 * @return true if equal values, false if not.
 */
template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator ==(const basic_string<CharT, Traits>& substr,
            const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str)
throw ();

/**
 * Comparison on equality with std::basic_string<...>. Compare
 * memory entities.
 * @param str std_basic_string to compare with this
 * @param substr BufferString to be compared
 * @return true if equal values, false if not.
 */
template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator ==(const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str,
            const basic_string<CharT, Traits>& substr)
throw ();

/**
 * The operator is defined to avoid error BufferString a; if (0==a){}
 * Now this code doesn't compile
 */
template <typename CharT, typename Traits>
bool
operator ==(int, const buffer::basic_string<CharT, Traits>&)
throw ();

/**
 * The operator is defined to avoid error BufferString a; if (a==0){}
 * Now this code doesn't compile
 */
template <typename CharT, typename Traits>
bool
operator ==(const buffer::basic_string<CharT, Traits>&, int)
throw ();

/**
 * Comparison on inequality
 * @param substr BufferString to be compared
 * @param str zero terminated C-string to compare its value
 * with BufferString
 * @return false if equal values, true if not.
 */
template <typename CharT, typename Traits>
bool
operator !=(const basic_string<CharT, Traits>& substr,
            typename basic_string<CharT, Traits>::const_pointer str)
throw (typename basic_string<CharT, Traits>::LogicError);

/**
 * Comparison on inequality
 * @param str zero terminated C-string to compare its value
 * with BufferString
 * @param substr BufferString to be compared
 * @return false if equal values, true if not.
 */
template <typename CharT, typename Traits>
bool
operator !=(typename basic_string<CharT, Traits>::
            const_pointer str, const basic_string<CharT, Traits>&
            substr)
throw (typename basic_string<CharT, Traits>::LogicError);

/**
 * Comparison two BufferString on inequality. Compare
 * memory entities.
 * @param left_substr left side of expression
 * @param right_substr BufferString to compare with this
 * @return false if equal values, true if not.
 */
template <typename CharT, typename Traits>
bool
operator !=(const basic_string<CharT, Traits>& left_substr,
            const basic_string<CharT, Traits>& right_substr)
throw ();

/**
 * Comparison on inequality with std::basic_string<...>. Compare
 * memory entities.
 * @param substr BufferString to be compared
 * @param str std_basic_string to compare with this
 * @return false if equal values, true if not.
 */
template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator !=(const basic_string<CharT, Traits>& substr,
            const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str)
throw ();

/**
 * Comparison on inequality with std::basic_string<...>. Compare
 * memory entities.
 * @param str std_basic_string to compare with this
 * @param substr BufferString to be compared
 * @return false if equal values, true if not.
 */
template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator !=(const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str,
            const basic_string<CharT, Traits>& substr)
throw ();

/**
 * The operator is defined to avoid error BufferString a; if (0!=a){}
 * Now this code doesn't compile
 */
template <typename CharT, typename Traits>
bool
operator !=(int, const buffer::basic_string<CharT, Traits>&)
throw ();

/**
 * The operator is defined to avoid error BufferString a; if (a!=0){}
 * Now this code doesn't compile
 */
template <typename CharT, typename Traits>
bool
operator !=(const buffer::basic_string<CharT, Traits>&, int)
throw ();

/**
 * Comparison on less
 * @param substr BufferString to be compared
 * @param str zero terminated C-string to compare its value
 * with BufferString
 * @return false if substr >= str, else true
 */
template <typename CharT, typename Traits>
bool
operator <(const basic_string<CharT, Traits>& substr,
           typename basic_string<CharT, Traits>::const_pointer str)
throw (typename basic_string<CharT, Traits>::LogicError);

/**
 * Comparison on less
 * @param str zero terminated C-string to compare its value
 * with BufferString
 * @param substr BufferString to be compared
 * @return false if substr >= str, else true
 */
template <typename CharT, typename Traits>
bool
operator <(typename basic_string<CharT, Traits>::
           const_pointer str, const basic_string<CharT, Traits>&
           substr)
throw (typename basic_string<CharT, Traits>::LogicError);

/**
 * Comparison two BufferString on less. Compare
 * memory entities.
 * @param left_substr left side of expression
 * @param right_substr BufferString to compare with this
 * @return false if left_substr >= right_substr, else true
 */
template <typename CharT, typename Traits>
bool
operator <(const basic_string<CharT, Traits>& left_substr,
           const basic_string<CharT, Traits>& right_substr) throw ();

/**
 * Comparison on less
 * @param substr BufferString to be compared
 * @param str std::string to compare with BufferString
 * @return false if substr >= str, else true
 */
template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator <(const basic_string<CharT, Traits>& substr,
           const std::basic_string<
               typename basic_string<CharT, Traits>::
               noconst_value_type, BasicStringTraits, Allocator>& str)
throw ();

/**
 * Comparison on less
 * @param str std::string to compare with BufferString
 * @param substr BufferString to be compared
 * @return false if str >= substr, else true
 */
template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator <(const std::basic_string<
               typename basic_string<CharT, Traits>::
               noconst_value_type, BasicStringTraits, Allocator>& str,
           const basic_string<CharT, Traits>& substr)
throw ();

/**
 * The operator is defined to avoid error BufferString a; if (0<a){}
 * Now this code doesn't compile
 */
template <typename CharT, typename Traits>
bool
operator <(int, const buffer::basic_string<CharT, Traits>&)
throw ();

/**
 * The operator is defined to avoid error BufferString a; if (a<0){}
 * Now this code doesn't compile
 */
template <typename CharT, typename Traits>
bool
operator <(const buffer::basic_string<CharT, Traits>&, int)
throw ();

typedef basic_string<const char, CharTraits<char>>
string;
typedef basic_string<const char, ci_char_traits<char>> istring;

typedef basic_string<const wchar_t, CharTraits<wchar_t>> wstring;

typedef basic_string<const char, ci_char_traits<wchar_t>> iwstring;

template <typename Hash,
          typename CharT, typename Traits>
void
hash_add(Hash& hash,
         const basic_string<CharT, Traits>& value) throw ();

/**
 * Adapter for functions that can take BufferString, std::string and
 * const char*. Use with care.
 */
template <typename CharT,
          typename Traits = std::char_traits<
              typename std::remove_const<CharT>::type>>
class BasicSubStringAdapter :
    public basic_string<CharT, Traits>
{
public:
    typedef basic_string<CharT, Traits> string;

    /**
     * Constructor from BufferString
     * @param substr substring
     */
    BasicSubStringAdapter(const string& substr)
    throw () : string(substr)
    { }

    /**
     * Constructor from pointer
     * @param ptr pointer to zero terminated string
     */
    BasicSubStringAdapter(typename string::pointer ptr)
    throw () : string(ptr)
    { }

    /**
     * Constructor from std::basic_string<...> (std::string, std::wstring
     * for example).
     * @param str The input string for coverage
     */
    template <typename BasicStringTraits, typename Allocator>
    BasicSubStringAdapter(
        const std::basic_string<typename string::BasicStringValueType,
                                BasicStringTraits, Allocator>& str) throw () : string(str)
    { }
};

typedef BasicSubStringAdapter<const char, CharTraits<char>> SubStringAdapter;
typedef BasicSubStringAdapter<const wchar_t, CharTraits<wchar_t>> WSubStringAdapter;
}

// @file String/BufferString.tpp

#include <algorithm>


namespace buffer
{
//
// CharTraits class
//

template <typename CharT>
int
CharTraits<CharT>::compare(const CharT* str1, const CharT* str2,
                           size_t size)
throw ()
{
    for (register const CharT* END = str1 + size; str1 != END;
         str1++ , str2++) {
        if (!std::char_traits<CharT>::eq(*str1, *str2)) {
            return std::char_traits<CharT>::lt(*str1, *str2) ? -1 : 1;
        }
    }
    return 0;
}

template <typename CharT>
CharT*
CharTraits<CharT>::copy(CharT* str1, const CharT* str2,
                        size_t size)
throw ()
{
    if (size == 1) {
        std::char_traits<CharT>::assign(*str1, *str2);
    } else {
        std::char_traits<CharT>::copy(str1, str2, size);
    }
    return str1;
}

//
// basic_string class
//

// begin_ + pos
template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::pointer
basic_string<CharT, Traits>::begin_plus_position_(
    size_type position) const
throw ()
{
    return begin_ + position;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::get_available_length_(
    size_type pos, size_type count) const
throw ()
{
    return std::min(count, length_ - pos);
}

//
// Public methods


template <typename CharT, typename Traits>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::assign(pointer ptr,
                                    size_type count)
throw ()
{
    begin_ = ptr;
    length_ = count;
    return *this;
}

template <typename CharT, typename Traits>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::assign(pointer begin,
                                    pointer end)
throw ()
{
    begin_ = begin;
    length_ = end - begin;
    return *this;
}

template <typename CharT, typename Traits>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::assign(
    const basic_string& str, size_type pos, size_type count)
throw ()
{
    begin_ = str.begin_plus_position_(pos);
    length_ = str.get_available_length_(pos, count);
    return *this;
}

template <typename CharT, typename Traits>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::assign(
    const basic_string& str)
throw ()
{
    begin_ = str.begin_;
    length_ = str.length_;
    return *this;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_reference
basic_string<CharT, Traits>::at(size_type pos) const
throw ()
{
    return *begin_plus_position_(pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::reference
basic_string<CharT, Traits>::at(size_type pos)
throw ()
{
    return *begin_plus_position_(pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_pointer
basic_string<CharT, Traits>::begin() const throw ()
{
    return begin_;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::pointer
basic_string<CharT, Traits>::begin() throw ()
{
    return begin_;
}

template <typename CharT, typename Traits>
void
basic_string<CharT, Traits>::clear() throw ()
{
    begin_ = 0;
    length_ = 0;
}

template <typename CharT, typename Traits>
basic_string<CharT, Traits>
basic_string<CharT, Traits>::substr(size_type pos,
                                    size_type count) const
throw ()
{
    return basic_string<CharT, Traits>(
        begin_plus_position_(pos),
        get_available_length_(pos, count));
}

template <typename CharT, typename Traits>
int
basic_string<CharT, Traits>::compare(
    const basic_string<CharT, Traits>& str) const
throw ()
{
    const size_type LEN = std::min(length_, str.length_);
    if (const int RESULT = Traits::compare(begin_, str.begin_, LEN)) {
        return RESULT;
    }
    return length_ == str.length_ ? 0 : length_ < str.length_ ? -1 : 1;
}

template <typename CharT, typename Traits>
int
basic_string<CharT, Traits>::compare(size_type pos1,
                                     size_type count1, const basic_string& str) const
throw ()
{
    return substr(pos1, count1).compare(str);
}

template <typename CharT, typename Traits>
int
basic_string<CharT, Traits>::compare(size_type pos1,
                                     size_type count1, const basic_string& str, size_type pos2,
                                     size_type count2) const
throw ()
{
    return substr(pos1, count1).compare(str.substr(pos2, count2));
}

template <typename CharT, typename Traits>
int
basic_string<CharT, Traits>::compare(
    const_pointer str) const
throw ()
{
    const CharT NUL(0);
    register pointer ptr = begin_;
    register const_pointer END = ptr + length_;
    while (ptr != END) {
        const CharT CH(*str++);
        if (Traits::eq(CH, NUL)) {
            return -1;
        }
        const CharT CH2(*ptr++);
        if (!Traits::eq(CH2, CH)) {
            return Traits::lt(CH2, CH) ? -1 : 1;
        }
    }
    return Traits::eq(*str, NUL) ? 0 : -1;
}

template <typename CharT, typename Traits>
int
basic_string<CharT, Traits>::compare(size_type pos1,
                                     size_type count1, const_pointer ptr) const
throw ()
{
    return substr(pos1, count1).compare(ptr);
}

template <typename CharT, typename Traits>
int
basic_string<CharT, Traits>::compare(size_type pos1,
                                     size_type count1, const_pointer ptr, size_type count2) const
throw ()
{
    return substr(pos1, count1).compare(
        basic_string(const_cast<pointer>(ptr), count2));
}

template <typename CharT, typename Traits>
bool
basic_string<CharT, Traits>::equal(const_pointer str) const
throw ()
{
    const CharT NUL(0);
    register pointer ptr = begin_;
    register const const_pointer END = ptr + length_;
    while (ptr != END) {
        const CharT CH(*str++);
        if (Traits::eq(CH, NUL) || !Traits::eq(*ptr++, CH)) {
            return false;
        }
    }
    return Traits::eq(*str, NUL);
}

template <typename CharT, typename Traits>
bool
basic_string<CharT, Traits>::equal(
    const basic_string& str) const
throw ()
{
    if (str.length_ != length_) {
        return false;
    }
    return !Traits::compare(begin_, str.begin_, length_);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::copy(noconst_value_type* ptr,
                                  size_type count, size_type pos) const
throw ()
{
    count = get_available_length_(pos, count);

    if (!count) {
        return 0;
    }

    Traits::copy(ptr, begin_ + pos, count);
    return count;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_pointer
basic_string<CharT, Traits>::data() const
throw ()
{
    return begin_;
}

template <typename CharT, typename Traits>
bool
basic_string<CharT, Traits>::empty() const
throw ()
{
    return !length_;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_pointer
basic_string<CharT, Traits>::end() const
throw ()
{
    return begin_ + length_;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::pointer
basic_string<CharT, Traits>::end()
throw ()
{
    return begin_ + length_;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_reverse_iterator
basic_string<CharT, Traits>::rbegin() const
throw ()
{
    return const_reverse_iterator(begin_ + length_);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::reverse_iterator
basic_string<CharT, Traits>::rbegin()
throw ()
{
    return reverse_iterator(begin_ + length_);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_reverse_iterator
basic_string<CharT, Traits>::rend() const
throw ()
{
    return const_reverse_iterator(begin_);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::reverse_iterator
basic_string<CharT, Traits>::rend()
throw ()
{
    return reverse_iterator(begin_);
}

template <typename CharT, typename Traits>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::erase_front(size_type count)
throw ()
{
    if (begin_) {
        count = std::min(count, length_);
        begin_ += count;
        length_ -= count;
    }
    return *this;
}

template <typename CharT, typename Traits>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::erase_back(size_type count)
throw ()
{
    if (begin_) {
        length_ -= std::min(count, length_);
    }
    return *this;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::length() const
throw ()
{
    return length_;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::max_size() const
throw ()
{
    return length_;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::size() const
throw ()
{
    return length_;
}

template <typename CharT, typename Traits>
void
basic_string<CharT, Traits>::swap(
    basic_string& right) throw ()
{
    std::swap(begin_, right.begin_);
    std::swap(length_, right.length_);
}

template <typename CharT, typename Traits>
template <typename BasicStringTraits, typename Allocator>
basic_string<CharT, Traits>&
basic_string<CharT, Traits>::operator =(
    const std::basic_string<noconst_value_type, BasicStringTraits,
                            Allocator>& str) throw ()
{
    begin_ = str.data();
    length_ = str.size();
    return *this;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::const_reference
basic_string<CharT, Traits>::operator [](size_type pos) const
throw ()
{
    return *begin_plus_position_(pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::reference
basic_string<CharT, Traits>::operator [](size_type pos)
throw ()
{
    return *begin_plus_position_(pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::std_basic_string
basic_string<CharT, Traits>::str() const
throw ()
{
    return std_basic_string(begin_, length_);
}

template <typename CharT, typename Traits>
template <typename BasicStringTraits, typename Allocator>
void
basic_string<CharT, Traits>::assign_to(
    std::basic_string<noconst_value_type, BasicStringTraits,
                      Allocator>& str) const
throw ()
{
    str.assign(begin_, length_);
}

template <typename CharT, typename Traits>
template <typename BasicStringTraits, typename Allocator>
void
basic_string<CharT, Traits>::append_to(
    std::basic_string<noconst_value_type, BasicStringTraits,
                      Allocator>& str) const
throw ()
{
    str.append(begin_, length_);
}
}

namespace buffer
{
//
// External functions
//

template <typename CharT, typename Traits>
std::basic_ostream<typename basic_string<CharT, Traits>::
    noconst_value_type>&
operator <<(std::basic_ostream<
                typename basic_string<CharT, Traits>::
                noconst_value_type>& ostr,
            const basic_string<CharT, Traits>& substr)
throw ()
{
    if (!substr.empty()) {
        ostr.write(substr.data(), substr.length());
    }
    return ostr;
}

template <typename CharT, typename Traits>
bool
operator ==(const basic_string<CharT, Traits>& substr,
            typename basic_string<CharT, Traits>::const_pointer str)
throw (typename basic_string<CharT, Traits>::LogicError)
{
    return substr.equal(str);
}

template <typename CharT, typename Traits>
bool
operator ==(
    typename basic_string<CharT, Traits>::const_pointer str,
    const basic_string<CharT, Traits>& substr)
throw (typename basic_string<CharT, Traits>::LogicError)
{
    return substr.equal(str);
}

template <typename CharT, typename Traits>
bool
operator ==(const basic_string<CharT, Traits>& left_substr,
            const basic_string<CharT, Traits>& right_substr)
throw ()
{
    return left_substr.equal(right_substr);
}

template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator ==(const basic_string<CharT, Traits>& substr,
            const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str)
throw ()
{
    return substr.equal(str);
}

template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator ==(const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str,
            const basic_string<CharT, Traits>& substr)
throw ()
{
    return substr.equal(str);
}

template <typename CharT, typename Traits>
bool
operator !=(const basic_string<CharT, Traits>& substr,
            typename basic_string<CharT, Traits>::const_pointer str)
throw (typename basic_string<CharT, Traits>::LogicError)
{
    return !substr.equal(str);
}

template <typename CharT, typename Traits>
bool
operator !=(
    typename basic_string<CharT, Traits>::const_pointer str,
    const basic_string<CharT, Traits>& substr)
throw (typename basic_string<CharT, Traits>::LogicError)
{
    return !substr.equal(str);
}

template <typename CharT, typename Traits>
bool
operator !=(const basic_string<CharT, Traits>& left_substr,
            const basic_string<CharT, Traits>& right_substr)
throw ()
{
    return !left_substr.equal(right_substr);
}

template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator !=(const basic_string<CharT, Traits>& substr,
            const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str)
throw ()
{
    return !substr.equal(str);
}

template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator !=(const std::basic_string<
                typename basic_string<CharT, Traits>::
                noconst_value_type, BasicStringTraits, Allocator>& str,
            const basic_string<CharT, Traits>& substr)
throw ()
{
    return !substr.equal(str);
}

template <typename CharT, typename Traits>
bool
operator <(const basic_string<CharT, Traits>& substr,
           typename basic_string<CharT, Traits>::const_pointer str)
throw (typename basic_string<CharT, Traits>::LogicError)
{
    return substr.compare(str) < 0;
}

template <typename CharT, typename Traits>
bool
operator <(
    typename basic_string<CharT, Traits>::const_pointer str,
    const basic_string<CharT, Traits>& substr)
throw (typename basic_string<CharT, Traits>::LogicError)
{
    return !operator <(substr, str);
}

template <typename CharT, typename Traits>
bool
operator <(const basic_string<CharT, Traits>& left_substr,
           const basic_string<CharT, Traits>& right_substr) throw ()
{
    return left_substr.compare(right_substr) < 0;
}

template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator <(const basic_string<CharT, Traits>& substr,
           const std::basic_string<
               typename basic_string<CharT, Traits>::
               noconst_value_type, BasicStringTraits, Allocator>& str)
throw ()
{
    return substr.compare(str) < 0;
}

template <typename CharT, typename Traits,
          typename BasicStringTraits, typename Allocator>
bool
operator <(const std::basic_string<
               typename basic_string<CharT, Traits>::
               noconst_value_type, BasicStringTraits, Allocator>& str,
           const basic_string<CharT, Traits>& substr)
throw ()
{
    return substr.compare(str) > 0;
}

template <typename Hash,
          typename CharT, typename Traits>
void
hash_add(Hash& hash,
         const basic_string<CharT, Traits>& value) throw ()
{
    hash.add(value.data(), value.size());
}
}

// @file String/SubStringFind.tpp

namespace buffer
{
//
// Find char forward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find(value_type ch,
                                  size_type pos) const
throw ()
{
    if (pos < length_) {
        const_pointer ptr = Traits::find(begin_ + pos, length_ - pos, ch);
        if (ptr) {
            return ptr - begin_;
        }
    }
    return npos;
}

//
// Find substring forward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find(const basic_string& str,
                                  size_type pos) const
throw ()
{
    if (!str.length_) {
        return pos >= length_ ? npos : 0;
    }

    size_type length = get_available_length_(pos, length_);
    if (str.length_ > length) {
        return npos;
    }

    const const_pointer END = begin_ + length_;
    const const_pointer FOUND = std::search(begin_ + pos, END,
                                            str.begin_, str.begin_ + str.length_, Traits::eq);
    return FOUND != END ? FOUND - begin_ : npos;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find(const_pointer ptr,
                                  size_type pos) const
throw ()
{
    return find(basic_string<CharT, Traits>(ptr), pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find(const_pointer ptr,
                                  size_type pos, size_type count) const
throw ()
{
    return find(basic_string<CharT, Traits>(ptr, count), pos);
}

//
// Find char backward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::rfind(value_type ch,
                                   size_type pos) const
throw ()
{
    if (length_) {
        const_pointer last = begin_ + std::min(length_ - 1, pos);
        for (;; --last) {
            if (Traits::eq(*last, ch)) {
                return last - begin_;
            }
            if (last == begin_) {
                return npos;
            }
        }
    }
    return npos;
}

//
// Find substring backward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::rfind(const basic_string& str,
                                   size_type pos) const
throw ()
{
    if (str.length_ > length_) {
        return npos;
    }

    if (pos > length_) {
        pos = length_;
    }

    if (!str.length_) {
        return pos;
    }

    if (pos > length_ - str.length_) {
        pos = length_ - str.length_;
    }

    for (const_pointer data = begin_ + pos; ; data--) {
        if (!Traits::compare(data, str.begin_, str.length_)) {
            return data - begin_;
        }
        if (data == begin_) {
            break;
        }
    }

    return npos;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::rfind(const_pointer ptr,
                                   size_type pos) const
throw ()
{
    return rfind(basic_string<CharT, Traits>(ptr), pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::rfind(const_pointer ptr,
                                   size_type pos, size_type count) const
throw ()
{
    return rfind(basic_string<CharT, Traits>(ptr), pos);
}

//
// Find char forward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_of(value_type ch,
                                           size_type pos) const
throw ()
{
    return find(ch, pos);
}

//
// Find chars forward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_of(
    const basic_string& str, size_type pos) const
throw ()
{
    for (; pos < length_; pos++) {
        if (Traits::find(str.begin_, str.length_, begin_[pos])) {
            return pos;
        }
    }
    return npos;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_of(
    const_pointer ptr, size_type pos) const
throw ()
{
    return find_first_of(basic_string<CharT, Traits>(ptr),
                         pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_of(
    const_pointer ptr, size_type pos, size_type count) const
throw ()
{
    return find_first_of(
        basic_string<CharT, Traits>(ptr, count), pos);
}

//
// Find not char forward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_not_of(value_type ch,
                                               size_type pos) const
throw ()
{
    for (; pos < length_; pos++) {
        if (!Traits::eq(begin_[pos], ch)) {
            return pos;
        }
    }
    return npos;
}

//
// Find not chars forward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_not_of(
    const basic_string& str, size_type pos) const
throw ()
{
    for (; pos < length_; pos++) {
        if (!Traits::find(str.begin_, str.length_, begin_[pos])) {
            return pos;
        }
    }
    return npos;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_not_of(
    const_pointer ptr, size_type pos) const
throw ()
{
    return find_first_not_of(basic_string<CharT, Traits>(ptr),
                             pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_first_not_of(
    const_pointer ptr, size_type pos, size_type count) const
throw ()
{
    return find_first_not_of(
        basic_string<CharT, Traits>(ptr, count), pos);
}

//
// Find char backward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_of(value_type ch,
                                          size_type pos) const
throw ()
{
    return rfind(ch, pos);
}

//
// Find chars backward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_of(
    const basic_string& str, size_type pos) const
throw ()
{
    if (length_ && str.length_) {
        if (pos >= length_) {
            pos--;
        }
        do {
            if (Traits::find(str.begin_, str.length_, begin_[pos])) {
                return pos;
            }
        } while (pos--);
    }
    return npos;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_of(
    const_pointer ptr, size_type pos) const
throw ()
{
    return find_last_of(basic_string<CharT, Traits>(ptr),
                        pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_of(
    const_pointer ptr, size_type pos, size_type count) const
throw ()
{
    return find_last_of(
        basic_string<CharT, Traits>(ptr, count), pos);
}

//
// Find not chars backward
//

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_not_of(
    const basic_string& str, size_type pos) const
throw ()
{
    if (length_ && str.length_) {
        if (pos >= length_) {
            pos--;
        }
        do {
            if (!Traits::find(str.begin_, str.length_, begin_[pos])) {
                return pos;
            }
        } while (pos--);
    }
    return npos;
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_not_of(
    const_pointer ptr, size_type pos) const
throw ()
{
    return find_last_not_of(basic_string<CharT, Traits>(ptr),
                            pos);
}

template <typename CharT, typename Traits>
typename basic_string<CharT, Traits>::size_type
basic_string<CharT, Traits>::find_last_not_of(
    const_pointer ptr, size_type pos, size_type count) const
throw ()
{
    return find_last_not_of(
        basic_string<CharT, Traits>(ptr, count), pos);
}


inline long stol(const string& str, size_t *idx = 0, int base = 10)
{	// convert string to int
    register const char *s = str.begin();
    register const char *end = str.end();
    register unsigned long acc;
    register int c;
    register unsigned long cutoff;
    register int neg = 0, any, cutlim;

    errno = 0;

    if (s == end) {
        errno = EINVAL;
        return 0;
    }

    c = *s;

    if (c == '-') {
        neg = 1;
        if (++s == end) {
            errno = EINVAL;
            return 0;
        }
    } else if (c == '+') {
        if (++s == end) {
            errno = EINVAL;
            return 0;
        }       
    }

    /*
     * Compute the cutoff value between legal numbers and illegal
     * numbers.  That is the largest legal value, divided by the
     * base.  An input number that is greater than this value, if
     * followed by a legal input character, is too big.  One that
     * is equal to this value may be valid or not; the limit
     * between valid and invalid numbers is then based on the last
     * digit.  For instance, if the range for longs is
     * [-2147483648..2147483647] and the input base is 10,
     * cutoff will be set to 214748364 and cutlim to either
     * 7 (neg==0) or 8 (neg==1), meaning that if we have accumulated
     * a value > 214748364, or equal but the next digit is > 7 (or 8),
     * the number is too big, and we will return a range error.
     *
     * Set any if any `digits' consumed; make it negative to indicate
     * overflow.
     */
    static const unsigned long cutoff_neg_10 = -(unsigned long)LONG_MIN / 10;
    static const unsigned long cutoff_10 = LONG_MAX / 10;
    static const int cutlim_neg_10 = -(unsigned long) LONG_MIN % 10;
    static const int cutlim_10 = LONG_MAX % 10;
    static const unsigned long cutoff_neg_16 = -(unsigned long) LONG_MIN / 16;
    static const unsigned long cutoff_16 = LONG_MAX / 16;
    static const int cutlim_neg_16 = -(unsigned long) LONG_MIN % 16;
    static const int cutlim_16 = LONG_MAX % 16;

    switch(base) {
    case 10:
        if (neg) {
            cutoff = cutoff_neg_10;
            cutlim = cutlim_neg_10;
        } else {
            cutoff = cutoff_10;
            cutlim = cutlim_10;
        }
        break;
    case 16:
        if (neg) {
            cutoff = cutoff_neg_16;
            cutlim = cutlim_neg_16;
        } else {
            cutoff = cutoff_16;
            cutlim = cutlim_16;
        }
        break;
    default:
        cutoff = neg ? -(unsigned long) LONG_MIN : LONG_MAX;
        cutlim = cutoff % (unsigned long) base;
        cutoff /= (unsigned long) base;
    }

    for (acc = 0, any = 0; s < end; s++) {
        c = *s;

        if (isdigit(c))
            c -= '0';
        else if (isalpha(c))
            c -= isupper(c) ? 'A' - 10 : 'a' - 10;
        else
            break;

        if (c >= base)
            break;

        if (acc > cutoff || acc == cutoff && c > cutlim) {
            s++; any = -1;
            break;
        }

        any = 1;
        acc *= base;
        acc += c;
    }

    if (any < 0) {
        acc = neg ? LONG_MIN : LONG_MAX;
        errno = ERANGE;
    } else if (any == 0) {
        errno = EINVAL;
    } else if (neg)
        acc = -acc;

    if (idx != 0)
        *idx = s - str.begin();
    return acc;
}
}


#endif
