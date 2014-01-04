Reliable-UDP
============

A reliable data transfer protocol implementation using Linux UDP sockets, we implemented two modes, mode1: stop and wait, mode2: selective repeat.
The project is composed of two parts:
 - Client side
 - Server side
 

Client side
-----
To run the client side you must first intialize its configuration file : client.in, example:
>21234

>IMG_0050.jpg

 - First line: Server's port number

 - Second line: The requested file name

Server side
-----
The server side handles each new client request in a new chid process.

To run the server side you must first intialize its configuration file : server.in, example:
>21234

>50

>10

>0.01

 - First line: Server's port number

 - Second line: Maximum window size for selective reapet

 - fourth  line: Package dropping probability (for simulation purpose)
 

Logging
-----
The output of each the server and client sides is loggeg in a log.txt file.
