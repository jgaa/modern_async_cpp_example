
#include <iostream>
#include <string>
#include <sstream>
#include <future>
#include <thread>
#include <memory>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>


using boost::asio::ip::tcp;

class Request
{
    boost::asio::io_service io_service_;
    std::promise<std::string> result_;
    
public:
    auto Fetch(const std::string& host) {

        // Call Fetch_ from the IO thread
        boost::asio::spawn(io_service_, std::bind(&Request::Fetch_, this,
                                                  host, std::placeholders::_1));
        // Start a thread for the actual work
        std::thread([=]() { io_service_.run();}).detach();

        return result_.get_future();
    }

private:
    void Fetch_(const std::string& host, boost::asio::yield_context yield) {
        std::string rval;
        boost::system::error_code ec;

        try {
            // Resolve address
            tcp::resolver resolver(io_service_);
            auto address_it = resolver.async_resolve({host, "80"}, yield);
            decltype(address_it) addr_end;

            for(; address_it != addr_end; ++address_it) {
                // Connect
                tcp::socket sck(io_service_);
                sck.async_connect(*address_it, yield[ec]);
                if (ec) {
                    std::cerr << "Failed to connect to "
                        << address_it->endpoint() << std::endl;
                    continue;
                }

                // Send request
                boost::asio::async_write(sck,
                                         boost::asio::buffer(GetRequest(host)),
                                         yield);

                // Get response
                char reply[1024] {};
                while(!ec) {
                    const auto rlen = sck.async_read_some(
                        boost::asio::mutable_buffers_1(reply, sizeof(reply)),
                                                          yield[ec]);

                    rval.append(reply, rlen);
                }

                result_.set_value(move(rval));
                return; // Success!
            }
            throw std::runtime_error("Unable to connect to any host");
        } catch(...) {
            result_.set_exception(std::current_exception());
            return;
        }
    }

    std::string GetRequest(const std::string& host) const {
        std::ostringstream req;
        req << "GET / HTTP/1.1\r\nHost: " << host << " \r\n"
            << "Connection: close\r\n\r\n";

        return req.str();
    }
};

int main(int argc, char *argv[])
{
    assert(argc == 2 && *argv[1]);
    Request req;
    auto result = req.Fetch(argv[1]);
    result.wait();

    try {
        std::cout << result.get();
    } catch(const std::exception& ex) {
        std::cerr << "Caught exception " << ex.what() << std::endl;
    } catch(...) {
        std::cerr << "Caught exception!" << std::endl;
    }
}
