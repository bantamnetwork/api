#include <bantam/client.h>
#include <bantam/order_book.h>

#include <CLI11.hpp>
#include <csignal>
#include <cstdlib>

std::promise<bool> wait_signal;

void signal_handler(int signal)
{
    wait_signal.set_value(true);
}

int main(int argc, char** argv) try
{
    std::string host = "127.0.0.1";
    std::string port = "9999";

    CLI::App app("Bantam network client example");
    app.add_option("host", host, "Server host address");
    app.add_option("port", port, "Server port");

    try
    {
        app.parse(argc, argv);
    }
    catch(CLI::Error& e)
    {
        return app.exit(e);
    }

    boost::asio::io_context ioc;
    bantam::pclient client = std::make_shared<bantam::client>(ioc, host, "/", port);

    bantam::order_book book;
    auto data_callback = [&](const rapidjson::Value& doc)
    {
        using namespace rapidjson;
//        OStreamWrapper w(std::cout);
//        Writer<OStreamWrapper> ww(w);
//        doc.Accept(ww);
//        std::cout << std::endl;

        const auto& content = doc["data"].GetObject();
        std::string type = content["type"].GetString();
        if (type == "snapshot")
            book.clear();
        const auto& bids = content["bids"].GetArray();
        for (const auto& v : bids)
        {
            double price = v[0].GetDouble();
            double vol   = v[1].GetDouble();
            book.update_bid(price, vol);
        }
        const auto& asks = content["asks"].GetArray();
        for (const auto& v : asks)
        {
            double price = v[0].GetDouble();
            double vol   = v[1].GetDouble();
            book.update_ask(price, vol);
        }
#ifdef WIN32
        std::system("cls");
#else
        std::system("clear");
#endif
        book.print();
    };
    auto ready_callback = [&](){
        client->get_resource("channels", [&](const rapidjson::Value& doc)
        {
            std::cout << "Server has " << doc.Size() << " channels:" << std::endl;
            for (rapidjson::SizeType i = 0; i < doc.Size(); ++i)
            {
                std::cout << doc[i].GetString() << std::endl;;
            }
            if (doc.Size() > 0)
            {
                client->subscribe(doc[0].GetString(), data_callback);
//                client->subscribe("binance/ETHBTC", data_callback);
            }
        });
    };

    client->run(ready_callback);
    std::thread t{[&](){ioc.run();}};
    std::cout << "Press Ctrl+C to stop" << std::endl;
    wait_signal.get_future().get();
    client->stop();
    ioc.stop();
    t.join();
    return EXIT_SUCCESS;
}
catch(std::exception& e)
{
    std::cerr << e.what() << std::endl;
}
