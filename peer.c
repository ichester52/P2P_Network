#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <arpa/inet.h>

// Configuration variables to replace the previus hardcoded #define values
char tracker_ip[50];
int tracker_port;
int update_interval;

// Server configuration variables
int server_listen_port;
char shared_folder[100];

#define MAXLINE 200

/** Loads the server configuration files. Reads serverThreadConfig.cfg to get peer's listening port
 */

void load_server_config(const char *filename)
{
	// create the file and open it for reading
	FILE *file = fopen(filename, "r");
	if (file == NULL)
	{
		printf("Error: Could not open client config file: %s\n", filename);
		exit(1);
	}

	char buffer[100];

	// Line 1: Peer listening port
	if (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		server_listen_port = atoi(buffer);
	}

	// Line 2: Shared folder name (stripping newline)
	if (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		buffer[strcspn(buffer, "\r\n")] = 0;
		strcpy(shared_folder, buffer);
	}

	printf("Server config loaded successfully (Listening Port: %d, Shared Folder: %s)\n",
		   server_listen_port, shared_folder);

	// close file
	fclose(file);
}

/** Loads the clent configuration files. Reads clientThreadConfig.cfg to get tracker server details
 *  and the periodic updatetracker interval.
 */

void load_client_config(const char *filename)
{
	// create the file and open it for reading
	FILE *file = fopen(filename, "r");
	if (file == NULL)
	{
		printf("Error: Could not open client config file: %s\n", filename);
		exit(1);
	}

	char buffer[100];

	// Line 1: Tracker Server Port (3490)
	if (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		tracker_port = atoi(buffer);
	}

	// Line 2: Tracker Server IP Address (127.0.0.1)
	if (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		buffer[strcspn(buffer, "\r\n")] = 0;
		strcpy(tracker_ip, buffer);
	}

	// Line 3: Periodic updatetracker interval (900)
	if (fgets(buffer, sizeof(buffer), file) != NULL)
	{
		update_interval = atoi(buffer);
	}

	// close file
	fclose(file);
}

// function to start the peer's listening server

void start_peer_server(int port)
{
	int server_fd, new_socket;
	struct sockaddr_in address;
	int opt = 1;
	int addrlen = sizeof(address);

	// create socket file descriptor
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
	{
		perror("Peer server socket failed");
		exit(EXIT_FAILURE);
	}

	// attach socket to the port in the config files
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)))
	{
		perror("setsockopt failed");
		exit(EXIT_FAILURE);
	}

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY; // listen on any network interface
	address.sin_port = htons(port);		  // uses the server_listen_port

	// bind socket to the port
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
	{
		perror("Peer server bind failed");
		exit(EXIT_FAILURE);
	}

	// listen for incoming connections from other peers
	if (listen(server_fd, 3) < 0)
	{
		perror("Peer server listen failed");
		exit(EXIT_FAILURE);
	}

	printf("Peer server is successfully listening on port %d...\n", port);

	// accept part
	while (1)
	{
		// accept() pauses execution until another peer connects to the port
		if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
		{
			perror("Server accept failed");
			continue; // Keep trying even if one connection fails
		}

		printf("Incoming connection accepted from another peer!\n");

		// Read what the connecting peer wants (e.g., a file request)
		char buffer[MAXLINE] = {0};
		int valread = read(new_socket, buffer, MAXLINE);
		if (valread > 0)
		{
			buffer[valread] = '\0';
			printf("Received message from peer: %s\n", buffer);

			// TODO: Handle what they want (like reading a file from PeerFolder and sending it back)
		}

		// Close the connection with this specific peer when done
		close(new_socket);
	}
}

int main(int argc, char *argv[])
{
	if (argc < 2)
	{
		printf("Usage: ./peer list|create|get|update|server\n");
		return 1;
	}

	// Temporary example address for testing peers on this same machine.
	const char *peer_ip = "127.0.0.1";

	// Load the configuration files (client and server)
	load_client_config("clientThreadConfig.cfg");
	load_server_config("serverThreadConfig.cfg");

	// check if the user wants to start the peer server listener (SO FAR ONLY LISTENS)
	if (argc > 1 && strcasecmp(argv[1], "server") == 0)
	{
		// This will launch the infinite accept() loop using the port from serverThreadConfig.cfg
		start_peer_server(server_listen_port);
		return 0;
	}

	struct sockaddr_in server_addr;
	int sockid;

	if ((sockid = socket(AF_INET, SOCK_STREAM, 0)) == -1)
	{ // create socket
		printf("socket cannot be created\n");
		exit(0);
	}

	server_addr.sin_family = AF_INET;					 // host byte order
	server_addr.sin_port = htons(tracker_port);			 // convert to network byte order - value from config file
	server_addr.sin_addr.s_addr = inet_addr(tracker_ip); // value from config file
	memset(&(server_addr.sin_zero), '\0', 8);			 // pad the rest of the struct with zeros

	if (connect(sockid, (struct sockaddr *)&server_addr, sizeof(struct sockaddr)) == -1)
	{ // connect and error check. connect should auto assing a port number for the client to
		printf("Cannot connect to server\n");
		exit(0);
	}

	/* If connected successfully*/
	printf("Socket Connected Successfully\n");

	if(!strcmp(argv[1],"list")){// if this is LIST command
		printf("list request\n");
		char* list_req= "<REQ LIST>\n";
		if((write(sockid,list_req,strlen(list_req))) < 0){ // Send text without the null terminator.
			printf("Send_request  failure\n"); exit(0);
		}
		printf("List request written to server\n");

		char msg[MAXLINE];
		ssize_t list_response_length;
		while ((list_response_length = read(sockid, msg, sizeof(msg) - 1)) > 0)
		{
			msg[list_response_length] = '\0';
			printf("%s", msg);
		}

        if(list_response_length < 0){// read what server has said
			printf("Read  failure\n"); exit(0); 
		}

		
		close(sockid);
		printf("Connection closed\n");
	    exit(0);
	}

	else if (!strcmp(argv[1], "create"))
	{
		printf("Create Tracker Message Sending...\n");
		// Example metadata for testing requests; this is not a real file checksum.
		char create_req[256];
		snprintf(create_req, sizeof(create_req),
			"<createtracker test_filename.avi 100400 Test_description 00000000000000000000000000000000 %s %d>\n",
			peer_ip, server_listen_port);
		if (write(sockid, create_req, strlen(create_req)) < 0)
		{
			printf("Send_request failure\n"); exit(0);
		}

		printf("Create Request Succefully Sent\n");

		char msg[MAXLINE];

		int response_length = read(sockid, msg, sizeof(msg) - 1);

		if (response_length < 0)
		{
			printf("Read Failure\n"); exit(0);
		}

		msg[response_length] = '\0';

		printf("Response: %s\n", msg);

		close(sockid);
		printf("Connection Closed\n");
		exit(0);
	}
	else if (!strcmp(argv[1], "get"))
	{
		char* get_req_example = "<GET test_filename.avi.track >\n";
		if (write(sockid, get_req_example, strlen(get_req_example)) < 0)
		{
			printf("Failed to write the get request to server\n"); exit(0);
		}

		printf("Get request was sent\n");

		char get_response[1000];

		ssize_t read_bytes;

		while ((read_bytes = read(sockid, get_response, sizeof(get_response) - 1)) > 0)
		{
			get_response[read_bytes] = '\0';
			printf("%s", get_response);
		}

		if (read_bytes < 0)
		{
			perror("Read failure");
		}

		close(sockid);
		printf("Connection closed\n");
		exit(0);

	}

	else if (!strcmp(argv[1], "update"))
	{
		char update_example[200];
		snprintf(update_example, sizeof(update_example),
			"<updatetracker test_filename.avi 2848 4200 %s %d>\n",
			peer_ip, server_listen_port);

		if (write(sockid, update_example, strlen(update_example)) < 0)
		{
			printf("update request failed\n");
			exit(0);
		}
		
		printf("Update Request sent Succefully\n");

		char read_response[MAXLINE];

		int bytes_read = read(sockid, read_response, sizeof(read_response) -1);

		if (bytes_read < 0)
		{
			perror("Read Failure\n");
		}
		else
		{
			read_response[bytes_read] = '\0';
			printf("Tracker response: %s", read_response);
		}

		close(sockid);
		printf("Connection Closed\n");
	}

	// comment more later
	
}
