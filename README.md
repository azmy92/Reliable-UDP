Reliable-UDP
============

A reliable data transfer protocol implementation using Linux UDP sockets, we implemented two modes, mode1: stop and wait, mode2: selective reapet.
The project is composed of two parts:
 - Client side
 - Server side
 

Client side
-----
To run the cient side you must first intialize its configuration file : client.in, example:
>21234

>IMG_0050.jpg

 - First line: Server's port number

 - Second line: The requested file name

Server side
-----
To run the server side you must first intialize its configuration file : server.in, example:
>21234

>50

>10

>0.01

 - First line: Server's port number

 - Second line: Maximum window size for selective reapet

 - fourth  line: Package dropping probability (for simulation purpose)
