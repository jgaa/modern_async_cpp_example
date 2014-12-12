This mini-project consists of some examples I wrote for a
30 minutes mini-lecture about modern async C++.

In order to put the modern code in perspective, I implemented
the same algorithm three times. First, "traditional.cpp" using
blocking IO. Then, in "async.cpp" I show how most people
would implement it today, using scattered methods and callbacks.
And last in "modern.cpp", I show how elegantly it can be done
with recent versions of boost::asio, using coroutines in stead
of callbacks. The code is async, but it looks just as simple as
traditional blocking code. As an extra bonus, the stack is
intact, so if your program crash, you will have much more
accurate information than in the async case.

The code is very simple; it resolves a host-name, loops over
the received IP numbers until it connects (or fails to connect
to any of them). When connected, it sends a minimalistic
HTTP request to the address, and prints the server-response
directly to the standard output.

The code is compiled with clang 3.5 and g++ 4.9.1

I put this code in the public domain.

Jarle (jgaa) Aase, December 2014.
