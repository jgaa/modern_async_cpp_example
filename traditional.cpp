
#include <iostream>
#include <string>
#include <sstream>
#include <boost/asio.hpp>

using boost::asio::ip::tcp;

class Request
{
public:
   std::string Fetch(const std::string& host) {
      std::string rval;
      boost::asio::io_service io_service;

      // Resolve address
      tcp::resolver resolver(io_service);
      auto address_it = resolver.resolve({host, "80"});
      decltype(address_it) addr_end;

      for(; address_it != addr_end; ++address_it) {
         // Connect
         tcp::socket sck(io_service);
         boost::asio::connect(sck, address_it);

         // Send request
         boost::asio::write(sck, boost::asio::buffer(GetRequest(host)));

         // Get response
         char reply[1024] {};
         boost::system::error_code ec;

         while(!ec) {
            const auto rlen = sck.read_some(
               boost::asio::mutable_buffers_1(reply, sizeof(reply)), ec);

            rval.append(reply, rlen);
         }

         break; // Only need one
      }

      // TODO: Deal with all connections failed

      return rval;
   }

private:
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
   std::cout << req.Fetch(argv[1]);
}
