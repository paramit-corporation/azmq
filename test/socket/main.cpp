/*
    Copyright (c) 2013-2014 Contributors as noted in the AUTHORS file

    This file is part of azmq

    Distributed under the Boost Software License, Version 1.0. (See accompanying
    file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
*/
#include <azmq/socket.hpp>
#include <azmq/util/scope_guard.hpp>

#define BOOST_ENABLE_ASSERT_HANDLER
#include <boost/assert.hpp>
#include <boost/asio/buffer.hpp>

#include <array>
#include <thread>
#include <iostream>
#include <vector>
#include <cstdint>
#include <memory>

#define CATCH_CONFIG_MAIN
#include "../catch.hpp"

std::array<boost::asio::const_buffer, 2> snd_bufs = {{
    boost::asio::buffer("A"),
    boost::asio::buffer("B")
}};

std::string subj(const char* name) {
    return std::string("inproc://") + name;
}

TEST_CASE( "Set/Get options", "[socket]" ) {
    boost::asio::io_service ios;

    azmq::socket s(ios, ZMQ_ROUTER);

    // set/get_option are generic, works for one and all...
    azmq::socket::rcv_hwm in_hwm(42);
    s.set_option(in_hwm);

    azmq::socket::rcv_hwm out_hwm;
    s.get_option(out_hwm);
    REQUIRE(in_hwm.value() == out_hwm.value());
}

TEST_CASE( "Send/Receive synchronous", "[socket]" ) {
    boost::asio::io_service ios;

    azmq::socket sb(ios, ZMQ_ROUTER);
    sb.bind(subj(BOOST_CURRENT_FUNCTION));

    azmq::socket sc(ios, ZMQ_DEALER);
    sc.connect(subj(BOOST_CURRENT_FUNCTION));

    sc.send(snd_bufs);

    azmq::message msg;
    auto size = sb.receive(msg);
    REQUIRE(msg.more() == true);

    size = sb.receive(msg, ZMQ_RCVMORE);
    REQUIRE(size == boost::asio::buffer_size(snd_bufs[0]));
    REQUIRE(msg.more() == true);

    size = sb.receive(msg);
    REQUIRE(size == boost::asio::buffer_size(snd_bufs[1]));
    REQUIRE(msg.more() == false);

    sc.send(snd_bufs);

    std::array<char, 5> ident;
    std::array<char, 2> a;
    std::array<char, 2> b;

    std::array<boost::asio::mutable_buffer, 3> rcv_bufs = {{
        boost::asio::buffer(ident),
        boost::asio::buffer(a),
        boost::asio::buffer(b)
    }};

    size = sb.receive(rcv_bufs);
    REQUIRE(size == 9);
}

TEST_CASE( "Send/Receive async", "[socket_ops]" ) {
    boost::asio::io_service ios_b;
    boost::asio::io_service ios_c;

    azmq::socket sb(ios_b, ZMQ_ROUTER);
    sb.bind(subj(BOOST_CURRENT_FUNCTION));

    azmq::socket sc(ios_c, ZMQ_DEALER);
    sc.connect(subj(BOOST_CURRENT_FUNCTION));

    boost::system::error_code ecc;
    size_t btc = 0;
    sc.async_send(snd_bufs, [&] (boost::system::error_code const& ec, size_t bytes_transferred) {
        SCOPE_EXIT { ios_c.stop(); };
        ecc = ec;
        btc = bytes_transferred;
    });

    std::array<char, 5> ident;
    std::array<char, 2> a;
    std::array<char, 2> b;

    std::array<boost::asio::mutable_buffer, 3> rcv_bufs = {{
        boost::asio::buffer(ident),
        boost::asio::buffer(a),
        boost::asio::buffer(b)
    }};

    boost::system::error_code ecb;
    size_t btb = 0;
    sb.async_receive(rcv_bufs, [&](boost::system::error_code const& ec, size_t bytes_transferred) {
        SCOPE_EXIT { ios_b.stop(); };
        ecb = ec;
        btb = bytes_transferred;
    });

    ios_c.run();
    ios_b.run();

    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btc == 4);
    REQUIRE(ecb == boost::system::error_code());
    REQUIRE(btb == 9);
}

TEST_CASE( "Send/Receive async is_speculative", "[socket_ops]" ) {
    boost::asio::io_service ios_b;
    boost::asio::io_service ios_c;

    azmq::socket sb(ios_b, ZMQ_ROUTER);
    sb.set_option(azmq::socket::allow_speculative(true));
    sb.bind(subj(BOOST_CURRENT_FUNCTION));

    azmq::socket sc(ios_c, ZMQ_DEALER);
    sc.set_option(azmq::socket::allow_speculative(true));
    sc.connect(subj(BOOST_CURRENT_FUNCTION));

    boost::system::error_code ecc;
    size_t btc = 0;
    sc.async_send(snd_bufs, [&] (boost::system::error_code const& ec, size_t bytes_transferred) {
        SCOPE_EXIT { ios_c.stop(); };
        ecc = ec;
        btc = bytes_transferred;
    });

    std::array<char, 5> ident;
    std::array<char, 2> a;
    std::array<char, 2> b;

    std::array<boost::asio::mutable_buffer, 3> rcv_bufs = {{
        boost::asio::buffer(ident),
        boost::asio::buffer(a),
        boost::asio::buffer(b)
    }};

    boost::system::error_code ecb;
    size_t btb = 0;
    sb.async_receive(rcv_bufs, [&](boost::system::error_code const& ec, size_t bytes_transferred) {
        SCOPE_EXIT { ios_b.stop(); };
        ecb = ec;
        btb = bytes_transferred;
    });

    ios_c.run();
    ios_b.run();

    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btc == 4);
    REQUIRE(ecb == boost::system::error_code());
    REQUIRE(btb == 9);
}

TEST_CASE( "Send/Receive async threads", "[socket]" ) {
    boost::asio::io_service ios_b;
    azmq::socket sb(ios_b, ZMQ_ROUTER);
    sb.bind(subj(BOOST_CURRENT_FUNCTION));

    boost::asio::io_service ios_c;
    azmq::socket sc(ios_c, ZMQ_DEALER);
    sc.connect(subj(BOOST_CURRENT_FUNCTION));

    boost::system::error_code ecc;
    size_t btc = 0;
    std::thread tc([&] {
        sc.async_send(snd_bufs, [&] (boost::system::error_code const& ec, size_t bytes_transferred) {
            SCOPE_EXIT { ios_c.stop(); };
            ecc = ec;
            btc = bytes_transferred;
        });
        ios_c.run();
    });

    boost::system::error_code ecb;
    size_t btb = 0;
    std::thread tb([&] {
        std::array<char, 5> ident;
        std::array<char, 2> a;
        std::array<char, 2> b;

        std::array<boost::asio::mutable_buffer, 3> rcv_bufs = {{
            boost::asio::buffer(ident),
            boost::asio::buffer(a),
            boost::asio::buffer(b)
        }};

        sb.async_receive(rcv_bufs, [&](boost::system::error_code const& ec, size_t bytes_transferred) {
            SCOPE_EXIT { ios_b.stop(); };
            ecb = ec;
            btb = bytes_transferred;
        });
        ios_b.run();
    });

    tc.join();
    tb.join();
    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btc == 4);
    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btb == 9);
}

TEST_CASE( "Send/Receive message async", "[socket]" ) {
    boost::asio::io_service ios_b;
    boost::asio::io_service ios_c;

    azmq::socket sb(ios_b, ZMQ_ROUTER);
    sb.bind(subj(BOOST_CURRENT_FUNCTION));

    azmq::socket sc(ios_c, ZMQ_DEALER);
    sc.connect(subj(BOOST_CURRENT_FUNCTION));

    boost::system::error_code ecc;
    size_t btc = 0;
    sc.async_send(snd_bufs, [&] (boost::system::error_code const& ec, size_t bytes_transferred) {
        SCOPE_EXIT { ios_c.stop(); };
        ecc = ec;
        btc = bytes_transferred;
    });

    std::array<char, 5> ident;
    std::array<char, 2> a;
    std::array<char, 2> b;

    boost::system::error_code ecb;
    size_t btb = 0;
    sb.async_receive([&](boost::system::error_code const& ec, azmq::message & msg, size_t bytes_transferred) {
        SCOPE_EXIT { ios_b.stop(); };
        ecb = ec;
        if (ecb)
            return;
        btb += bytes_transferred;
        msg.buffer_copy(boost::asio::buffer(ident));

        if (msg.more()) {
            btb += sb.receive(msg, ZMQ_RCVMORE, ecb);
            if (ecb)
                return;
            msg.buffer_copy(boost::asio::buffer(a));
        }

        if (msg.more()) {
            btb += sb.receive(msg, 0, ecb);
            if (ecb)
                return;
            msg.buffer_copy(boost::asio::buffer(b));
        }
    });

    ios_c.run();
    ios_b.run();

    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btc == 4);
    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btb == 9);
}

TEST_CASE( "Send/Receive message more async", "[socket]" ) {
    boost::asio::io_service ios_b;
    boost::asio::io_service ios_c;

    azmq::socket sb(ios_b, ZMQ_ROUTER);
    sb.bind(subj(BOOST_CURRENT_FUNCTION));

    azmq::socket sc(ios_c, ZMQ_DEALER);
    sc.connect(subj(BOOST_CURRENT_FUNCTION));

    boost::system::error_code ecc;
    size_t btc = 0;
    sc.async_send(snd_bufs, [&] (boost::system::error_code const& ec, size_t bytes_transferred) {
        SCOPE_EXIT { ios_c.stop(); };
        ecc = ec;
        btc = bytes_transferred;
    });

    std::array<char, 5> ident;
    std::array<char, 2> a;
    std::array<char, 2> b;

    std::array<boost::asio::mutable_buffer, 2> rcv_bufs = {{
        boost::asio::buffer(a),
        boost::asio::buffer(b)
    }};

    boost::system::error_code ecb;
    size_t btb = 0;
    sb.async_receive([&](boost::system::error_code const& ec, azmq::message & msg, size_t bytes_transferred) {
        SCOPE_EXIT { ios_b.stop(); };
        ecb = ec;
        if (ecb)
            return;
        btb += bytes_transferred;
        msg.buffer_copy(boost::asio::buffer(ident));

        if (!msg.more())
            return;

        azmq::message_vector v;
        btb += sb.receive_more(v, 0, ecb);
        if (ecb)
            return;
        auto it = std::begin(v);
        for (auto&& buf : rcv_bufs)
            (*it++).buffer_copy(buf);
    });

    ios_c.run();
    ios_b.run();

    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btc == 4);
    REQUIRE(ecc == boost::system::error_code());
    REQUIRE(btb == 9);
}

struct monitor_handler {
    using ptr = std::shared_ptr<monitor_handler>;

#if defined BOOST_MSVC
#pragma pack(push, 1)
    struct event_t
    {
        uint16_t e;
        uint32_t i;
    };
#pragma pack(pop)
#else
    struct event_t
    {
        uint16_t e;
        uint32_t i;
    } __attribute__((packed));
#endif

    azmq::socket socket_;
    std::string role_;
    std::vector<event_t> events_;

    monitor_handler(boost::asio::io_service & ios, azmq::socket& s, std::string role)
        : socket_(s.monitor(ios, ZMQ_EVENT_ALL))
        , role_(std::move(role))
    { }

    void start()
    {
        socket_.async_receive([this](boost::system::error_code const& ec,
                                     azmq::message & msg, size_t) {
                if (ec)
                    return;
                event_t event;
                msg.buffer_copy(boost::asio::buffer(&event, sizeof(event)));
                events_.push_back(event);
                socket_.flush();
                start();
            });
    }

    void cancel()
    {
        socket_.cancel();
    }
};

void bounce(azmq::socket & server, azmq::socket & client) {
    const char *content = "12345678ABCDEFGH12345678abcdefgh";
    std::array<boost::asio::const_buffer, 2> snd_bufs = {{
        boost::asio::buffer(content, 32),
        boost::asio::buffer(content, 32)
    }};

    std::array<char, 32> buf0;
    std::array<char, 32> buf1;

    std::array<boost::asio::mutable_buffer, 2> rcv_bufs = {{
        boost::asio::buffer(buf0),
        boost::asio::buffer(buf1)
    }};
    client.send(snd_bufs);
    server.receive(rcv_bufs);
    server.send(snd_bufs);
    client.receive(rcv_bufs);
}

TEST_CASE( "Socket Monitor", "[socket]" ) {
    boost::asio::io_service ios;
    boost::asio::io_service ios_m;

    using socket_ptr = std::unique_ptr<azmq::socket>;
    socket_ptr client(new azmq::socket(ios, ZMQ_DEALER));
    socket_ptr server(new azmq::socket(ios, ZMQ_DEALER));

    monitor_handler client_monitor(ios_m, *client, "client");
    monitor_handler server_monitor(ios_m, *server, "server");

    client_monitor.start();
    server_monitor.start();

    std::thread t([&] {
        ios_m.run();
    });

    server->bind("tcp://127.0.0.1:9998");
    client->connect("tcp://127.0.0.1:9998");

    bounce(*client, *server);

    ios_m.stop();
    t.join();

    REQUIRE(client_monitor.events_.size() == 2);
    REQUIRE(server_monitor.events_.size() == 2);
    REQUIRE(client_monitor.events_.at(0).e == ZMQ_EVENT_CONNECT_DELAYED);
    REQUIRE(client_monitor.events_.at(1).e == ZMQ_EVENT_CONNECTED);
    REQUIRE(server_monitor.events_.at(0).e == ZMQ_EVENT_LISTENING);
    REQUIRE(server_monitor.events_.at(1).e == ZMQ_EVENT_ACCEPTED);
}
