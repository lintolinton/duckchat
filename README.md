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
 
