#ifndef BANTAM_CLIENT_H
#define BANTAM_CLIENT_H
#include <chrono>
#include <boost/beast.hpp>
#include <boost/asio/bind_executor.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/deadline_timer.hpp>
#include <boost/asio/connect.hpp>
#include <algorithm>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include <future>

#define RAPIDJSON_HAS_STDSTRING 1
#include <rapidjson/document.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

namespace bantam
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace asio = boost::asio;
    using tcp = asio::ip::tcp;

    struct client;
    using pclient = std::shared_ptr<client>;

    struct client_error : public std::runtime_error
    {
        client_error(const std::string& message) : std::runtime_error(message){}
    };

    struct client  : public std::enable_shared_from_this<client>
    {
        using json_callback_type = std::function<void(const rapidjson::Value& val)>;

        static const size_t timer_period_seconds = 1;
        // Resolver and socket require an io_context
        explicit client(
            boost::asio::io_context& ioc,
            const std::string& host,
            const std::string& path,
            const std::string& port
        );
        void write(std::string&& msg);
        void write(const rapidjson::Value& doc);

        virtual void handle_write()
        {}
        virtual void handle_read(const std::string& /*msg*/)
        {}
        virtual void handle_read_binary(const std::string& /*msg*/)
        {}
        virtual void handle_connected()
        {
            if (ready_callback)
                ready_callback();
        }
        virtual void handle_disconnected()
        {}

        // Start the asynchronous operation
        void run(std::function<void()> _ready_callback);
        void stop();

        void open();
        void close();
        void reconnect();
        bool is_connected() const
        {
            return ws_.lowest_layer().is_open() && handshake_completed;
        }
        const std::string& get_session_name() const
        {return session_name;}

        void subscribe(const std::string& channel_name, const json_callback_type& callback);
        void get_resource(const std::string& path, const json_callback_type& callback);

        int64_t next_opaque()
        {return ++opaque;}
    private:
        void on_resolve(
            boost::system::error_code ec,
            tcp::resolver::results_type results
        );

        void on_connect(boost::system::error_code ec);

        void on_handshake(boost::system::error_code ec);

        void on_write(
            boost::system::error_code ec,
            std::size_t bytes_transferred);

        void on_read(
            boost::system::error_code ec,
            std::size_t bytes_transferred);

        void on_close(boost::system::error_code ec);

        // Report a failure
        void fail(boost::system::error_code ec, char const* what)
        {
            std::cerr << " FAIL [" << session_name << "] " << what << ": " << ec.message() << "\n";
        }
        // Report an info
        void info(const std::string& what)
        {
            std::cerr << " INFO [" << session_name << "] " << what << std::endl;
        }

        int64_t last_read_elapsed() const;

        void write_next();
    private:
        void on_timer(const boost::system::error_code& ec);
        void do_read();
        void write_hello(int64_t opaque);
        void write_pong(int64_t opaque);
    private:
        bool handshake_completed = false;
        tcp::resolver resolver_;
        websocket::stream<tcp::socket> ws_;
        boost::beast::multi_buffer buffer_;

        boost::asio::deadline_timer timer;

        const std::string host;
        const std::string path;
        const std::string port = "443";

        std::chrono::system_clock::time_point last_read_time;
        unsigned long reconnect_seconds = 30;
        bool writing_now = false, reading_now = false;
        std::string session_name;

        std::map<std::string, json_callback_type> subscriptions;
        std::map<int64_t, json_callback_type> resource_reads;

        int64_t opaque = 0;


        std::list<std::string> write_queue;
        std::function<void()> ready_callback;
    };

}//bantam
#endif // BANTAM_CLIENT_H
