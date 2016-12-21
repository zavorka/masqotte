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

namespace ev
{
struct connect
{
    std::string client_id;
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

struct pingreq
{
};

struct pingresp
{
};

struct disconnect
{
    bool force;
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
std::vector<gsl::byte> serialize(Packet const& packet)
{
    return {};
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
