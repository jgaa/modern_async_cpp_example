
#include <iostream>
#include <string>
#include <sstream>
#include <future>
#include <thread>
#include <memory>
#include <boost/asio.hpp>


using boost::asio::ip::tcp;

class Request
{
    const std::string host_;
    boost::asio::io_service io_service_;
    tcp::resolver resolver_;
    std::promise<std::string> result_;
    std::unique_ptr<tcp::socket> sck_;
    char io_buffer_[1024] = {};
    std::string result_buffer_;
    
public:
    Request(const std::string& host)
        : host_(host), resolver_(io_service_)
    {}

    std::future<std::string> Fetch() {

        // Start resolving the address
        std::thread([=]() {
            resolver_.async_resolve( {host_, "80"},
                                     std::bind(&Request::OnResolved, this,
                                               std::placeholders::_1,
                                               std::placeholders::_2));
            io_service_.run();
        }).detach();

        return result_.get_future();
    }

private:
    void OnResolved(const boost::system::error_code& error,
                    tcp::resolver::iterator iterator) {
        try {
            if (error || (iterator == tcp::resolver::iterator())) {
                throw std::runtime_error("Failed to resolve host");
            }

            // Connect
            sck_ = std::make_unique<tcp::socket>(io_service_);
            sck_->async_connect(*iterator,
                                std::bind(&Request::OnConnected, this,
                                          iterator, std::placeholders::_1));
        } catch(...) {
            result_.set_exception(std::current_exception());
        }
    }

    void OnConnected(tcp::resolver::iterator iterator,
                     const boost::system::error_code& error) {

        if (error) {
            ++iterator;
            io_service_.post(std::bind(&Request::OnResolved, this,
                                       boost::system::error_code(),
                                       iterator));
            return;
        }

        // Send request
        boost::asio::async_write(*sck_, boost::asio::buffer(GetRequest(host_)),
                                 std::bind(&Request::OnSentRequest, this,
                                           std::placeholders::_1));
    }

    void OnSentRequest(const boost::system::error_code& error) {

        try {
            if (error) {
                throw std::runtime_error("Failed to send request");
            }

            // Initiate fetching of the reply
            FetchMoreData();
        } catch(...) {
            result_.set_exception(std::current_exception());
        }
    }

    void FetchMoreData() {
        sck_->async_read_some(boost::asio::mutable_buffers_1(io_buffer_,
                              sizeof(io_buffer_)),
                              std::bind(&Request::OnDataRead,
                                        this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
    }

    void OnDataRead(const boost::system::error_code& error,
                    std::size_t bytes_transferred) {

        if (error) {
            // Just assume we are done
            result_.set_value(std::move(result_buffer_));
            return;
        }

        result_buffer_.append(io_buffer_, bytes_transferred);
        FetchMoreData();
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
    Request req(argv[1]);
    auto result = req.Fetch();
    result.wait();

    try {
        std::cout << result.get();
    } catch(const std::exception& ex) {
        std::cerr << "Caught exception " << ex.what() << std::endl;
    } catch(...) {
        std::cerr << "Caught exception!" << std::endl;
    }
}
