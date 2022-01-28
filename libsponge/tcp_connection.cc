#include "tcp_connection.hh"

#include <iostream>

// Dummy implementation of a TCP connection

// For Lab 4, please replace with a real implementation that passes the
// automated checks run by `make check`.

template <typename... Targs>
void DUMMY_CODE(Targs &&... /* unused */) {}

using namespace std;

size_t TCPConnection::remaining_outbound_capacity() const { return sender_.stream_in().remaining_capacity(); }

size_t TCPConnection::bytes_in_flight() const { return sender_.bytes_in_flight(); }

size_t TCPConnection::unassembled_bytes() const { return receiver_.unassembled_bytes(); }

size_t TCPConnection::time_since_last_segment_received() const { return time_since_last_segment_received_; }

void TCPConnection::segment_received(const TCPSegment &seg) {
    // 非启动时不接收
    if (!active_) {
        return;
    }

    // 重置连接时间
    time_since_last_segment_received_ = 0;

    // RST标志，直接关闭连接
    if (seg.header().rst) {
        // 在出站入站流中标记错误，使active返回false
        receiver_.stream_out().set_error();
        sender_.stream_in().set_error();
        active_ = false;
    }
    // 当前是Closed/Listen状态
    else if (sender_.next_seqno_absolute() == 0) {
        // 收到SYN，说明TCP连接由对方启动，进入Syn-Revd状态
        if (seg.header().syn) {
            // 此时还没有ACK，所以sender不需要ack_received
            receiver_.segment_received(seg);
            // 我们主动发送一个SYN
            connect();
        }
    }
    // 当前是Syn-Sent状态
    else if (sender_.next_seqno_absolute() == sender_.bytes_in_flight() && !receiver_.ackno().has_value()) {
        if (seg.header().syn && seg.header().ack) {
            // 收到SYN和ACK，说明由对方主动开启连接，进入Established状态，通过一个空包来发送ACK
            sender_.ack_received(seg.header().ackno, seg.header().win);
            receiver_.segment_received(seg);
            sender_.send_empty_segment();
            send_data();
        } else if (seg.header().syn && !seg.header().ack) {
            // 只收到了SYN，说明由双方同时开启连接，进入Syn-Rcvd状态，没有接收到对方的ACK，我们主动发一个
            receiver_.segment_received(seg);
            sender_.send_empty_segment();
            send_data();
        }
    }
    // 当前是Syn-Revd状态，并且输入没有结束
    else if (sender_.next_seqno_absolute() == sender_.bytes_in_flight() && receiver_.ackno().has_value() &&
             !receiver_.stream_out().input_ended()) {
        // 接收ACK，进入Established状态
        sender_.ack_received(seg.header().ackno, seg.header().win);
        receiver_.segment_received(seg);
    }
    // 当前是Established状态，连接已建立
    else if (sender_.next_seqno_absolute() > sender_.bytes_in_flight() && !sender_.stream_in().eof()) {
        // 发送数据，如果接到数据，则更新ACK
        sender_.ack_received(seg.header().ackno, seg.header().win);
        receiver_.segment_received(seg);
        if (seg.length_in_sequence_space() > 0) {
            sender_.send_empty_segment();
        }
        sender_.fill_window();
        send_data();
    }
    // 当前是Fin-Wait-1状态
    else if (sender_.stream_in().eof() && sender_.next_seqno_absolute() == sender_.stream_in().bytes_written() + 2 &&
             sender_.bytes_in_flight() > 0 && !receiver_.stream_out().input_ended()) {
        if (seg.header().fin) {
            // 收到Fin，则发送新ACK，进入Closing/Time-Wait
            sender_.ack_received(seg.header().ackno, seg.header().win);
            receiver_.segment_received(seg);
            sender_.send_empty_segment();
            send_data();
        } else if (seg.header().ack) {
            // 收到ACK，进入Fin-Wait-2
            sender_.ack_received(seg.header().ackno, seg.header().win);
            receiver_.segment_received(seg);
            send_data();
        }
    }
    // 当前是Fin-Wait-2状态
    else if (sender_.stream_in().eof() && sender_.next_seqno_absolute() == sender_.stream_in().bytes_written() + 2 &&
             sender_.bytes_in_flight() == 0 && !receiver_.stream_out().input_ended()) {
        sender_.ack_received(seg.header().ackno, seg.header().win);
        receiver_.segment_received(seg);
        sender_.send_empty_segment();
        send_data();
    }
    // 当前是Time-Wait状态
    else if (sender_.stream_in().eof() && sender_.next_seqno_absolute() == sender_.stream_in().bytes_written() + 2 &&
             sender_.bytes_in_flight() == 0 && receiver_.stream_out().input_ended()) {
        if (seg.header().fin) {
            // 收到FIN，保持Time-Wait状态
            sender_.ack_received(seg.header().ackno, seg.header().win);
            receiver_.segment_received(seg);
            sender_.send_empty_segment();
            send_data();
        }
    }
    // 其他状态
    else {
        sender_.ack_received(seg.header().ackno, seg.header().win);
        receiver_.segment_received(seg);
        sender_.fill_window();
        send_data();
    }
}

bool TCPConnection::active() const { return active_; }

size_t TCPConnection::write(const string &data) {
    if (data.empty()) {
        return 0;
    }

    // 在sender中写入数据并发送
    size_t size = sender_.stream_in().write(data);
    sender_.fill_window();
    send_data();
    return size;
}

//! \param[in] ms_since_last_tick number of milliseconds since the last call to this method
void TCPConnection::tick(const size_t ms_since_last_tick) {
    // 非启动时不接收
    if (!active_) {
        return;
    }

    // 保存时间，并通知sender
    time_since_last_segment_received_ += ms_since_last_tick;
    sender_.tick(ms_since_last_tick);

    // 超时需要重置连接
    if (sender_.consecutive_retransmissions() > TCPConfig::MAX_RETX_ATTEMPTS) {
        reset_connection();
        return;
    }
    send_data();
}

void TCPConnection::end_input_stream() {
    sender_.stream_in().end_input();
    sender_.fill_window();
    send_data();
}

void TCPConnection::connect() {
    // 主动启动，fill_window方法会发送Syn
    sender_.fill_window();
    send_data();
}

void TCPConnection::send_data() {
    // 将sender中的数据保存到connection中
    while (!sender_.segments_out().empty()) {
        TCPSegment seg = sender_.segments_out().front();
        sender_.segments_out().pop();
        // 尽量设置ackno和window_size
        if (receiver_.ackno().has_value()) {
            seg.header().ack = true;
            seg.header().ackno = receiver_.ackno().value();
            seg.header().win = receiver_.window_size();
        }
        segments_out_.push(seg);
    }
    // 如果发送完毕则结束连接
    if (receiver_.stream_out().input_ended()) {
        if (!sender_.stream_in().eof()) {
            linger_after_streams_finish_ = false;
        }

        else if (sender_.bytes_in_flight() == 0) {
            if (!linger_after_streams_finish_ || time_since_last_segment_received() >= 10 * cfg_.rt_timeout) {
                active_ = false;
            }
        }
    }
}

void TCPConnection::reset_connection() {
    // 发送RST标志
    TCPSegment seg;
    seg.header().rst = true;
    segments_out_.push(seg);

    // 在出站入站流中标记错误，使active返回false
    receiver_.stream_out().set_error();
    sender_.stream_in().set_error();
    active_ = false;
}

TCPConnection::~TCPConnection() {
    try {
        if (active()) {
            cerr << "Warning: Unclean shutdown of TCPConnection\n";

            // Your code here: need to send a RST segment to the peer
            reset_connection();
        }
    } catch (const exception &e) {
        std::cerr << "Exception destructing TCP FSM: " << e.what() << std::endl;
    }
}
