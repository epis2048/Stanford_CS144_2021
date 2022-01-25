#include "tcp_sender.hh"

#include "tcp_config.hh"

#include <random>

// Dummy implementation of a TCP sender

// For Lab 3, please replace with a real implementation that passes the
// automated checks run by `make check_lab3`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

//! \param[in] capacity the capacity of the outgoing byte stream
//! \param[in] retx_timeout the initial amount of time to wait before retransmitting the oldest outstanding segment
//! \param[in] fixed_isn the Initial Sequence Number to use, if set (otherwise uses a random ISN)
TCPSender::TCPSender(const size_t capacity, const uint16_t retx_timeout, const std::optional<WrappingInt32> fixed_isn)
    : isn_(fixed_isn.value_or(WrappingInt32{random_device()()}))
    , initial_retransmission_timeout_{retx_timeout}
    , retransmission_timeout_(retx_timeout)
    , stream_(capacity) {}

uint64_t TCPSender::bytes_in_flight() const { return bytes_in_flight_; }

void TCPSender::fill_window() {
    TCPSegment tcp_segment;
    // 如果发送完了
    if (fin_) {
        return;
    }
    // 如果还没有开始
    if (!syn_) {
        tcp_segment.header().syn = true;
        send_tcp_segment(tcp_segment);
        syn_ = true;
        return;
    }
    // 窗口大小，未知则假设为1
    uint16_t window_size = (receiver_window_size_ > 0 ? receiver_window_size_ : 1);
    // 发送结束，单独返回一个FIN包
    if (stream_.eof() && recv_seqno_ + window_size > next_seqno_) {
        tcp_segment.header().fin = true;
        send_tcp_segment(tcp_segment);
        fin_ = true;
        return;
    }
    // 循环发送，直到无新的字节需要读取或者无可用空间
    while (!stream_.buffer_empty() && recv_seqno_ + window_size > next_seqno_) {
        // 根据大小来读取数据
        size_t send_size =
            min(TCPConfig::MAX_PAYLOAD_SIZE, static_cast<size_t>(window_size - (next_seqno_ - recv_seqno_)));
        tcp_segment.payload() = stream_.read(min(send_size, stream_.buffer_size()));
        // 如果发送完了，则添加FIN标志
        if (stream_.eof() && tcp_segment.length_in_sequence_space() < window_size) {
            tcp_segment.header().fin = true;
            fin_ = true;
        }
        // 发送
        send_tcp_segment(tcp_segment);
    }
}

void TCPSender::send_tcp_segment(TCPSegment &tcp_segment) {
    // 设置序号
    tcp_segment.header().seqno = wrap(next_seqno_, isn_);
    // 放入队列中
    segments_out_.push(tcp_segment);
    segments_wait_.push(tcp_segment);
    // 本地保存发送的数据大小
    next_seqno_ += tcp_segment.length_in_sequence_space();
    bytes_in_flight_ += tcp_segment.length_in_sequence_space();
    // 启动重传计时器
    if (!retransmissions_timer_running_) {
        retransmissions_timer_running_ = true;
        retransmissions_timer_ = 0;
    }
}

//! \param ackno The remote receiver's ackno (acknowledgment number)
//! \param window_size The remote receiver's advertised window size
void TCPSender::ack_received(const WrappingInt32 ackno, const uint16_t window_size) {
    uint64_t abs_ackno = unwrap(ackno, isn_, next_seqno_);
    // 超出了窗口范围
    if (abs_ackno > next_seqno_) {
        return;
    }
    // 设置新的窗口大小
    if (abs_ackno >= recv_seqno_) {
        recv_seqno_ = abs_ackno;
        receiver_window_size_ = window_size;
    }
    // 删除已经确认发送成功的段
    bool pop = false;
    while (!segments_wait_.empty()) {
        TCPSegment tcp_segment = segments_wait_.front();
        // 当前队列头的段还未发送成功
        if (abs_ackno <
            unwrap(tcp_segment.header().seqno, isn_, next_seqno_) + tcp_segment.length_in_sequence_space()) {
            return;
        }
        // 发送成功，要出队列，修改发送的数据大小
        segments_wait_.pop();
        bytes_in_flight_ -= tcp_segment.length_in_sequence_space();
        // 重置重传超时时间、重传次数与重传计时器
        retransmission_timeout_ = initial_retransmission_timeout_;
        consecutive_retransmissions_ = 0;
        retransmissions_timer_ = 0;
        pop = true;
    }
    // 如果有新的空间打开，则尝试填充之
    if (pop) {
        fill_window();
    }
    // 重传计时器是否启动取决于发送方是否有未完成的数据
    retransmissions_timer_running_ = !segments_wait_.empty();
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void TCPSender::tick(const size_t ms_since_last_tick) {
    // 重传计时器是否启动
    if (!retransmissions_timer_running_) {
        return;
    }
    // 记录时间
    retransmissions_timer_ += ms_since_last_tick;
    // 已超时
    if (retransmissions_timer_ >= retransmission_timeout_ && !segments_wait_.empty()) {
        // 重传数据
        TCPSegment tcp_segment = segments_wait_.front();
        segments_out_.push(tcp_segment);
        // 重置计时器
        retransmissions_timer_ = 0;
        // 记录连续重传的数量，并将RTO值翻倍
        if (receiver_window_size_ > 0 || tcp_segment.header().syn) {
            consecutive_retransmissions_++;
            retransmission_timeout_ *= 2;
        }
    }
}

unsigned int TCPSender::consecutive_retransmissions() const { return consecutive_retransmissions_; }

void TCPSender::send_empty_segment() {
    TCPSegment tcp_segment;
    tcp_segment.header().seqno = wrap(next_seqno_, isn_);
    segments_out_.push(tcp_segment);
}
