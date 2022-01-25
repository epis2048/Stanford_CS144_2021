#ifndef SPONGE_LIBSPONGE_TCP_SENDER_HH
#define SPONGE_LIBSPONGE_TCP_SENDER_HH

#include "byte_stream.hh"
#include "tcp_config.hh"
#include "tcp_segment.hh"
#include "wrapping_integers.hh"

#include <functional>
#include <queue>

//! \brief The "sender" part of a TCP implementation.

//! Accepts a ByteStream, divides it up into segments and sends the
//! segments, keeps track of which segments are still in-flight,
//! maintains the Retransmission Timer, and retransmits in-flight
//! segments if the retransmission timer expires.
class TCPSender {
  private:
    //! our initial sequence number, the number for our SYN.
    WrappingInt32 isn_;

    //! outbound queue of segments that the TCPSender wants sent
    std::queue<TCPSegment> segments_out_{};
    //! 等待返回接收成功的段
    std::queue<TCPSegment> segments_wait_{};

    //! retransmission timer for the connection
    unsigned int initial_retransmission_timeout_;
    //! RTO，重传等待时间
    unsigned int retransmission_timeout_;
    //! 重传次数
    uint16_t consecutive_retransmissions_ = 0;
    //! 重传计时器
    size_t retransmissions_timer_ = 0;
    //! 重传计时器是否启动
    bool retransmissions_timer_running_ = false;

    //! outgoing stream of bytes that have not yet been sent
    ByteStream stream_;

    //! the (absolute) sequence number for the next byte to be sent
    uint64_t next_seqno_{0};
    uint64_t recv_seqno_{0};

    // 标记是否开始和结束
    bool syn_ = false;
    bool fin_ = false;

    // 尚在发送中的数据大小
    uint64_t bytes_in_flight_ = 0;
    // 接收方的窗口大小
    uint16_t receiver_window_size_ = 0;

  public:
    //! Initialize a TCPSender
    TCPSender(const size_t capacity = TCPConfig::DEFAULT_CAPACITY,
              const uint16_t retx_timeout = TCPConfig::TIMEOUT_DFLT,
              const std::optional<WrappingInt32> fixed_isn = {});

    //! \name "Input" interface for the writer
    //!@{
    ByteStream &stream_in() { return stream_; }
    const ByteStream &stream_in() const { return stream_; }
    //!@}

    //! \name Methods that can cause the TCPSender to send a segment
    //!@{

    //! \brief A new acknowledgment was received
    void ack_received(const WrappingInt32 ackno, const uint16_t window_size);

    //! \brief Generate an empty-payload segment (useful for creating empty ACK segments)
    void send_empty_segment();

    //! \brief create and send segments to fill as much of the window as possible
    void fill_window();

    //! \brief Notifies the TCPSender of the passage of time
    void tick(const size_t ms_since_last_tick);
    //!@}

    //! \name Accessors
    //!@{

    //! \brief How many sequence numbers are occupied by segments sent but not yet acknowledged?
    //! \note count is in "sequence space," i.e. SYN and FIN each count for one byte
    //! (see TCPSegment::length_in_sequence_space())
    size_t bytes_in_flight() const;

    //! \brief Number of consecutive retransmissions that have occurred in a row
    unsigned int consecutive_retransmissions() const;

    //! \brief TCPSegments that the TCPSender has enqueued for transmission.
    //! \note These must be dequeued and sent by the TCPConnection,
    //! which will need to fill in the fields that are set by the TCPReceiver
    //! (ackno and window size) before sending.
    std::queue<TCPSegment> &segments_out() { return segments_out_; }
    //!@}

    //! \name What is the next sequence number? (used for testing)
    //!@{

    //! \brief absolute seqno for the next byte to be sent
    uint64_t next_seqno_absolute() const { return next_seqno_; }

    //! \brief relative seqno for the next byte to be sent
    WrappingInt32 next_seqno() const { return wrap(next_seqno_, isn_); }

    //! \brief 发送一个TCP段
    void send_tcp_segment(TCPSegment &tcp_segment);
    //!@}
};

#endif  // SPONGE_LIBSPONGE_TCP_SENDER_HH
