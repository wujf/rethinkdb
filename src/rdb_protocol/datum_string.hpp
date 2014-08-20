// Copyright 2010-2014 RethinkDB, all rights reserved.
#ifndef RDB_PROTOCOL_DATUM_STRING_HPP_
#define RDB_PROTOCOL_DATUM_STRING_HPP_

#include <string>

#include "containers/archive/archive.hpp"
#include "containers/shared_buffer.hpp"

/* `datum_string_t` is a length-prefixed ("Pascal style") non-mutable string.
 * This has two advantages over C-strings:
 * - it can be efficiently serialized and deserialized
 * - it can contain any character, including '\0'
 *
 * Underneath `datum_string_t` uses a `shared_buf_ref_t`. This makes it
 * relatively cheap to copy.
 */
class datum_string_t {
public:
    // Creates an empty datum_string_t
    datum_string_t();

    // Creates a datum_string_t with its content copied from _data
    datum_string_t(size_t _size, const char *_data);

    // Construct from a C string
    explicit datum_string_t(const char *c_str);

    // Construct from an STD string
    explicit datum_string_t(const std::string &str);

    // Create a datum_string_t from an existing shared_buf_ref_t.
    // It must have the length in varint encoding at the beginning, followed
    // by the string data.
    datum_string_t(const shared_buf_ref_t<char> &_ref);
    datum_string_t(shared_buf_ref_t<char> &&_ref);

    datum_string_t(datum_string_t &&movee) noexcept;
    datum_string_t(const datum_string_t &copyee) noexcept;

    datum_string_t &operator=(const datum_string_t &copyee) noexcept;
    datum_string_t &operator=(datum_string_t &&movee) noexcept;

    // The result of data() is not automatically null terminated. Do not use
    // as a C string.
    const char *data() const;
    size_t size() const;
    bool empty() const;

    int compare(const datum_string_t &other) const;

    // Short cut for comparing to C-strings and STD strings
    bool operator==(const char *other) const;
    bool operator!=(const char *other) const;
    bool operator==(const datum_string_t &other) const;
    bool operator!=(const datum_string_t &other) const;
    bool operator<(const datum_string_t &other) const;
    bool operator>(const datum_string_t &other) const;
    bool operator<=(const datum_string_t &other) const;
    bool operator>=(const datum_string_t &other) const;

    std::string to_std() const;

private:
    // Contains the length of the string in varint encoding, followed by the actual
    // string content.
    shared_buf_ref_t<char> data_;
};


datum_string_t concat(const datum_string_t &a, const datum_string_t &b);

#endif  // RDB_PROTOCOL_DATUM_STRING_HPP_