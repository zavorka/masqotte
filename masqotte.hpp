#include <cstddef>
#include <string>
#include <tuple>

#include <asio.hpp>
#include <boost/sml.hpp>
#include <gsl/gsl>

namespace masqotte
{

namespace sml = boost::sml;
using std::uint8_t;
using std::uint16_t;
using std::uint32_t;
using std::uint64_t;

enum class qos_t: uint16_t
{
    QOS0 = 0x00,
    QOS1 = 0x01,
    QOS2 = 0x02,
    FAILURE = 0x80
};

using topic_t = std::string;
using topic_filter_t = std::tuple<topic_t, qos_t>;
using topic_filter_list_t = std::vector<topic_filter_t>;
using filters_t = topic_filter_list_t;
using payload_t = gsl::span<gsl::byte>;

namespace packet
{
struct pingreq
{
};

struct pingresp
{
};

struct subscribe
{
    uint16_t packet_id;
    filters_t filters;
};

struct connack {
    bool session_present;
    uint8_t response_code;
};

struct puback
{
    uint16_t packet_id;
};

struct suback
{
    uint16_t packet_id;
    using qos_obtained_t = std::vector<qos_t>;
    qos_obtained_t qos_obtained;
};

struct unsuback
{
    uint16_t packet_id;
};

struct publish
{
    bool dup = false;
    qos_t qos = qos_t::QOS0;
    bool retain = false;
    topic_t topic_name;
    payload_t payload;
};
}

namespace ev
{
struct connect
{
    std::string client_id;
};

struct connack {
    bool session_present;
    uint8_t response_code;
};

struct subscribe
{
    uint16_t packet_id;
    filters_t filters;
    payload_t subscribe;
};

struct unsubscribe
{
    uint16_t packet_id;
    std::vector<std::string> filters;
    payload_t unsubscribe;
};

struct disconnect
{
    bool force;
};



struct publish_out
{
    payload_t publish;
};

struct shutdown_timeout
{
};
}

namespace detail {
template <typename Packet>
std::vector<uint8_t> get_packet_buffer(Packet const& packet, uint16_t size)
{
    return std::vector<uint8_t>(packet.data(), size);
}

template <typename Packet>
std::vector<uint8_t> serialize(Packet const& packet)
{
    auto variable_length = packet.get_variable_length();
    // packet size : variable length + control header + encoded length size
    uint16_t packet_size = variable_length + 1 + (int)(variable_length/128) + 1;
    auto packet_data = get_packet_buffer(packet, packet_size);
    serialize_impl(packet, std::back_inserter(packet_data), variable_length);
    return packet_data;
}

constexpr uint16_t get_variable_length(packet::pingreq const &)
{
    return 0;
}

template <typename Iterator>
Iterator serialize_impl(packet::subscribe const& packet, Iterator iter, int variable_length)
{
// control header
    *iter++ = 0x82;
    *(iter++) = variable_length >> 8;
    *(iter++) = variable_length & 0xff;
    *(iter++) = packet.packet_id >> 8;
    *(iter++) = packet.packet_id & 0xff;
    for(auto const & filter : packet.filters)
    {
        auto filter_size = std::get<0>(filter).size();
        *(iter++) = filter_size >> 8;
        *(iter++) = filter_size & 0xff;
        *iter++ = static_cast<uint8_t>(std::get<1>(filter));
    }
    return iter;
}

template <typename Iterator>
Iterator serialize_impl(packet::pingreq const &, Iterator iter, int)
{
    std::array<uint8_t , 2> bytes{0xc0, 0x00};
    std::copy(bytes.cbegin(), bytes.cend(), iter);
    return iter;
}

struct client_interface {
    virtual void send_to_broker(uint8_t const* data, uint16_t length) = 0;
    virtual void receive_from_broker(uint8_t const* data, uint16_t length) = 0;
    virtual void queue_task(/* ... */) = 0;
    virtual void update_connection_status(/* ... */) = 0;

    template <typename Packet>
    void send(Packet const& packet)
    {
        auto buffer = serialize(packet);
        send_to_broker(reinterpret_cast<uint8_t const*>(buffer.data()), static_cast<uint16_t>(buffer.size()));
    }
};

struct send_packet
{
    template <typename Fsm>
    void operator() (ev::publish_out const& evt, Fsm& fsm)
    {
        fsm.m_client->send(evt.publish);
    }

    template <typename Fsm>
    void operator() (ev::subscribe const& evt, Fsm& fsm)
    {
        fsm.m_client->send(evt.subscribe);
    }

    template <typename Fsm>
    void operator() (ev::unsubscribe const& evt, Fsm& fsm)
    {
        fsm.m_client->send(evt.unsubscribe);
    }


};

template <typename Connection, typename Executor>
struct client_interface_wrapper : client_interface
{
    client_interface_wrapper(Connection& broker, Executor& executor)
      : m_broker(broker)
      , m_executor(executor)
    {}

    virtual void send_to_broker(uint8_t const* data, uint16_t length) override
    {
        m_broker.send(data, length);
    }

private:
    Connection& m_broker;
    Executor& m_executor;
};
}

struct sub
{
    auto operator()() const noexcept
    {
        using namespace boost::sml;
        return make_transition_table(
             *"connect_broker"_s + on_entry<_> = "negotiate_broker"_s
            , "negotiate_broker"_s = "wait_connection_ack"_s
            , "wait_connection_ack"_s + event<ev::connack> = X
        );
    }
};

struct states
{
    struct FSM {
        detail::client_interface *m_client;
    };

    auto operator()() const noexcept {
        using namespace boost::sml;
        return make_transition_table(
             *"not_connected"_s + sml::on_entry<_> / [](detail::client_interface* client) { client->update_connection_status(); }
            , "not_connected"_s + event<ev::connect> = "connect_broker"_s
            , "connect_broker"_s = state<sub>
            , "connected"_s + sml::on_entry<_> / [](detail::client_interface* client) { client->update_connection_status(); }
            , "connected"_s + sml::on_exit<_> / [](detail::client_interface* client) { client->update_connection_status(); }
            , "connected"_s + event<ev::publish_out> / detail::send_packet{}
            , "connected"_s + event<ev::subscribe> / detail::send_packet{}
            , "connected"_s + event<ev::unsubscribe> / detail::send_packet{}
            , "connected"_s + event<ev::disconnect> = "shutting_down"_s
            , "shutting_down"_s + event<ev::shutdown_timeout> = "not_connected"_s
        );
    }
};


template <typename Connection, typename Executor, typename PublishHandler>
class client
{
public:
    client(std::string id, Connection& connection, Executor& task_executor)
      : m_id(id)
      , m_client_interface(connection, task_executor)
    {
    }

    void connect() {}
    void disconnect(bool force=false) {}
    void subscribe(topic_filter_t filter) {}
    void subscribe(topic_filter_list_t filter) {}
    void unsubscribe(topic_filter_t filter) {}
    void unsubscribe(topic_filter_list_t filter) {}

    template <typename F>
    void set_publish_handler(F&& handler) {
        m_publish_handler = PublishHandler(handler);
    }
    void publish(topic_t topic, payload_t payload, qos_t qos, bool retain) {};

private:
    detail::client_interface_wrapper<Connection, Executor> m_client_interface;
    PublishHandler m_publish_handler;
    std::string m_id;

    /* detail::client_machine m_client_machine; */
};

}
