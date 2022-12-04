#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <string>
#include <map>
#include <iostream>
#include <time.h>
#include <vector>
#include <signal.h>
#include <algorithm>
#include <chrono>

using namespace std;

// #include "hash.h"
#include "duckchat.h"

#define MAX_CONNECTIONS 10
#define HOSTNAME_MAX 100
#define PORT_MAX 6
#define MAX_MESSAGE_LEN 65536
#define HEARTBEAT_INTERVAL 60 //Timeout before sending join
#define KICK_SERVER_OUT_TIME 120 // Leave channel if server not responded

static volatile sig_atomic_t received_alarm = 0;

struct server_peers_info
{
	int sockfd;
	string hostname;
	int port;
	struct sockaddr_in s_addr_in;
	uint32_t heart_beat_tstamp;
};

// typedef map<string,string> channel_type; //<username, ip+port in string>
typedef map<string, struct sockaddr_in> channel_type; //<username, sockaddr_in of user>

int s; // socket for listening and sending
struct sockaddr_in server;
string server_hostname;
string server_port;

typedef map<string, struct server_peers_info> server_channel_type;
map<string, struct sockaddr_in> server_peers;
map<string, struct sockaddr_in> usernames; //<username, sockaddr_in of user>
map<string, int> active_usernames;		   // 0-inactive , 1-active
// map<struct sockaddr_in,string> rev_usernames;
map<string, string> rev_usernames; //<ip+port in string, username>
// map<string,channel_type> channels;
map<string, channel_type> channels;
map<string, struct server_peers_info> server_peers_map;
map<string, vector<server_channel_type>> server_channels;
map<uint64_t, struct text_server_say> server_to_server_say;

void handle_socket_input();
void handle_login_message(void *data, struct sockaddr_in sock);
void handle_logout_message(struct sockaddr_in sock);
void handle_join_message(void *data, struct sockaddr_in sock);
void handle_leave_message(void *data, struct sockaddr_in sock);
void handle_say_message(void *data, struct sockaddr_in sock);
void handle_list_message(struct sockaddr_in sock);
void handle_who_message(void *data, struct sockaddr_in sock);
void handle_keep_alive_message(struct sockaddr_in sock);
void send_error_message(struct sockaddr_in sock, string error_msg);
void initialize_peer_servers();
void get_current_time(time_t *);
void soc(struct sockaddr_in, string channel_name);
int start_io_multiplexing(int main_sock, fd_set *rfds, fd_set *wfds, int *nfds);
void handle_ss_communication();
static void sig_alarm_handler(int sig);
void send_ss_join(struct sockaddr_in sock_addr_in, string channel_name, string req_server);
void handle_ss_join_message(void *data, struct sockaddr_in sock);
void handle_ss_leave_message(void *data, struct sockaddr_in sock);
void broadcast_join_message(string channel);
void handle_ss_say_message(void *data, struct sockaddr_in sock);
uint64_t random_number_generator();
void send_ss_leave(string channel, struct sockaddr_in sock);
void send_ss_say_message(string message, string username, string channel, uint64_t msg_id);
int broadcast_say_message_to_clients(string text, string channel, string username);

map<string, struct server_peers_info> populate_list_server_peers(char *hostname, char *port, sockaddr_in s_addr_in, uint32_t heart_beat_tstamp, map<string, struct server_peers_info> list_server_peers);


void send_ss_leave(string channel, struct sockaddr_in sock){
	void *data;
	struct req_ss_leave rq_ss_leave;
	rq_ss_leave.req_type = REQ_SERVER_LEAVE;
	strcpy(rq_ss_leave.req_channel, channel.c_str());
	data = &rq_ss_leave;
	int bytes_sent = sendto(s, data, sizeof(rq_ss_leave), 0, (struct sockaddr *)&sock, sizeof(sock));
	if(bytes_sent < 0)
		cout<< "Failed to send ss_leave payload"<<endl;

}

map<string, struct server_peers_info> populate_list_server_peers(char *hostname, char *port, sockaddr_in s_addr_in, uint32_t heart_beat_tstamp, map<string, struct server_peers_info> list_server_peers)
{
	char buff[50];
	hostent *hentry;
	string ip_addr;
	struct server_peers_info server_peers_p;
	if ((hentry = gethostbyname(hostname)) == NULL)
	{
		cout << "Unable to resolve host " << hostname << endl;
		// server doesnt exist.. return early
		return list_server_peers;
	}
	server_peers_p.s_addr_in = s_addr_in;
	memcpy(&server_peers_p.s_addr_in.sin_addr, hentry->h_addr_list[0], hentry->h_length);
	server_peers_p.port = atoi(port);
	server_peers_p.hostname = inet_ntoa(server_peers_p.s_addr_in.sin_addr);
	ip_addr = server_peers_p.hostname;
	server_peers_p.heart_beat_tstamp = heart_beat_tstamp;
	sprintf(buff, ":%s", port);
	ip_addr.append(buff);
	list_server_peers.insert(pair<string, struct server_peers_info>(ip_addr, server_peers_p));
	return list_server_peers;
}

uint64_t random_number_generator()
{
	const char *pathname = "/dev/urandom";
	size_t bytes_to_read = 8;
	ssize_t bytes_read;
	uint64_t seed_val;
	char buff[9];
	memset(buff, '\0', 9);
	int fd;
	fd = open(pathname, O_RDONLY);
	if (fd == -1)
	{
		cout << "Error unable to open /dev/urandom path, errno " << errno << endl;
		return -1;
	}
	bytes_read = read(fd, buff, bytes_to_read);
	if (bytes_read != 8)
	{

		cout << "Error unable read from /dev/urandom path, errno: " << errno << endl;
		return -1;
	}
	memcpy(&seed_val, buff, bytes_read);
	close(fd);
	return seed_val;
}

void handle_ss_say_message(void *data, sockaddr_in sock)
{
	

	struct text_server_say *txt_svr_say = (struct text_server_say *)data;
	// cout<< "Received S2S say message, wow";
	
	string ip = inet_ntoa(sock.sin_addr);
	int port = ntohs(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;
	
	uint64_t msg_id = txt_svr_say->txt_server_id;
	string channel = txt_svr_say->txt_server_channel;
	string username = txt_svr_say->txt_server_username;
	string text = txt_svr_say->txt_server_text;

	//check if we're a leaf
	map<string, vector<server_channel_type>>:: iterator svr_itr;
	svr_itr = server_channels.find(channel);
	if(svr_itr == server_channels.end()){
		send_ss_leave(channel, sock);
		return;
	}

	map<uint64_t, struct text_server_say>::iterator itr;
	itr = server_to_server_say.find(msg_id);

	if(itr == server_to_server_say.end()){
		if(broadcast_say_message_to_clients(text, channel, username) != -1)
		{
			cout<<server_hostname<<":"<<server_port<<" "<<key<<" recv S2S say "<<username<<" "<<channel <<" "<<text<< endl;
			struct text_server_say msg_svr_say;
			server_to_server_say[msg_id] = msg_svr_say;
			msg_svr_say.txt_type = REQ_SERVER_SAY;
			msg_svr_say.txt_server_id = msg_id;
			strcpy(msg_svr_say.txt_server_text, text.c_str());
			strcpy(msg_svr_say.txt_server_username, username.c_str());
			strcpy(msg_svr_say.txt_server_channel, channel.c_str());
			send_ss_say_message(text, username, channel, msg_id);
		}

		return;
	}

	//duplicate message
	send_ss_leave(channel, sock);
		
}

void send_ss_say_message(string message, string username, string channel, uint64_t msg_id)
{
	// cout<< "About to send S2S message, ";
	map<uint64_t, struct text_server_say>::iterator itrt;
	itrt = server_to_server_say.find(msg_id);
	if (itrt != server_to_server_say.end() && ! msg_id == 0) //message already sent, drop packet
		return;
	if(msg_id == 0)
		msg_id = random_number_generator();

	// Loop through server_channels and send messages to servers whose channel matches
	// cout<<"["<<msg_id<<"]"<<server_hostname<< ":" <<server_port<<" "<<" send S2S say, channel "<<channel<<" does exist"<<endl;
	map<string, vector<server_channel_type>>::iterator itr;
	itr = server_channels.find(channel);
	if (itr == server_channels.end())
	{
		cout << "Channel " << channel << " does not have any connected peers, dropping packet" << endl;
		return;
	}
	for (server_channel_type svr_chan_type : server_channels[channel])
	{
		map<string, struct server_peers_info> svr_peers_map;
		svr_peers_map = svr_chan_type;
		for (auto pair : svr_peers_map)
		{
			string key = pair.first;
			struct server_peers_info svr_peers_i = pair.second;
			struct text_server_say txt_svr_say;
			int bytes_sent;
			void *send_buff;
			txt_svr_say.txt_type = REQ_SERVER_SAY;
			memcpy(&txt_svr_say.txt_server_id, &msg_id, 8);
			strcpy(txt_svr_say.txt_server_channel, channel.c_str());
			strcpy(txt_svr_say.txt_server_username, username.c_str());
			strcpy(txt_svr_say.txt_server_text, message.c_str());
			send_buff = &txt_svr_say;
			server_to_server_say[msg_id] = txt_svr_say;
			bytes_sent = sendto(s, send_buff, sizeof txt_svr_say, 0, (struct sockaddr *)&svr_peers_i.s_addr_in, sizeof svr_peers_i.s_addr_in);
			if (bytes_sent < 0)
			{
				cout << "Error occured during s2s msg sending, errno: " << errno << endl;
			}
			else
			{
				cout<< server_hostname << ":" << server_port << " " << key << " send S2S say " << username << " " << channel << " " << message << endl;
			}
		}
	}
}

void broadcast_join_message(string channel)
{

	struct server_peers_info svr_peers_i;
	for (auto pair : server_peers_map)
	{
		svr_peers_i = pair.second;
		send_ss_join(svr_peers_i.s_addr_in, channel, pair.first);
	}
}

void handle_ss_leave_message(void *data, struct sockaddr_in sock)
{
	// get message fields
	struct req_ss_leave *msg;
	msg = (struct req_ss_leave *)data;
	
	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);
	time_t current_tstamp;
	get_current_time(&current_tstamp);
	string timestamp = to_string(current_tstamp);
	int port = ntohs(sock.sin_port);
	vector<server_channel_type> svr_vec;
	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	// check whether key is in rev_usernames
	map<string, vector<server_channel_type>>::iterator iter;

	iter = server_channels.find(channel);
	if (iter == server_channels.end())
	{
		// Server asking us to leave a channel that doesn't exist, ignore
		cout << key << " Channel doesn't exist, ignoring request to leave: " << channel << endl;
	}
	else
	{
		cout << "Initializing channel exit procedure" << endl;

		for (size_t i = 0; i < server_channels[channel].size(); i++)
		{
			map<string, struct server_peers_info>::iterator itr;
			itr = server_channels[channel][i].find(key);
			if (itr == server_channels[channel][i].end())
			{
				cout << "Server " << key << " not found in channel " << channel << endl;
			}
			else
			{
				cout << server_hostname << ":" << server_port << " " << key << " recv S2S leave "<< channel<<endl;
				server_channels[channel].erase(server_channels[channel].begin() + i);

			}
		}
	}
}

void handle_ss_join_message(void *data, struct sockaddr_in sock)
{
	// get message fields
	struct req_ss_join *msg;
	msg = (struct req_ss_join *)data;
	string channel = msg->req_channel;
	string default_channel = "Common";
	string ip = inet_ntoa(sock.sin_addr);
	int port = ntohs(sock.sin_port);
	vector<server_channel_type> svr_vec;
	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;
	time_t current_tstamp;
	get_current_time(&current_tstamp);
	map<string, vector<server_channel_type>>::iterator iter;
	struct server_peers_info svr_peers_i;
	iter = server_channels.find(channel);
	
	if(iter != server_channels.end()){
		for(size_t i=0; i < server_channels[channel].size(); i++){
			map<string, struct server_peers_info>:: iterator itrt;
			for(auto pr: server_channels[channel][i]){
				svr_peers_i = pr.second;
				if(itrt != server_channels[channel][i].end())
				{
					cout << server_hostname << ":" << server_port << " " << key << " recv S2S join "<< channel<< " but already subscribed to the channel" << endl;
					return;
				}
			}
			
		}	
	}
	cout << server_hostname << ":" << server_port << " " << key << " recv S2S join " << channel << endl;
	vector<server_channel_type> svr_chan_t_vec;
	server_channels[channel] = svr_chan_t_vec;
	map<string, struct server_peers_info> svr_peers_m;
	//struct server_peers_info svr_peers_i;
	svr_peers_m[key] = svr_peers_i;

	svr_peers_i.hostname = ip;
	svr_peers_i.port = port;
		
	svr_peers_i.heart_beat_tstamp = current_tstamp;
	svr_peers_i.s_addr_in = sock;
	svr_peers_m[key] = svr_peers_i;
	server_channels[channel].push_back(svr_peers_m);
	broadcast_join_message(channel);
}

static void sig_alarm_handler(int sig)
{
	received_alarm = 1;
}

void handle_ss_communication()
{
	//We have to send an ss join to all the peers on the channels we're subscribed to
	time_t current_tstamp;
	int elapsed;
	get_current_time(&current_tstamp);
	struct server_peers_info svr_peers_info;
	//loop through all channels, and all peers connected to those channels
	for (auto pair : server_channels)
	{
		string chan_name = pair.first;
		vector<server_channel_type> peers_vec = pair.second;
		for(size_t index = 0; index < server_channels[chan_name].size(); index++){
		 	map<string, struct server_peers_info>  & svr_chan_type=  server_channels[chan_name].at(index);
			for(auto pr: svr_chan_type){
				string req_svr = pr.first;
				struct server_peers_info svr_p_info = pr.second;
				struct sockaddr_in sk_addr_in;
				if((current_tstamp - svr_p_info.heart_beat_tstamp) > KICK_SERVER_OUT_TIME){
					//Kick out channels for servers that havent responded within two minutes
					
					send_ss_leave(chan_name, svr_p_info.s_addr_in);
					cout << server_hostname << ":" << server_port << " " << req_svr << " send S2S leave " << chan_name << endl;
					server_channels[chan_name].erase(server_channels[chan_name].begin() + index);
					return;
				}
				//svr_p_info.heart_beat_tstamp = current_tstamp;
				svr_chan_type[req_svr] = svr_p_info;
				server_channels[chan_name][index] = svr_chan_type;
				memcpy(&sk_addr_in, &svr_p_info.s_addr_in, sizeof(svr_p_info.s_addr_in));
				send_ss_join(sk_addr_in, chan_name, req_svr);
		}
		}
		
	}

	received_alarm = 0;
}

int start_io_multiplexing(int main_sock, fd_set *rfds, fd_set *wfds, int *nfds)
{
	struct server_peers_info svr_peers_info;
	// cout << "Starting IO Multiplexing "<<endl;
	for (auto pair : server_peers_map)
	{
		svr_peers_info = pair.second;
		FD_SET(svr_peers_info.sockfd, wfds);
		FD_SET(svr_peers_info.sockfd, rfds);
		FD_SET(main_sock, rfds);
		if (svr_peers_info.sockfd > *nfds)
			*nfds = svr_peers_info.sockfd;
	}
	return 1;
}

void send_ss_join(struct sockaddr_in sock_addr_in, string channel_name, string req_server)
{
	ssize_t bytes_sent;
	void *buff;
	struct req_ss_join ss_join_msg;
	ss_join_msg.req_type = REQ_SERVER_JOIN;
	memset(ss_join_msg.req_channel, '\0', CHANNEL_MAX);
	strcpy(ss_join_msg.req_channel, channel_name.c_str());
	buff = &ss_join_msg;
	map<string, vector<server_channel_type>>:: iterator itr;
	itr = server_channels.find(channel_name);
	if(itr == server_channels.end()){
	 	vector<server_channel_type> svr_chan_type;
	 	server_channels[channel_name] = svr_chan_type;
	}
	bytes_sent = sendto(s, buff, sizeof ss_join_msg, 0, (struct sockaddr *)&sock_addr_in, sizeof sock_addr_in);
	
	if (bytes_sent < 1)
	{
		cout << "Failed to send data" << endl;
		cout << errno << endl;
	}
	else
	{
		cout << server_hostname << ":" << server_port << " " << req_server << " send S2S join " << channel_name << endl;
	}

}

void initialize_peer_servers()
{
	// Save the state of all neighbors so we can easily broadcast join messages
	struct server_peers_info svr_peers_info;
	for (auto pair : server_peers_map)
	{
		svr_peers_info = pair.second;
		int sockfd, conn, port;
		struct hostent *he;
		port = svr_peers_info.port;
		struct sockaddr_in svr_addr_in;
		svr_addr_in.sin_family = AF_INET;
		svr_addr_in.sin_port = htons(port);
		string hostname = svr_peers_info.hostname;
		if ((he = gethostbyname(hostname.c_str())) == NULL)
		{
			cout << "Failed to resolved host " << pair.first << endl;
			continue;
		}
		memcpy(&svr_addr_in.sin_addr, he->h_addr_list[0], he->h_length);
		svr_peers_info.sockfd = s;
		svr_peers_info.s_addr_in = svr_addr_in;
		server_peers_map[pair.first] = svr_peers_info;
	}
	broadcast_join_message("Common");
}

void get_current_time(time_t *current_timestamp)
{
	struct timeval current_time;
	gettimeofday(&current_time, NULL);
	// cout << "timestamp : " << current_time.tv_sec <<endl;
	*current_timestamp = current_time.tv_sec;
}

int main(int argc, char *argv[])
{
	struct itimerval it_new_val;
	static struct timeval start, interval;
	struct sigaction sigact;
	start.tv_sec = HEARTBEAT_INTERVAL;
	start.tv_usec = 0;
	interval.tv_sec = HEARTBEAT_INTERVAL;
	interval.tv_usec = 0;
	it_new_val.it_interval = interval;
	it_new_val.it_value = start;

	if (argc < 3)
	{
		printf("Usage: ./server domain_name port_num\n");
		exit(1);
	}

	// Case where user hasn't given correct server topology
	else if (argc % 2 == 0)
	{
		printf(
			"Usage: ./server server1_domain server1_port [server2_domain server2_port ...]\n");
		exit(1);
	}

	sigemptyset(&sigact.sa_mask);
	sigact.sa_flags = 0;
	sigact.sa_handler = sig_alarm_handler;

	if(sigaction(SIGALRM, &sigact, NULL) == -1)
	{
		cout<< "Fatal, timer failed to start timer due to " << errno <<endl;
		exit(-1);
	}
	cout<< " Alarm handler initialized "<<endl;

	if(setitimer(ITIMER_REAL, &it_new_val, 0) < 0)
	{
			cout<< "Fatal, timer failed to start timer due to " << errno <<endl;
			exit(-1);
	}
	cout << "Timer initialized"<<endl;

	// server_fingerprint = argv[1] + ":" + argv[2];
	char hostname[HOSTNAME_MAX];
	int port;

	strcpy(hostname, argv[1]);
	port = atoi(argv[2]);

	s = socket(PF_INET, SOCK_DGRAM, 0);
	if (s < 0)
	{
		perror("socket() failed\n");
		exit(1);
	}

	// struct sockaddr_in server;

	struct hostent *he;

	server.sin_family = AF_INET;
	server.sin_port = htons(port);

	if ((he = gethostbyname(hostname)) == NULL)
	{
		puts("error resolving hostname..");
		exit(1);
	}
	memcpy(&server.sin_addr, he->h_addr_list[0], he->h_length);
	server_hostname = inet_ntoa(server.sin_addr);
	server_port.append(argv[2]);
	int err;

	err = ::bind(s, (struct sockaddr *)&server, sizeof server);

	if (err < 0)
	{
		printf("Fatal error, ::bind to %s:%d failed\n", hostname, port);
		exit(-1);
	}
	else
	{
		cout<<"Socket initialized and bound to port "<<argv[2]<<endl;
	}

	// testing maps end

	// create default channel Common
	string default_channel = "Common";
	map<string, struct sockaddr_in> default_channel_users;
	channels[default_channel] = default_channel_users;
	vector<server_channel_type> svr_chan_vc_type;
	// server_channels[default_channel] = svr_chan_vc_type;
	char peer_port[PORT_MAX];
	char peer_host[HOSTNAME_MAX];
	int p_port;
	time_t current_time_stamp;
	struct sockaddr_in sk_addr_in;
	struct hostent *hnet;
	map<string, struct server_peers_info> server_peers_temp;

	if (argc >= 4)
	{
		int j;

		for (j = 3; j < argc - 1; j = j + 2)
		{
			strcpy(peer_host, argv[j]);
			strcpy(peer_port, argv[j + 1]);
			p_port = atoi(argv[j + 1]);
			// peer_socket = -1;
			get_current_time(&current_time_stamp);

			server_peers_temp = populate_list_server_peers(peer_host, peer_port, sk_addr_in, current_time_stamp, server_peers_temp);
		}
		server_peers_map = server_peers_temp;
		initialize_peer_servers();
	}

	for (;;) // server runs for ever
	{

		// initialize_peer_servers(server_peers_map);

		// use a file descriptor with a timer to handle timeouts
		int rc;
		int nfds;
		fd_set readfds;

		nfds = s;
		FD_ZERO(&readfds);
		// FD_ZERO(&writefds);
		FD_SET(s, &readfds);

		struct timeval slct_timeval;
		slct_timeval.tv_sec = 0;
		slct_timeval.tv_usec = 0;

		rc = select(s + 1, &readfds, NULL, NULL, &slct_timeval);

		if ((rc < 0) && (errno == EINTR))
		{
			sleep(1);
			continue;
		}

		int socket_data = 0;

		if (FD_ISSET(s, &readfds))
		{
			// reading from socket
			handle_socket_input();
			socket_data = 1;
		}

		if(received_alarm == 1 )
			handle_ss_communication(); //TODO
	}

	return 0;
}

void handle_socket_input()
{

	struct sockaddr_in recv_client;
	ssize_t bytes;
	void *data;
	size_t len;
	socklen_t fromlen;
	fromlen = sizeof(recv_client);
	char recv_text[MAX_MESSAGE_LEN];
	data = &recv_text;
	len = sizeof recv_text;

	bytes = recvfrom(s, data, len, 0, (struct sockaddr *)&recv_client, &fromlen);

	if (bytes < 0)
	{
		perror("recvfrom failed\n");
	}
	else
	{
		// printf("received message\n");

		struct request *request_msg;
		request_msg = (struct request *)data;

		// printf("Message type:");
		request_t message_type = request_msg->req_type;

		// printf("%d\n", message_type);

		switch (message_type)
		{

		case REQ_LOGIN:
			handle_login_message(data, recv_client); // some methods would need recv_client
			break;

		case REQ_LOGOUT:
			handle_logout_message(recv_client);
			break;

		case REQ_JOIN:
			handle_join_message(data, recv_client);
			break;

		case REQ_LEAVE:
			handle_leave_message(data, recv_client);
			break;

		case REQ_SAY:
			handle_say_message(data, recv_client);
			break;

		case REQ_LIST:

			handle_list_message(recv_client);
			break;

		case REQ_WHO:
			handle_who_message(data, recv_client);
			break;

		case REQ_SERVER_JOIN:
			handle_ss_join_message(data, recv_client);
			break;

		case REQ_SERVER_LEAVE:
			handle_ss_leave_message(data, recv_client);
			break;
		case REQ_SERVER_SAY:
			handle_ss_say_message(data, recv_client);
			break;

		default:
			// send error message to client
			send_error_message(recv_client, "*Unknown command");
			break;
		}
	}
}

void handle_login_message(void *data, struct sockaddr_in sock)
{
	struct request_login *msg;
	msg = (struct request_login *)data;

	string username = msg->req_username;
	usernames[username] = sock;
	active_usernames[username] = 1;

	// rev_usernames[sock] = username;

	// char *inet_ntoa(struct in_addr in);
	string ip = inet_ntoa(sock.sin_addr);
	// cout << "ip: " << ip <<endl;
	int port = ntohs(sock.sin_port);
	// unsigned short short_port = sock.sin_port;
	// cout << "short port: " << short_port << endl;
	// cout << "port: " << port << endl;

	char port_str[6];
	sprintf(port_str, "%d", port);
	// cout << "port: " << port_str << endl;

	string key = ip + ":" + port_str;
	// cout << "key: " << key <<endl;
	rev_usernames[key] = username;

	cout << server_hostname << ":" << server_port << " " << key << " recv Request login " << username << endl;
}

void handle_logout_message(struct sockaddr_in sock)
{

	// construct the key using sockaddr_in
	string ip = inet_ntoa(sock.sin_addr);
	// cout << "ip: " << ip <<endl;
	int port = ntohs(sock.sin_port);

	char port_str[6];
	sprintf(port_str, "%d", port);
	// cout << "port: " << port_str << endl;

	string key = ip + ":" + port_str;
	// cout << "key: " << key <<endl;

	// check whether key is in rev_usernames
	map<string, string>::iterator iter;

	/*
	for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
	{
		cout << "key: " << iter->first << " username: " << iter->second << endl;
	}
	*/

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end())
	{
		// send an error message saying not logged in
		send_error_message(sock, "Not logged in");
	}
	else
	{
		// cout << "key " << key << " found."<<endl;
		string username = rev_usernames[key];
		rev_usernames.erase(iter);

		// remove from usernames
		map<string, struct sockaddr_in>::iterator user_iter;
		user_iter = usernames.find(username);
		usernames.erase(user_iter);

		// remove from all the channels if found
		map<string, channel_type>::iterator channel_iter;
		for (channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			// cout << "key: " << iter->first << " username: " << iter->second << endl;
			// channel_type current_channel = channel_iter->second;
			map<string, struct sockaddr_in>::iterator within_channel_iterator;
			within_channel_iterator = channel_iter->second.find(username);
			if (within_channel_iterator != channel_iter->second.end())
			{
				channel_iter->second.erase(within_channel_iterator);
			}
		}

		// remove entry from active usernames also
		// active_usernames[username] = 1;
		map<string, int>::iterator active_user_iter;
		active_user_iter = active_usernames.find(username);
		active_usernames.erase(active_user_iter);

		cout << "server: " << username << " logs out" << endl;
	}

	/*
	for(iter = rev_usernames.begin(); iter != rev_usernames.end(); iter++)
	{
		cout << "key: " << iter->first << " username: " << iter->second << endl;
	}
	*/

	// if so delete it and delete username from usernames
	// if not send an error message - later
}

void handle_join_message(void *data, struct sockaddr_in sock)
{
	// get message fields
	struct request_join *msg;
	msg = (struct request_join *)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = ntohs(sock.sin_port);

	char port_str[6];
	memset(port_str, '\0', PORT_MAX);
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	// check whether key is in rev_usernames
	map<string, string>::iterator iter;

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end())
	{
		// ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		map<string, channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			// channel not found
			map<string, struct sockaddr_in> new_channel_users;
			new_channel_users[username] = sock;
			channels[channel] = new_channel_users;
			// cout << "creating new channel and joining" << endl;
		}
		else
		{
			// channel already exits
			// map<string,struct sockaddr_in>* existing_channel_users;
			// existing_channel_users = &channels[channel];
			//*existing_channel_users[username] = sock;

			channels[channel][username] = sock;
			// cout << "joining exisitng channel" << endl;
		}

		cout << server_hostname << ":" << server_port << " " << key << " recv Request join " << username << " " << channel << endl;
		broadcast_join_message(channel);
	}

	// check whether the user is in usernames
	// if yes check whether channel is in channels
	// if channel is there add user to the channel
	// if channel is not there add channel and add user to the channel
}

void handle_leave_message(void *data, struct sockaddr_in sock)
{

	// check whether the user is in usernames
	// if yes check whether channel is in channels
	// check whether the user is in the channel
	// if yes, remove user from channel
	// if not send an error message to the user

	// get message fields
	struct request_leave *msg;
	msg = (struct request_leave *)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = ntohs(sock.sin_port);

	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	// check whether key is in rev_usernames
	map<string, string>::iterator iter;

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end())
	{
		// ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in");
	}
	else
	{
		string username = rev_usernames[key];

		map<string, channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		active_usernames[username] = 1;

		if (channel_iter == channels.end())
		{
			// channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << server_hostname << ":" << server_port << " " << username <<" trying to leave non-existent channel "<< channel << endl;
		}
		else
		{
			// channel already exits
			// map<string,struct sockaddr_in> existing_channel_users;
			// existing_channel_users = channels[channel];
			map<string, struct sockaddr_in>::iterator channel_user_iter;
			channel_user_iter = channels[channel].find(username);

			if (channel_user_iter == channels[channel].end())
			{
				// user not in channel
				send_error_message(sock, "You are not in channel " + channel);
				cout << "server: " << username << " trying to leave channel " << channel << " where he/she is not a member" << endl;
				cout << server_hostname << ":" << server_port << " " << username <<" trying to leave channel "<< channel << " where he/she is not a member" << endl;
			}
			else
			{
				channels[channel].erase(channel_user_iter);
				// existing_channel_users.erase(channel_user_iter);
				
				cout << server_hostname << ":" << server_port << " " << username <<" leaves channel "<< channel<< endl;
				// delete channel if no more users
				if (channels[channel].empty() && (channel != "Common"))
				{
					channels.erase(channel_iter);
					cout << "server: "
						 << "removing empty channel " << channel << endl;
				}
			}
		}
	}
}

void handle_say_message(void *data, struct sockaddr_in sock)
{

	// check whether the user is in usernames
	// if yes check whether channel is in channels
	// check whether the user is in the channel
	// if yes send the message to all the members of the channel
	// if not send an error message to the user
	struct request_say *msg;
	msg = (struct request_say *)data;
	string channel = msg->req_channel;
	string text = msg->req_text;
	string ip = inet_ntoa(sock.sin_addr);
	int port = ntohs(sock.sin_port);
	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;
	// check whether key is in rev_usernames
	map<string, string>::iterator iter;
	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end())
	{
		// ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
		return;
	}
	string username = rev_usernames[key];
	map<string, channel_type>::iterator channel_iter;
	channel_iter = channels.find(channel);
	active_usernames[username] = 1;
	if (channel_iter == channels.end())
	{
		// channel not found
		send_error_message(sock, "No channel by the name " + channel);
		cout << "server: " << username << " trying to send a message to non-existent channel " << channel << endl;
		return;
	}
	map<string, struct sockaddr_in>::iterator channel_user_iter;
	channel_user_iter = channels[channel].find(username);
	if (channel_user_iter == channels[channel].end())
	{
		// user not in channel
		send_error_message(sock, "You are not in channel " + channel);
		//cout << "server: " << username << " trying to send a message to channel " << channel << " where he/she is not a member" << endl;
		cout << server_hostname << ":" << server_port << " " << username <<" trying to send a message to channel " << channel << " where he/she is not a member" << endl;
		return;
	}

	if(broadcast_say_message_to_clients(text, channel, username) > -1)
		cout << server_hostname << ":" << server_port << " " << key << " recv Request " << username << " sends message in " <<channel<< endl;
	send_ss_say_message(text, username, channel, 0);
}

int broadcast_say_message_to_clients(string text, string channel, string username){
	
	map<string, struct sockaddr_in> existing_channel_users;
	existing_channel_users = channels[channel];

	map<string, struct sockaddr_in>::iterator channel_user_iter;
	channel_user_iter = channels[channel].find(username);

	for (channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
	{
		ssize_t bytes;
		void *send_data;
		size_t len;

		struct text_say send_msg;
		send_msg.txt_type = TXT_SAY;
		const char *str = channel.c_str();
		strcpy(send_msg.txt_channel, str);
		str = username.c_str();
		strcpy(send_msg.txt_username, str);
		str = text.c_str();
		strcpy(send_msg.txt_text, str);
		// send_msg.txt_username, *username.c_str();
		// send_msg.txt_text,*text.c_str();
		send_data = &send_msg;

		len = sizeof send_msg;

		// cout << username <<endl;
		struct sockaddr_in send_sock = channel_user_iter->second;

		// bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
		bytes = sendto(s, send_data, len, 0, (struct sockaddr *)&send_sock, sizeof send_sock);

		if (bytes < 0)
			return -1;
		
	}
	return 0;
}

void handle_list_message(struct sockaddr_in sock)
{

	// check whether the user is in usernames
	// if yes, send a list of channels
	// if not send an error message to the user

	string ip = inet_ntoa(sock.sin_addr);

	int port = ntohs(sock.sin_port);

	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	// check whether key is in rev_usernames
	map<string, string>::iterator iter;

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end())
	{
		// ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];
		int size = channels.size();
		// cout << "size: " << size << endl;

		active_usernames[username] = 1;

		ssize_t bytes;
		void *send_data;
		size_t len;

		// struct text_list temp;
		struct text_list *send_msg = (struct text_list *)malloc(sizeof(struct text_list) + (size * sizeof(struct channel_info)));

		send_msg->txt_type = TXT_LIST;

		send_msg->txt_nchannels = size;

		map<string, channel_type>::iterator channel_iter;

		// struct channel_info current_channels[size];
		// send_msg.txt_channels = new struct channel_info[size];
		int pos = 0;

		for (channel_iter = channels.begin(); channel_iter != channels.end(); channel_iter++)
		{
			string current_channel = channel_iter->first;
			const char *str = current_channel.c_str();
			// strcpy(current_channels[pos].ch_channel, str);
			// cout << "channel " << str <<endl;
			strcpy(((send_msg->txt_channels) + pos)->ch_channel, str);
			// strcpy(((send_msg->txt_channels)+pos)->ch_channel, "hello");
			// cout << ((send_msg->txt_channels)+pos)->ch_channel << endl;

			pos++;
		}

		// send_msg.txt_channels =
		// send_msg.txt_channels = current_channels;
		send_data = send_msg;
		len = sizeof(struct text_list) + (size * sizeof(struct channel_info));

		// cout << username <<endl;
		struct sockaddr_in send_sock = sock;

		// bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
		bytes = sendto(s, send_data, len, 0, (struct sockaddr *)&send_sock, sizeof send_sock);

		if (bytes < 0)
		{
			perror("Message failed\n"); // error
		}
		else
		{
			// printf("Message sent\n");
		}

		//cout << "server: " << username << " lists channels" << endl;
		cout << server_hostname << ":" << server_port << " " << username << " lists channels" << endl;
		
	}
}

void handle_who_message(void *data, struct sockaddr_in sock)
{

	// check whether the user is in usernames
	// if yes check whether channel is in channels
	// if yes, send user list in the channel
	// if not send an error message to the user

	// get message fields
	struct request_who *msg;
	msg = (struct request_who *)data;

	string channel = msg->req_channel;

	string ip = inet_ntoa(sock.sin_addr);

	int port = ntohs(sock.sin_port);

	char port_str[6];
	sprintf(port_str, "%d", port);
	string key = ip + ":" + port_str;

	// check whether key is in rev_usernames
	map<string, string>::iterator iter;

	iter = rev_usernames.find(key);
	if (iter == rev_usernames.end())
	{
		// ip+port not recognized - send an error message
		send_error_message(sock, "Not logged in ");
	}
	else
	{
		string username = rev_usernames[key];

		active_usernames[username] = 1;

		map<string, channel_type>::iterator channel_iter;

		channel_iter = channels.find(channel);

		if (channel_iter == channels.end())
		{
			// channel not found
			send_error_message(sock, "No channel by the name " + channel);
			cout << server_hostname << ":" << server_port << " " << username << " trying to list users in non-existing channel " << channel << endl;
			
		}
		else
		{
			// channel exits
			map<string, struct sockaddr_in> existing_channel_users;
			existing_channel_users = channels[channel];
			int size = existing_channel_users.size();

			ssize_t bytes;
			void *send_data;
			size_t len;

			// struct text_list temp;
			struct text_who *send_msg = (struct text_who *)malloc(sizeof(struct text_who) + (size * sizeof(struct user_info)));

			send_msg->txt_type = TXT_WHO;

			send_msg->txt_nusernames = size;

			const char *str = channel.c_str();

			strcpy(send_msg->txt_channel, str);

			map<string, struct sockaddr_in>::iterator channel_user_iter;

			int pos = 0;

			for (channel_user_iter = existing_channel_users.begin(); channel_user_iter != existing_channel_users.end(); channel_user_iter++)
			{
				string username = channel_user_iter->first;

				str = username.c_str();

				strcpy(((send_msg->txt_users) + pos)->us_username, str);

				pos++;
			}

			send_data = send_msg;
			len = sizeof(struct text_who) + (size * sizeof(struct user_info));

			// cout << username <<endl;
			struct sockaddr_in send_sock = sock;

			// bytes = sendto(s, send_data, len, 0, (struct sockaddr*)&send_sock, fromlen);
			bytes = sendto(s, send_data, len, 0, (struct sockaddr *)&send_sock, sizeof send_sock);

			if (bytes < 0)
			{
				perror("Message failed\n"); // error
			}
			else
			{
				// printf("Message sent\n");
			}

			cout << server_hostname << ":" << server_port << " " << username  << " lists users in channnel " << channel << endl;


		}
	}
}

void send_error_message(struct sockaddr_in sock, string error_msg)
{
	ssize_t bytes;
	void *send_data;
	size_t len;

	struct text_error send_msg;
	send_msg.txt_type = TXT_ERROR;

	const char *str = error_msg.c_str();
	strcpy(send_msg.txt_error, str);

	send_data = &send_msg;

	len = sizeof send_msg;

	struct sockaddr_in send_sock = sock;

	bytes = sendto(s, send_data, len, 0, (struct sockaddr *)&send_sock, sizeof send_sock);

	if (bytes < 0)
	{
		perror("Message failed\n"); // error
	}
	else
	{
		// printf("Message sent\n");
	}
}
