This mini-project consists of some examples I wrote for a 
30 minutes lecture about modern async C++.

In order to put the modern code in perspektive, I implemented
the same class three times. First, "traditional.cpp" using 
blocking IO. Then, in "async.cpp" I show how most people 
would implement it today, using scattered methods and callbacks.
And last in "modern.cpp", I show how elegantly it can be done 
with recet versons of boost::asio, using coroutines in stead 
of callbacks. The code is async, but it looks just as simple as 
traditional blocking code. As an extra bonus, the stack is 
intact, so if your program crash, you will have much more 
accurate information than in the async case. 

The code is very simple; it resolves a hostname, loops over 
the received IP nubers until it connects (or fails to connect 
to any of them). When connected, it sends a minimaslistic 
HTTP request to the address, and prints the server-response 
directly to the standard output.

I put this code in the pubic domain.
