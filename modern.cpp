
/*
 * See "traditional.cpp" and "async.cpp" first.
 *
 * This file shows how we /can/ write async C++ code today. The example
 * uses a feature in recent versions of boost::asio, but AFAIK, the C+++
 * standards committee is working on something similar for C++17.
 *
 * This approach is highly efficient. In a HTTP Server pet-project, I managed
 * to handle 170.000 page-requests per second on my 4-core laptop, using code
 * similar to the code here (although it also did proper HTTP header parsing,
 * time-outs, content injection, statistics and other things that HTTP
 * servers does).
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
#include <boost/asio/spawn.hpp>


using boost::asio::ip::tcp;

/*! HTTP Client object. */
class Request
{
    boost::asio::io_service io_service_;
    std::promise<std::string> result_;

public:

    /*! Async fetch a single HTTP page, at the root-level "/".
     *
     * @param host The host we want to connect to
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
    auto Fetch(const std::string& host) {

        /* Ask asio to call Fetch_ from the IO thread
         *
         * At this time we don't have an event-loop thread running, so asio
         * will just queue the request and execute it later for us.
         */
        boost::asio::spawn(io_service_, std::bind(&Request::Fetch_, this,
                                                  host, std::placeholders::_1));
        /* Start a thread for the actual work
         *
         * Note that we again use a C++11 lambda to execute the main scope
         * of the thread. And that we, again, need to detach the thread from
         * the local variable (since we soon will exit our scope, and the
         * thread needs to carry out the work we assigned to it.
         *
         * As soon as io_service_.run() is called from within the lambda,
         * asio will call Fetch_()
         */
        std::thread([=]() { io_service_.run();}).detach();

        // Return the future to the caller.
        return result_.get_future();
    }

private:

    /*! The implementation of the async resolve and fetch.
     *
     * This is run from the thread we started in Fetch()
     */
    void Fetch_(const std::string& host, boost::asio::yield_context yield) {
        std::string rval;
        boost::system::error_code ec;

        try {
            // Construct a resolver instance
            tcp::resolver resolver(io_service_);

            /* Note that we call async_resolve. What do you think it will
             * return? A future? An iterator?
             *
             * The beauty here is that async_resolve will actually suspend
             * the processing of this method here, save the stack, and return
             * the thread to asio.
             *
             * When asio has finished the resolve request, it will restore
             * the stack, and resume the processing exactly where we left off
             * (at least, that is how it appears to our code), so that
             * what is returned is the iterator.
             *
             * From a coding perspective, this is exactly what we did in
             * "traditional.cpp". However, in this case the thread was
             * free for other jobs while asio waited for the DNS system.
             *
             * Another huge benefit is that in the debugger, and in a core-dump,
             * we will see the full stack-trace of Fetch_, not just a
             * callback that implements a fragment of the functionality.
             */
            auto address_it = resolver.async_resolve({host, "80"}, yield);

            /* Use decltype to copy the type from address_it in stead of
             * typing it. That way we really don't need to know or care about
             * the actual type returned by async_resolve()
             */
            decltype(address_it) addr_end;

            /* Again, our loop looks like a loop. Even if the thread will
             * be able to do many other things while we wait for network IO
             * inside the loop.
             */
            for(; address_it != addr_end; ++address_it) {
                // Construct a TCP socket instance
                tcp::socket sck(io_service_);

                /* Again, we do an async operation where the stack will be
                 * saved, the thread released to other tasks, before the stack
                 * is restored and the processing resumes where it left off.
                 */
                sck.async_connect(*address_it, yield[ec]);
                if (ec) {
                    std::cerr << "Failed to connect to "
                        << address_it->endpoint() << std::endl;

                    // Try another IP
                    continue;
                }

                /* Here we initiate an async write.
                 *
                 * As before, the thread can be used for other things
                 * before processing resumes.
                 *
                 * Note the apparently missing error-handling.
                 *
                 * Here we do not supply [ec] to yield. That causes asio to
                 * throw an exception if async_write fails. Since we are
                 * inside a try/catch scope, the error will actually be dealt
                 * with. (It's pretty awesome that exception handling works
                 * as in traditional code when we effectively are in a
                 * co-routine.
                 */
                boost::asio::async_write(sck,
                                         boost::asio::buffer(GetRequest(host)),
                                         yield);

                /* We can use the stack - no need to put
                 * data as properties (although it may give better
                 * performance - that is something you can experiment with).
                 */
                char reply[1024] {}; // Zero-initialize the buffer

                // Async read data until we fail. (As in the other examples)
                while(!ec) {
                    const auto rlen = sck.async_read_some(
                        boost::asio::mutable_buffers_1(reply, sizeof(reply)),
                                                          yield[ec]);

                    // Append the read data to the data we will return
                    rval.append(reply, rlen);
                }

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
                result_.set_value(move(rval));
                return; // Success!
            }

            // We failed.
            throw std::runtime_error("Unable to connect to any host");
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
            return;
        }
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
    Request req;

    // Initiate the fetch and get the future
    auto result = req.Fetch(argv[1]);

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
