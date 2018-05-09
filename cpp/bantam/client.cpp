#include "client.h"

namespace bantam
{

client::client(asio::io_context &ioc, const std::string &host, const std::string &path, const std::string &port)
    : resolver_(ioc)
    , ws_(ioc)
    , timer(ioc, boost::posix_time::seconds(timer_period_seconds))
    , host(host)
    , path(path)
    , port(port)
{
}

void client::write(std::string &&msg)
{
    if (!is_connected())
        return;
    write_queue.push_back(std::move(msg));
    write_next();
}

void client::write(const rapidjson::Value &doc)
{
    using namespace rapidjson;
    StringBuffer buffer;
    Writer<StringBuffer> writer(buffer);
    doc.Accept(writer);
    write(std::string(buffer.GetString(), buffer.GetString() + buffer.GetLength()));
}

void client::run(std::function<void()> _ready_callback)
{
    ready_callback = _ready_callback;
    open();
    timer.expires_from_now(boost::posix_time::seconds(timer_period_seconds));
    timer.async_wait(std::bind(
                         &client::on_timer,
                         shared_from_this(),
                         std::placeholders::_1));
}

void client::stop()
{
    timer.cancel();
}

void client::open()
{
    info("Opening connection");
    last_read_time = std::chrono::system_clock::now();
    buffer_.consume(buffer_.size());
    // Look up the domain name
    resolver_.async_resolve(
                host,
                port,
                std::bind(
                    &client::on_resolve,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
}

void client::close()
{
    info("Closing connection");
    if (is_connected())
    {
        // Close the WebSocket connection
        ws_.async_close(websocket::close_code::normal,
                        std::bind(
                            &client::on_close,
                            shared_from_this(),
                            std::placeholders::_1));
    }
    reading_now = false;
    writing_now = false;
}

void client::reconnect()
{
    info("Reconnecting");
    close();
    open();
}

void client::get_resource(const std::string &path, const client::json_callback_type &callback)
{
    if (!callback)
        throw client_error("Invalid argument value: callback");

    int64_t id = next_opaque();
    resource_reads[id] = callback;

    using namespace rapidjson;
    Document doc(kObjectType);
    doc.AddMember("type", Value("get").Move(), doc.GetAllocator());
    doc.AddMember("resource", Value(StringRef(path)).Move(), doc.GetAllocator());
    doc.AddMember("opaque", Value(id).Move(), doc.GetAllocator());
    write(doc);
}

void client::subscribe(const std::string &channel_name, const client::json_callback_type &callback)
{
    if (!handshake_completed)
        throw client_error("Connection is not ready");

    subscriptions.emplace(channel_name, callback);
    using namespace rapidjson;
    Document doc(kObjectType);
    doc.AddMember("type", Value("subscribe").Move(), doc.GetAllocator());
    doc.AddMember("channel", Value(StringRef(channel_name)).Move(), doc.GetAllocator());
    doc.AddMember("opaque", Value(next_opaque()).Move(), doc.GetAllocator());
    write(doc);
}

void client::on_resolve(boost::system::error_code ec, tcp::resolver::results_type results)
{
    if(ec)
        return fail(ec, "resolve");
    info("Resolve");

    // Make the connection on the IP address we get from a lookup
    boost::asio::async_connect(
                ws_.next_layer(),
                results.begin(),
                results.end(),
                std::bind(
                    &client::on_connect,
                    shared_from_this(),
                    std::placeholders::_1));
}

void client::on_connect(boost::system::error_code ec)
{
    if(ec)
        return fail(ec, "connect");
    info("Connect");

    // Perform the websocket handshake
    ws_.async_handshake(host, path,
                        std::bind(
                            &client::on_handshake,
                            shared_from_this(),
                            std::placeholders::_1));
}

void client::on_handshake(boost::system::error_code ec)
{
    if(ec)
        return fail(ec, "handshake");
    info("Handhsake");

    do_read();
}

void client::on_write(boost::system::error_code ec, size_t bytes_transferred)
{
    boost::ignore_unused(bytes_transferred);

    if(ec)
        return fail(ec, "write");
    BOOST_VERIFY(writing_now);
    BOOST_VERIFY(!write_queue.empty());
    writing_now = false;
    write_queue.pop_front();
    if (!write_queue.empty())
        write_next();

    try
    {handle_write();}
    catch(std::exception& e)
    {
        std::cerr << session_name << " Handle write - " << e.what() << std::endl;
        close();
    }
}

void client::on_read(boost::system::error_code ec, size_t bytes_transferred)
{
    using namespace rapidjson;

    boost::ignore_unused(bytes_transferred);

    reading_now = false;

    if(ec)
        return fail(ec, "read");

    last_read_time = std::chrono::system_clock::now();
    std::string str = boost::beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    try
    {
        if (ws_.got_text())
        {
            using namespace rapidjson;
            Document doc;
            doc.Parse(str);
            if (!doc.HasMember("type"))
                throw client_error("Sequence failed, invalid message format");
            std::string type = doc["type"].GetString();
            int64_t opaque_id = doc.HasMember("opaque") ? doc["opaque"].GetInt64() : -1;
            if (type == "hello")
            {
                if (handshake_completed)
                    throw client_error("Connection sequence error, handshake already completed");
                handshake_completed = true;
                write_hello(opaque_id);
                try
                {handle_connected();}
                catch(std::exception& e)
                {
                    std::cerr << session_name << " Handle connected - " << e.what() << std::endl;
                    close();
                }
            }
            else if (type == "ping")
            {
                write_pong(opaque_id);
            }
            else if (type == "get")
            {
                auto it = resource_reads.find(opaque_id);
                if (it == resource_reads.end())
                    throw client_error("Invalid resource read opaque id: " + std::to_string(opaque_id));

                it->second(doc["content"]);
            }
            else if (type == "data")
            {
                const std::string& channel = doc["channel"].GetString();
                auto it = subscriptions.find(channel);
                if (it != subscriptions.end())
                    it->second(doc);
            }
        }
        else if (ws_.got_binary())
            handle_read_binary(str);
    }
    catch(std::exception& e)
    {
        std::cerr << session_name << " Handle read - " << e.what() << std::endl;
        close();
    }

    do_read();
    write_next();
}

void client::on_close(boost::system::error_code ec)
{
    if(ec)
        return fail(ec, "close");
    info("closed");
    try
    {handle_disconnected();}
    catch(std::exception& e)
    {
        std::cerr << "Handle disconnected - " << e.what() << std::endl;
        close();
    }

}

int64_t client::last_read_elapsed() const
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now() - last_read_time).count();
}

void client::write_next()
{
    if (writing_now || write_queue.empty() || !is_connected())
        return;

    writing_now = true;
    // Send the message
    ws_.async_write(
                boost::asio::buffer(write_queue.front()),
                std::bind(
                    &client::on_write,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
}

void client::on_timer(const boost::system::error_code &ec)
{
    if (last_read_elapsed() >= reconnect_seconds)
        reconnect();

    if (!ec)
    {
//        timer.expires_at(boost::posix_time::second_clock::universal_time() + boost::posix_time::seconds(timer_period_seconds));
        timer.expires_from_now(boost::posix_time::seconds(timer_period_seconds));
        timer.async_wait(std::bind(
                             &client::on_timer,
                             shared_from_this(),
                             std::placeholders::_1));
    }
    else return fail(ec, "Reconnect timer");
}

void client::do_read()
{
    if (reading_now)
        return;
    reading_now = true;
    // Read a message into our buffer
    ws_.async_read(
                buffer_,
                std::bind(
                    &client::on_read,
                    shared_from_this(),
                    std::placeholders::_1,
                    std::placeholders::_2));
}

void client::write_hello(int64_t opaque)
{
    using namespace rapidjson;
    Document doc(kObjectType);
    doc.AddMember("type", Value(StringRef("hello")).Move(), doc.GetAllocator());
    doc.AddMember("opaque", Value(opaque).Move(), doc.GetAllocator());
    doc.AddMember("protocol_version", Value(StringRef("1.0")).Move(), doc.GetAllocator());

    write(doc);
}

void client::write_pong(int64_t opaque)
{
    using namespace rapidjson;
    Document doc(kObjectType);
    doc.AddMember("type", Value(StringRef("pong")).Move(), doc.GetAllocator());
    doc.AddMember("opaque", Value(opaque).Move(), doc.GetAllocator());

    write(doc);
}

}//bantam
