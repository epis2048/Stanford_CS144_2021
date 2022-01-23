#include "tcp_receiver.hh"

// Dummy implementation of a TCP receiver

// For Lab 2, please replace with a real implementation that passes the
// automated checks run by `make check_lab2`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

void TCPReceiver::segment_received(const TCPSegment &seg) {
    // 未开始
    if (isn_ == nullopt && !seg.header().syn) {
        return;
    }
    // 设置初始序列号
    if (seg.header().syn) {
        isn_ = seg.header().seqno;
    }
    // 获取reassembler的index，即Seq->AboSeq
    int64_t abo_seqno = unwrap(seg.header().seqno + static_cast<int>(seg.header().syn), isn_.value(), checkpoint_);
    // 将任何数据或流结束标记推到StreamReassembler，序号使用Stream Indices，即AboSeq - 1
    reassembler_.push_substring(seg.payload().copy(), abo_seqno - 1, seg.header().fin);
    // 设置新的checkpoint
    checkpoint_ += seg.length_in_sequence_space();
}

optional<WrappingInt32> TCPReceiver::ackno() const {
    // ISN还未设置
    if (isn_ == nullopt)
        return nullopt;
    // Stream Indices->AboSeq
    uint64_t written = stream_out().bytes_written() + 1;
    if (stream_out().input_ended()) {
        written++;
    }
    // AboSeq->Seq
    return wrap(written, isn_.value());
}

size_t TCPReceiver::window_size() const {
    // first_unacceptable - first_unassembled
    return capacity_ - stream_out().buffer_size();
}
