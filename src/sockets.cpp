/*
*   demosauce - fancy icecast source client
*
*   this source is published under the GPLv3 license.
*   http://www.gnu.org/licenses/gpl.txt
*   also, this is beerware! you are strongly encouraged to invite the
*   authors of this software to a beer when you happen to meet them.
*   copyright MMXI by maep
*/

#include <boost/asio.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/thread/thread.hpp>

#include "settings.h"
#include "logror.h"
#include "sockets.h"

using std::string;
using boost::asio::ip::tcp;

namespace aio = ::boost::asio;
namespace sys = ::boost::system;

struct Sockets::Pimpl
{
    bool send_command(string command, string& result);
    string host;
    uint32_t port;
    aio::io_service io;
};

Sockets::Sockets(string host, uint32_t port):
    pimpl(new Pimpl)
{
    pimpl->host = host;
    pimpl->port = port;
}

bool Sockets::Pimpl::send_command(string command, string& result)
{
    result.clear(); // result string might conatin shit from lasst call or something
    try {
        // create endpoint for address + port
        tcp::resolver resolver(io);
        tcp::resolver::query query(host, boost::lexical_cast<string>(port));
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::resolver::iterator end;
        tcp::socket socket(io);
        // find first suitable endpoint
        sys::error_code error = aio::error::host_not_found;
        while (error && endpoint_iterator != end) {
            socket.close();
            socket.connect(*endpoint_iterator++, error);
        }
        if (error) {
            throw sys::system_error(error);
        }
        // write command to socket
        aio::write(socket, aio::buffer(command), aio::transfer_all(), error);
        if (error) {
            throw sys::system_error(error);
        }
        for (;;) {
            boost::array<char, 256> buf;
            sys::error_code error;
            size_t len = socket.read_some(aio::buffer(buf), error);
            if (error == aio::error::eof)
                break; // Connection closed cleanly by peer.
            else if (error)
                throw sys::system_error(error); // Some other error.
            result.append(buf.data(), len);
        }
        LOG_DEBUG("[sockets] send_command(%s)", command.c_str());
    }
    catch (std::exception& e) {
        LOG_WARN("[sockets] send_command: %i", e.what());
        return false;
    }
    return true;
}

string Sockets::get_next_song()
{
    string result;
    if (!pimpl->send_command("NEXTSONG", result)) {
        ERROR("[sockets] command NEXTSONG failed");
    }
    return result;
}

bool resolve_ip(string host, string& ipAddress)
{
    try {
        aio::io_service io;
        tcp::resolver resolver(io);
        tcp::resolver::query query(tcp::v4(), host, "0");
        tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        tcp::resolver::iterator end;
        if (endpoint_iterator == end)
            return false;
        tcp::endpoint ep = *endpoint_iterator;
        ipAddress = ep.address().to_string();
    }
    catch (std::exception& e) {
        ERROR("[resolve_ip] %s", e.what());
        return false;
    }
    return true;
}

Sockets::~Sockets() {} // empty, to make scoped_ptr happy