#include "byte_stream.hh"

// Dummy implementation of a flow-controlled in-memory byte stream.

// For Lab 0, please replace with a real implementation that passes the
// automated checks run by `make check_lab0`.

// You will need to add private members to the class declaration in `byte_stream.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

ByteStream::ByteStream(const size_t capacity) : capacity_(capacity) {}

size_t ByteStream::write(const string &data) {
    DUMMY_CODE(data);
    // 获取数据大小
    size_t writed_size = (remaining_capacity() >= data.size() ? data.size() : remaining_capacity());
    // 保存数据并更新信息
    buffer_ += data.substr(0, writed_size);
    used_ += writed_size;
    total_write_ += writed_size;
    return writed_size;
}

size_t ByteStream::write(const char data) {
    // 获取数据大小
    if (remaining_capacity() <= 0)
        return 0;
    // 保存数据并更新信息
    buffer_ += data;
    used_++;
    total_write_++;
    return 1;
}

//! \param[in] len bytes will be copied from the output side of the buffer
string ByteStream::peek_output(const size_t len) const { return buffer_.substr(0, len); }

//! \param[in] len bytes will be removed from the output side of the buffer
void ByteStream::pop_output(const size_t len) {
    buffer_.erase(0, len);
    total_read_ += len;
    used_ -= len;
}

//! Read (i.e., copy and then pop) the next "len" bytes of the stream
//! \param[in] len bytes will be popped and returned
//! \returns a string
std::string ByteStream::read(const size_t len) {
    // 获取数据大小
    size_t read_size = (len <= used_ ? len : used_);
    // 返回数据
    std::string temp = peek_output(read_size);
    pop_output(read_size);
    return temp;
}

void ByteStream::end_input() { input_end_ = true; }

bool ByteStream::input_ended() const { return input_end_; }

size_t ByteStream::buffer_size() const { return used_; }

bool ByteStream::buffer_empty() const { return (used_ == 0); }

bool ByteStream::eof() const { return input_end_ && used_ == 0; }

size_t ByteStream::bytes_written() const { return total_write_; }

size_t ByteStream::bytes_read() const { return total_read_; }

size_t ByteStream::remaining_capacity() const { return capacity_ - used_; }
