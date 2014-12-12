
/*
 * Traditional approach to fetch a page from a HTTP server.
 *
 * We dedicate a thread (in this case the programs main-thread) to
 * fetch the page, using blocking IO. This makes the code easy to write,
 * easy to debug and easy to understand.
 *
 * (However, if we want to use this class in a web-spider, and fetch 1000 pages
 * in parallel, we need to start 1000 threads. That's quite some overhead
 * for such a simple task.)\
 *
 * Copyright 2014 by Jarle (jgaa) Aase <jarle@jgaa.com>
 * I put this code in the public domain.
 */


#include <iostream>
#include <string>
#include <sstream>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

/*! HTTP Client object. */
class Request
{
public:
   /*! Fetch a single HTTP page, at the root-level "/".
    *
    * @param host Host-name to connect to. If the DNS server
    *   returns several IP's, we will try them in sequence until
    *   we successfully connect to one.
    *
    * @returns The content of the page, including HTTP headers
    *
    * @Note This is not production-grade code, as we don't look at the
    *   HTTP headers from the server, and don't validate the length of
    *   the returned data. We also don't deal with redirects or authentication.
    *   This is intentionally, as this code is meant to illustrate how we do
    *   network IO, and not how we deal with an increasingly bloated HTTP
    *   standard.
    */
   std::string Fetch(const std::string& host) {
      std::string rval;
      boost::asio::io_service io_service;

      // Resolve address
      tcp::resolver resolver(io_service);
      auto address_it = resolver.resolve({host, "80"});
      decltype(address_it) addr_end;

      // Iterate over the IP address(es) we got from the DNS systems
      for(; address_it != addr_end; ++address_it) {
         // Connect
         tcp::socket sck(io_service);
         boost::asio::connect(sck, address_it);

         // Send request
         boost::asio::write(sck, boost::asio::buffer(GetRequest(host)));

         // Get response
         char reply[1024] {};
         boost::system::error_code ec;

         // Read the reply until we fail to read more.
         // In production code, we would have looked at the HTTP headers and
         // read exactly the number of bytes the server said she would
         // return.
         while(!ec) {
            const auto rlen = sck.read_some(
               boost::asio::mutable_buffers_1(reply, sizeof(reply)), ec);

            // Add the data we got from the server to the buffer we will return.
            rval.append(reply, rlen);
         }

         // We are done! No need to connect again to another server.
         return rval;
      }

      throw std::runtime_error("Failed to connect to all/any address");
   }

private:
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
    try {
        // Fetch the page and send it to stdout
        std::cout << req.Fetch(argv[1]);
    } catch(std::exception& ex) {
        // We failed - explain why to the user.
        std::cerr << "ERROR: " << ex.what() << std::endl;

        // Error exit
        return -1;
    }

    // Successful exit
    return 0;
}
