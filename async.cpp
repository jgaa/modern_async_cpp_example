
/*
 * See "traditional.cpp" first.
 *
 * This file illustrates how C++ experts write asynchronous code today.
 *
 * The code is highly efficient, and scales way better than the code shown in
 * "traditional.cpp". It is however harder to write, harder to debug and much
 * harder to understand.
 *
 * Using async IO, we don't dedicate one thread to a single connection. We
 * can handle tens of thousands of connections from one thread. Very often,
 * servers that use this approach will have only one thread per CPU-core,
 * no matter how many connections they deal with.
 *
 * What I show here is a simple async state-machine that download the page,
 * using the same naive algorithm as in "traditional.cpp"
 *
 * Copyright 2014 by Jarle (jgaa) Aase <jarle@jgaa.com>
 * I put this code in the public domain.
 */

#include <iostream>
#include <string>
#include <sstream>
#include <future>
#include <thread>
#include <memory>
#include <boost/asio.hpp>

// Convenience...
using boost::asio::ip::tcp;

/*! HTTP Client object. */
class Request
{
    /* We can't use the stack for data this time, so we put what we need
     * as private properties in the object.
     */
    const std::string host_;
    boost::asio::io_service io_service_;
    tcp::resolver resolver_;
    std::promise<std::string> result_;
    std::unique_ptr<tcp::socket> sck_;
    char io_buffer_[1024] = {};
    std::string result_buffer_;

public:
    /*! Constructor
     *
     * Since we use properties to hold the data we need, it makes sense to
     * initialize it in the constructor.
     */
    Request(const std::string& host)
        : host_(host), resolver_(io_service_)
    {}

    /*! Async fetch a single HTTP page, at the root-level "/".
     *
     * Since we gave the host parameter to the constructor, this method
     * is now without arguments.
     *
     * @returns A future that will, at some later time, be able to provide
     *  the content of the page, or throw an exception if the async
     *  fetch failed. We need to return a future, in stead of a string, since
     *  this is an Async method that is expected to return immediately.
     *
     * @Note This is not production-grade code, as we don't look at the
     *   HTTP headers from the server, and don't validate the length of
     *   the returned data. We also don't deal with redirects or authentication.
     *   This is intentionally, as this code is meant to illustrate how we do
     *   network IO, and not how we deal with an increasingly bloated HTTP
     *   standard.
     */
    std::future<std::string> Fetch() {

        // Start resolving the address in another thread.
        std::thread([=]() {
            /* This is the scope of a C++11 lambda expression.
             * It executes in the newly started thread, and when the scope
             * is left, the thread will exit.
             */

            /* Resolve the host-name, and ask asio to call OnResolved()
             * when done. async_resolve() itself will return immediately.
             */
            resolver_.async_resolve( {host_, "80"},
                                     std::bind(&Request::OnResolved, this,
                                               std::placeholders::_1,
                                               std::placeholders::_2));

            /* Run the event-loop for asio now. This function will return
             * when we have no more requests pending - in our case, when
             * we fail or have received the page from the server.
             */
            io_service_.run();
        }
        /* Back in the main-thread, we need to detach the thread we just
         * started from it's local variable - or the program, will die.
         */
        ).detach();

        /* Return the future.
         *
         * At this point, the thread we just started will deal with
         * resolving the host-name and fetching the page on it's own.
         */
        return result_.get_future();
    }

private:
    /*! Callback that is called for an IP belonging to the host
     *
     * The first time, it is initiated from async_resolve. If we fail
     * to connect to that IP, it may be called again, initiated
     * from OnConnected(), as we iterate over the IP numbers returned
     * by the DNS system.
     *
     * If we have to debug this, there are no easy way to know if we were
     * called from async_resolve or OnConnected, just by looking at the
     * properties and stack-trace. This complicates debugging quite a bit.
     */
    void OnResolved(const boost::system::error_code& error,
                    tcp::resolver::iterator iterator) {
        try {
            if (error || (iterator == tcp::resolver::iterator())) {
                // We failed. The exception is picked up below
                throw std::runtime_error("Failed to resolve host");
            }

            // Connect
            sck_ = std::make_unique<tcp::socket>(io_service_);

            /* Initiate an async Connect operation.
             *
             * Ask asio to call OnConnected() when we have a connection
             * or a connection-failed result.
             *
             * async_connect returns immediately
             */
            sck_->async_connect(*iterator,
                                std::bind(&Request::OnConnected, this,
                                          iterator, std::placeholders::_1));
        } catch(...) {
            /* We pick up the exception, and pass it to the result_
             * property. At this moment, the future that the main-thread
             * holds will unblock, and the exception will be re-thrown
             * there when result.get() is called.
             *
             * Since we don't start another async operation,
             * io_service_.run() will return, and our thread will exit.
             */
            result_.set_exception(std::current_exception());
        }
    }

    /*! Callback that is called by asio when we are connected,
     * or failed to connect.
     *
     * If we failed, this callback will participate in iterating
     * over the IP's returned by the DNS system.
     */
    void OnConnected(tcp::resolver::iterator iterator,
                     const boost::system::error_code& error) {

        if (error) {
            /* Simulate a fragment of the loop in "traditional.cpp"
             *    for(; address_it != addr_end; ++address_it)
             */
            ++iterator;

            /* Ask asio to call OnResolved().
             *
             * We post it as a task in stead of calling it directly to
             * avoid stack-buildup and potential side-effects (when the
             * code becomes more complex and we may introduce mutexes etc.)
             */
            io_service_.post(std::bind(&Request::OnResolved, this,
                                       boost::system::error_code(),
                                       iterator));
            return;
        }

        /* Async send the HTTP request
         *
         * Ask asio to call OnSentRequest() when done, or if it failed.
         */
        boost::asio::async_write(*sck_, boost::asio::buffer(GetRequest(host_)),
                                 std::bind(&Request::OnSentRequest, this,
                                           std::placeholders::_1));
    }

    /* Callback when a request have been sent (or failed). */
    void OnSentRequest(const boost::system::error_code& error) {

        try {
            if (error) {
                // Failure. Same work-flow as in OnResolved()
                throw std::runtime_error("Failed to send request");
            }

            // Initiate fetching of the reply
            FetchMoreData();
        } catch(...) {
            // Same work-flow as in OnResolved()
            result_.set_exception(std::current_exception());
        }
    }

    /* Initiate a async read operation to get a reply or part of it.
     *i
     * This function returns immediately.
     */
    void FetchMoreData() {

        /* Ask asio to start a async read, and to call OnDataRead when done */
        sck_->async_read_some(boost::asio::mutable_buffers_1(io_buffer_,
                              sizeof(io_buffer_)),
                              std::bind(&Request::OnDataRead,
                                        this,
                                        std::placeholders::_1,
                                        std::placeholders::_2));
    }

    /*! Callback that is called when we have read (or failed to read) data
     * from the remote HTTP server.
     */
    void OnDataRead(const boost::system::error_code& error,
                    std::size_t bytes_transferred) {

        if (error) {
            /* Just assume that we are done
             *
             * We set a value to the result_, and immediately the value
             * will be available to the main-thread that can get it from
             * result.get(). In this case, there is unlikely to be any
             * exceptions.
             *
             * Since we don't start another async operation,
             * io_service_.run() will return, and our thread will exit.
             */
            result_.set_value(std::move(result_buffer_));
            return;
        }

        /* Append the data read to our private buffer that we will later\
         * hand over to the main thread.
         */
        result_buffer_.append(io_buffer_, bytes_transferred);

        // Initiate another async read from the server.
        FetchMoreData();
    }

    // Construct a simple HTTP request to the host
    std::string GetRequest(const std::string& host) const {
        std::ostringstream req;
        req << "GET / HTTP/1.1\r\nHost: " << host << " \r\n"
            << "Connection: close\r\n\r\n";

        return req.str();
    }
};

int main(int argc, char *argv[])
{
    // Check that we have one and only one argument (the host-name)
    assert(argc == 2 && *argv[1]);

    // Construct our HTTP Client object
    Request req(argv[1]);

    // Initiate the fetch and get the future
    auto result = req.Fetch();

    // Wait for the other thread to do it's job
    result.wait();

    try {
        // Get the page or an exception
        std::cout << result.get();
    } catch(const std::exception& ex) {
        // Explain to the user that there was a problem
        std::cerr << "Caught exception " << ex.what() << std::endl;

        // Error exit
        return -1;
    } catch(...) {
        // Explain to the user that there was an ever bigger problem
        std::cerr << "Caught exception!" << std::endl;

        // Error exit
        return -2;
    }

    // Successful exit
    return 0;
}
