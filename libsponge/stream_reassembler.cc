#include "stream_reassembler.hh"

// Dummy implementation of a stream reassembler.

// For Lab 1, please replace with a real implementation that passes the
// automated checks run by `make check_lab1`.

// You will need to add private members to the class declaration in `stream_reassembler.hh`

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

StreamReassembler::StreamReassembler(const size_t capacity)
    : output_(capacity)
    , capacity_(capacity)
    , unassembled_bytes_(0)
    , datas_(capacity, '\0')
    , write_flag_(capacity)
    , next_index_(0)
    , is_eof_(false) {}

//! \details This function accepts a substring (aka a segment) of bytes,
//! possibly out-of-order, from the logical stream, and assembles any newly
//! contiguous substrings and writes them into the output stream in order.
void StreamReassembler::push_substring(const string &data, const size_t index, const bool eof) {
    if (index >= next_index_ + output_.remaining_capacity()) {
        return;
    }
    // 设置eof标志
    if (eof && index + data.size() <= next_index_ + output_.remaining_capacity()) {
        is_eof_ = true;
    }
    // 如果所有数据都已经保存过了，那就跳过
    if (index + data.size() > next_index_) {
        // 将数据保存到datas_中，从index和next_index_中更大的一方开始，避免无用循环
        for (size_t i = (index > next_index_? index: next_index_); i < next_index_ + output_.remaining_capacity() && i < index + data.size(); i++) {
            if (write_flag_.count(i) == 0) {
                // 动态扩容
                if (datas_.capacity() <= i) {
                    datas_.reserve(i * 2);
                }
                datas_[i] = data[i - index];
                write_flag_.insert(i);
                unassembled_bytes_++;
            }
        }
        // 将已经有序的数据发送到字节流中
        while (write_flag_.count(next_index_) > 0) {
            output_.write(datas_[next_index_]);
            write_flag_.erase(next_index_);
            next_index_++;
            unassembled_bytes_--;
        }
    }
    // 如果输入结束且所有数据都发送了，则结束字节流
    if (is_eof_ && empty()) {
        output_.end_input();
    }
}

size_t StreamReassembler::unassembled_bytes() const { return unassembled_bytes_; }

bool StreamReassembler::empty() const { return unassembled_bytes_ == 0; }
