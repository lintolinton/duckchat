**DuckChat**




Chat or Instant Messaging is one of the most popular applications on the Internet. In this assignment, you will develop a chat application that speaks the DuckChat protocol. This will familiarize you with the task of implementing a network protocol and introduce you to the Berkeley socket API. The specification for DuckChat is provided for you. Since everyone will be implementing the same protocol, your chat programs will be able to interoperate with one another.
DuckChat is a simple client-server protocol. You should implement both the client and server sides of the DuckChat protocol for this assignment. Users run client software to connect to a server and communicate with other connected users. Users join a channel (sometimes called a "chat room") and communicate with other people on the channel. Users may join as many channels as they please and can even create their own channels.
Client user interface
The client program must take exactly three command-line arguments. The first is the hostname where the server is running. The second argument is the port number on which the server is listening. The third argument is the user's username.
When the client starts, it automatically connects to the chat server, joins a channel called “Common”, and provides the user a prompt (i.e. the client must send the join message to join “Common”). When the user types/enters text at the prompt and hits 'Enter’, the text is sent to the server (using the ”say request" message), and the server relays the text to all users on the channel (including the speaker).
The exception is when the text begins with a forward slash (’/’) character. In that case, the text is interpreted as a special command. These special commands are supported by the DuckChat client:
   •	/exit: Logout the user and exit the client software.
   •	/joln channel: Join (subscribe in) the named channel, creating the channel if it does not exist.
   •	/1eave channeL: Leave the named channel.
   •	/1ist: List the names of all channels.
   •	/who channel: List the users who are on the named channel.
   •	/switch channel: Switch to an existing named channel that the user has already joined.
 
A user can listen to multiple channels at the same time but can send a text (be active) on only one channel at any point in time (known as an ”active channel" for a user). The most recently joined channel by a user is always the active channel for that user. Note that Common is the active channel for each user upon invoking the client program. All other channels, other than the active channel, act as listening channels only. The user may use the /switch command to switch to another channel. The client should keep track of the active channel for the user and therefore the /switch command does not require sending any message to the server. However, the join command requires the client to send a message to the server.
If the user leaves the active channel, all their typed text is discarded until s/he joins a new channel or switches to an already subscribed channel. The client should detect when the user tries to switch to a channel to which it has not already subscribed and give an error message to the user. If the switch command fails, the active channel should remain unchanged. Otherwise, the user's active channel becomes the named channel in the switch command.
When a client receives a text on a subscribed channel, it must be displayed in the following format:


Before displaying text, the client application should print many backspace characters (’\b’ in C) to erase the prompt and anything the local user has typed. (Carefully test this on ix if you are using Java.) After the text is printed, the client should redisplay the prompt and any user input. This means the client will need to keep track of the user's input as they are typing it.
Below is a sample run. The bold text is typed by the user.


**Server**
To make the project seem more real, but not without the added complexity of TCP, DuckChat uses UDP to communicate. While UDP is not reliable, we can run the client and server on the same machine and not worry about dropping packets or out-of-order packets.
 
The server takes two arguments: the host address to bind to and the port number. The host address can be either ’localhost', the IP hostname of the machine that the server is running on, or the IP address of an interface connected to the machine. Once the server is running, clients may use the hostname and port to connect to the server. Note that if you use ’localhost', you will not be able to connect to the server from another machine, but you also do not have to worry about dropped packets.
The server does not need to directly interact with the user in any way. However, it is strongly recommended that the server outputs debugging messages to the console. For example, each time the server receives a message from a client, it can output a line describing the contents of the message and who it is from using the following format: [channel][user][message] where the message denotes a command and its parameters (if any).
The server's primary job is to deliver messages from user X to all users on X’s active channel. To accomplish this, the server must keep track of individual users and the channels they have subscribed to. On the flip side, the server must also track each channel and the subscribed users on it.
Channel creation and deletion at the server are handled implicitly. Whenever a channel has no users, it is deleted. Whenever a user joins a channel that did not exist, it is created.
If the server receives a message from a user who is not logged in, the server should silently ignore the message.

**Protocol**
The DuckChat protocol runs over UDP. Each UDP datagram from a client is a request, asking the server to perform some action. Each UDP datagram from the server is a text meant to be displayed to the user.
Each message begins with a 32-bit type identifier. This is a code that designates what type of message the datagram contains. By examining this code, the application can determine how to interpret the rest of the message.
The following messages may be sent to the server. They correspond closely with the commands the user may issue on the client. The server must parse these messages, then perform the right action. For the login, logout, join, and leave requests, the server must update its records of which users are present and what channels the user belongs to. For the say request, the server must relay the text to all users on that channel. For the list and who requests, the server must respond to the client with the corresponding text message. The type code is listed in parentheses. Note that this type code is used as the 32-bit identifier in the header of each packet so that you can classify each packet as it arrives.
    •	login request(0): The message contains an additional 32-byte username field. Here’s the layout of the packet:


    •	logout request(1): No additional fields are included. Here’s the layout of the packet:

    •	join request(2): The message contains an additional 32-byte channel name field. Here’s the layout of the packet:

    •	leave request(3): The message contains an additional 32-byte channel name field. Here's the layout of the packet:


    •	say request(4): The message contains an additional 32-byte channel name field followed by a 64- byte text field. Here’s the layout of the packet:


    •	list request(5): No additional fields are included. Here's the layout of the packet:


    •	who request(6): The message contains an additional 32-byte channel name field. Here’s the layout of the packet:


    •	keep alive request(7): No additional fields are included. This message is only used in the graduate student portion of the assignment. Here's the layout of the packet:


    If the server finds a problem with the user’s request, it denies the request by sending an error message back to the user. The following messages may be sent to the client:
    •	say (0): The message contains an additional 32-byte channel name field, a 32-byte username field, and a 64-byte text field. Here’s the layout of the packet:



    •	list (1): The message contains a 4-byte field that specifies the total number of channels. Following that are 32-byte channel names, one for each channel. Here’s the layout of the packet:

    •	who (2): The message contains a 4-byte field that specifies the total number of users on the channel. Following that is a 32-byte channel name and 32-byte usernames, one for each user. Here's the layout of the packet:



    •	error (3): The message contains a 64-byte text field with an error message for the user. Here's the layout of the packet:
    
 Duckchat S2S protocol   
    
In this assignment, your server must take a variable number of parameters. The first two parameters will always be present and are the IP address and port number of the server. The rest of the parameters are IP addresses and port numbers of additional servers. We will provide a script that starts up several servers and connects them together. It’s available here	(

Each server must maintain additional state. Each server already keeps track of which users are in each channel. Now, the server must additionally note if any adjacent servers are subscribed to a channel. The servers subscribed to a channel will form a tree. When a user types a message, the user’s server transmits the message throughout the tree.
 
When a server receives a message from one of its users, the server broadcasts the message to any adjacent servers on the channel. If a server receives a message from another server, it forwards it to any other servers on that channel. Obviously, the server also sends the message to any interested users.
Remember that this tree is a per-channel overlay built over the existing topology. The topology itself never changes; servers never form new connections once started. The tree is a subset of this topology, and a separate tree exists for each channel.
For simplicity, you will not be required to implement inter-server versions of List and Who.

Forming Trees
When a user joins a channel, the user’s server checks to see if it is already subscribed to that channel. If so, the server need not take any additional steps. If not, the server must attempt to locate other servers subscribed to that channel.
The server begins by sending a Join message to all adjacent servers. When a server receives one of these messages, it checks to see if it is already subscribed. If so, the join message ends there. If not, the server subscribes itself to the channel and forwards the message to all remaining interfaces.
Intermediate servers must subscribe themselves in this way to ensure there is a path to distant servers on the same channel.
Removing unnecessary servers from a channel will be done in a lazy fashion. When a server receives a Say message but has nowhere to forward it, it responds with a Leave message. In other words, if a server is a leaf in the tree with no users, it removes itself from the tree. Note that this means that a server has to make sure that it does not have any clients and that it also only has at most one other server subscribed to the channel before it removes itself from the tree. In other words, the server should not have any client in the particular channel and it should know about only one other server in the channel (the one that is sending messages to it) when it decides to remove itself from the tree.
To guard against loops, additional steps must be taken. Inter-server Say messages must include a unique identifier, and each server must maintain a list of recent identifiers. When the server receives a new message, it checks against this list. If a duplicate is detected, the server knows a loop has been found. It discards the Say message and sends a Leave message to the sender. The Leave message is not forwarded further. This removes the extra link from the loop.
Finally, to guard against network failures, soft-state Joins will be used. Every server must renew its subscriptions by sending a new Join message once per minute. A server interprets a two-minute interval with no Join as a Leave. This ensures that a crashed server will not split the tree.
There is no simple way for a server to generate globally unique identifiers. Therefore, the Imaginarians have agreed to settle for identifiers that are unique with high probability. To accomplish this, servers use their random number generator to create unique identifiers. Servers must seed their random number generator by reading bytes from /dev/urandom.
 
Protocol

The protocol between clients and servers is unchanged. However, the new DuckChat Server-to-Server protocol must be supported. Like the DuckChat client-server protocol, each message begins with a 32- bit type identifier. There are just three messages in the DuckChat Server-to-Server protocol:
•	S2S Join (8): The message contains a 32-byte channel name. Here’s the layout of the packet:

•	S2S Leave (9): The message contains a 32-byte channel name. Here’s the layout of the packet:

•	S2S Say (10): The message contains a 64-bit unique identifier, a 32-byte username, a 32-byte channel name, and a 64-byte text field. Here's the layout of the packet:

Output

 
To simplify debugging and grading, servers must output a message each time they transmit or receive a message. Because all the servers will typically be writing to the same terminal, it is necessary to label each message with the server’s IP address and port number. These messages must follow these examples:


Note that the server that receives or sends the message is always on the left side. Who the server receives from or sends to is on the right side. Also, note that the 3rd line is a client (at 127.0.0.1:8295) sending a say message to the server at 127.0.0.1:12792.

 
 
 
