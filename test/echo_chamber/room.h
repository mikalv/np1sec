/**
 * (n+1)Sec Multiparty Off-the-Record Messaging library
 * Copyright (C) 2016, eQualit.ie
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of version 3 of the GNU Lesser General
 * Public License as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#pragma once

#include <boost/asio.hpp>
#include <boost/thread/future.hpp>
#include "src/room.h"
#include "timer.h"
#include "conv.h"
#include "client.h"

struct RoomImpl : public std::enable_shared_from_this<RoomImpl>
                , public np1sec::RoomInterface {
    using tcp = boost::asio::ip::tcp;
    using error_code = boost::system::error_code;


    std::string _name;
    std::shared_ptr<Client> _client;
    Timers _timers;
    np1sec::PrivateKey _private_key;
    np1sec::Room _np1sec_room;

    Pipe<error_code> _connect_pipe;
    Pipe<Conv> _created_conversation_pipe;
    Pipe<Conv> _invitation_pipe;
    Pipe<std::string, np1sec::PublicKey> _user_joined_pipe;

    RoomImpl(boost::asio::io_service& ios, std::string name)
        : _name(std::move(name))
        , _client(std::make_shared<Client>(ios, [=] (std::string name, std::string msg) {
                        using std::move;
                        _np1sec_room.message_received(name , msg);
                    }))
        , _private_key(np1sec::PrivateKey::generate(true))
        , _np1sec_room(this, _name, _private_key)
    {}

    template<class H> void connect(tcp::endpoint remote_ep, H&& h)
    {
        _client->connect(remote_ep, [=] (error_code ec) {
                if (ec) return h(ec);
                _connect_pipe.schedule(get_io_service(), std::move(h));
                _np1sec_room.connect();
            });
    }

    boost::asio::io_service& get_io_service() {
        return _client->get_io_service();
    }

    void send_message(const std::string& msg) override
    {
        _client->send_message(_name, msg);
    }

    np1sec::TimerToken* set_timer(uint32_t ms, np1sec::TimerCallback* cb) override
    {
        return _timers.create(get_io_service(), ms, cb);
    }

    void connected() override
    {
        _connect_pipe.apply(boost::system::error_code());
    }

    void disconnected() override
    {
        std::cout << _name << " TODO: disconnected" << std::endl;
    }

    void user_joined(const std::string& name, const np1sec::PublicKey& pubkey) override
    {
        _user_joined_pipe.apply(name, pubkey);
    }

    void user_left(const std::string&, const np1sec::PublicKey&) override
    {
        std::cout << _name << " TODO: user_left" << std::endl;
    }

    np1sec::ConversationInterface* created_conversation(np1sec::Conversation* c) override
    {
        Conv conv(get_io_service(), c);
        auto retval = conv.get_impl();
        _created_conversation_pipe.apply(std::move(conv));
        return retval;
    }

    np1sec::ConversationInterface* invited_to_conversation(np1sec::Conversation* c, const std::string&) override
    {
        Conv conv(get_io_service(), c);
        auto retval = conv.get_impl();

        _invitation_pipe.apply(std::move(conv));
        return retval;
    }

    void stop() {
        _client->stop();
    }
};

class Room {
    using tcp = boost::asio::ip::tcp;

public:
    Room(boost::asio::io_service& ios, std::string name)
        : _ios(ios)
        , _impl(std::make_shared<RoomImpl>(ios, std::move(name)))
    {}

    Room(const Room&) = delete;
    Room& operator=(const Room&) = delete;

    Room(Room&&) = default;
    Room& operator=(Room&&) = default;

    void stop() {
        _impl->stop();
        _impl.reset();
    }

    std::string username() const {
        return _impl->_name;
    }

    ~Room() {
        if (_impl) _impl->stop();
    }

    np1sec::Room* get_np1sec_room() {
        return &_impl->_np1sec_room;
    }

    template<class H>
    void wait_for_user_to_join(H&& h) {
        _impl->_user_joined_pipe.schedule(_ios, std::forward<H>(h));
        //_impl->wait_for_user_to_join(std::forward<H>(h));
    }

    template<class H>
    void connect(const tcp::endpoint& ep, H&& h) {
        _impl->connect(ep, std::forward<H>(h));
    }

    template<class H>
    void create_conversation(H&& h) {
        _impl->_created_conversation_pipe.schedule(_ios, std::forward<H>(h));
        _impl->_np1sec_room.create_conversation();
    }

    template<class H>
    void wait_for_invite(H&& h) {
        _impl->_invitation_pipe.schedule(_ios, std::forward<H>(h));
    }

    boost::asio::io_service& get_io_service() { return _ios; }
private:
    boost::asio::io_service& _ios;
    std::shared_ptr<RoomImpl> _impl;
};

