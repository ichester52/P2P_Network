#include <sys/stat.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
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

void load_server_config(const char *filename) {
	// create the file and open it for reading
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Could not open client config file: %s\n", filename);
        exit(1);
    }

    char buffer[100];
    
    // Line 1: Peer listening port
    if (fgets(buffer, sizeof(buffer), file) != NULL) {
        server_listen_port = atoi(buffer);
    }
    
    // Line 2: Shared folder name (stripping newline)
    if (fgets(buffer, sizeof(buffer), file) != NULL) {
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

void load_client_config(const char *filename) {
	// create the file and open it for reading
    FILE *file = fopen(filename, "r");
    if (file == NULL) {
        printf("Error: Could not open client config file: %s\n", filename);
        exit(1);
    }

    char buffer[100];
    
    // Line 1: Tracker Server Port (3490)
    if (fgets(buffer, sizeof(buffer), file) != NULL) {
        tracker_port = atoi(buffer);
    }
    
    // Line 2: Tracker Server IP Address (127.0.0.1)
    if (fgets(buffer, sizeof(buffer), file) != NULL) {
        buffer[strcspn(buffer, "\r\n")] = 0;
        strcpy(tracker_ip, buffer);
    }
    
    // Line 3: Periodic updatetracker interval (900)
    if (fgets(buffer, sizeof(buffer), file) != NULL) {
        update_interval = atoi(buffer);
    }

	// close file
    fclose(file);
}


// function to start the peer's listening server 

void start_peer_server(int port) {
	int server_fd, new_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);

	// create socket file descriptor
	if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("Peer server socket failed");
        exit(EXIT_FAILURE);
    }

	// attach socket to the port in the config files
	if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
        exit(EXIT_FAILURE);
    }

	address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY; // listen on any network interface
    address.sin_port = htons(port);       // uses the server_listen_port

	// bind socket to the port
	if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("Peer server bind failed");
        exit(EXIT_FAILURE);
    }

	// listen for incoming connections from other peers
	if (listen(server_fd, 3) < 0) {
        perror("Peer server listen failed");
        exit(EXIT_FAILURE);
    }

	printf("Peer server is successfully listening on port %d...\n", port);

	// accept part
	while (1) {
        // accept() pauses execution until another peer connects to the port
        if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen)) < 0) {
            perror("Server accept failed");
            continue; // Keep trying even if one connection fails
        }

        printf("Incoming connection accepted from another peer!\n");

        // Read what the connecting peer wants (e.g., a file request)
        char buffer[MAXLINE] = {0};
        int valread = read(new_socket, buffer, MAXLINE);
        if (valread > 0) {
            buffer[valread] = '\0';
            printf("Received message from peer: %s\n", buffer);
            
            // TODO: Handle what they want (like reading a file from PeerFolder and sending it back)
        }

        // Close the connection with this specific peer when done
        close(new_socket);
    }
}


int main(int argc,char *argv[]){
	
	// Load the configuration files (client and server)
    load_client_config("clientThreadConfig.cfg");
	load_server_config("serverThreadConfig.cfg");

	// check if the user wants to start the peer server listener (SO FAR ONLY LISTENS)
    if (argc > 1 && strcasecmp(argv[1], "server") == 0) {
        // This will launch the infinite accept() loop using the port from serverThreadConfig.cfg
        start_peer_server(server_listen_port);
        return 0;
    }

	
	struct sockaddr_in server_addr;
	int sockid;
        
	if ((sockid = socket(AF_INET,SOCK_STREAM,0))==-1) { //create socket
		printf("socket cannot be created\n"); exit(0);
	}
   
    server_addr.sin_family = AF_INET; //host byte order
    server_addr.sin_port = htons(tracker_port); // convert to network byte order - value from config file
	server_addr.sin_addr.s_addr = inet_addr(tracker_ip); // value from config file
	memset(&(server_addr.sin_zero), '\0', 8); // pad the rest of the struct with zeros

    if (connect(sockid ,(struct sockaddr *) &server_addr,sizeof(struct sockaddr))==-1) { //connect and error check. connect should auto assing a port number for the client to
		printf("Cannot connect to server\n"); exit(0);
	}

   /* If connected successfully*/
   printf("Socket Connected Successfully\n");
    
	if (argc > 1 && strcasecmp(argv[1], "list") == 0) { // if this is LIST command / and argument isn't empty
		printf("list request\n");
		char* list_req= "REQ LIST";

		if((write(sockid,"REQ LIST",strlen(list_req))) < 0){ //exclude null pointer because the server will add it
			printf("Send_request  failure\n"); exit(0);
		}

		printf("List request written to server\n");

		char msg[MAXLINE];
		int list_response_length = read(sockid,msg,MAXLINE);

        if(list_response_length < 0){// read what server has said
			printf("Read failure\n"); exit(0); 
		}

		//message was read succesfully
		msg[list_response_length] = '\0';

		printf("%s", msg);
		
		close(sockid);
		printf("Connection closed\n");
	    exit(0);
	} //end close

    // comment more later 
	else if (argc > 1 && strcasecmp(argv[1], "createtracker") == 0) { // create tracker command /  argument isn't empty
        if (argc < 5) {
            printf("Usage: ./peer createtracker <filename> <description> <md5>\n");
            close(sockid);
            exit(0);
        }

        char *filename = argv[2];
        char *description = argv[3];
        char *md5 = argv[4];

        // get file size from the shared folder specified in serverThreadConfig.cfg
        char filepath[256];
        snprintf(filepath, sizeof(filepath), "%s/%s", shared_folder, filename);
        
        struct stat st;
        if (stat(filepath, &st) != 0) {
            printf("Error: File '%s' not found in shared folder '%s'\n", filename, shared_folder);
            close(sockid);
            exit(0);
        }
        long filesize = st.st_size;

        // grab the peer's local IP address
        char peer_ip[64];
        int sock_ip = socket(AF_INET, SOCK_DGRAM, 0);

        if (sock_ip >= 0) {
            struct sockaddr_in serv;
            memset(&serv, 0, sizeof(serv));
            serv.sin_family = AF_INET;
            serv.sin_addr.s_addr = inet_addr(tracker_ip);
            serv.sin_port = htons(tracker_port);
            connect(sock_ip, (const struct sockaddr*)&serv, sizeof(serv));
            
            struct sockaddr_in name;
            socklen_t namelen = sizeof(name);
            getsockname(sock_ip, (struct sockaddr*)&name, &namelen);
            inet_ntop(AF_INET, &name.sin_addr, peer_ip, sizeof(peer_ip));
            close(sock_ip);
        } else {
            strcpy(peer_ip, "127.0.0.1"); // Fallback
        }

        // format protocol message: <createtracker filename filesize description md5 ip-address port-number>\n
        char message[1024];
        snprintf(message, sizeof(message), "<createtracker %s %ld %s %s %s %d>\n",
                 filename, filesize, description, md5, peer_ip, server_listen_port);

        // send to tracker server
        if (write(sockid, message, strlen(message)) < 0) {
            printf("Send createtracker failure\n");
            close(sockid);
            exit(0);
        }

        printf("Sent to tracker: %s", message);

        // read the response from tracker server (<createtracker succ>, <createtracker ferr>, etc.)
        char msg[MAXLINE];
        int response_len = read(sockid, msg, MAXLINE);
        if (response_len < 0) {
            printf("Read failure\n");
            close(sockid);
            exit(0);
        }

        msg[response_len] = '\0';
        printf("Tracker response: %s", msg);

        close(sockid);
        printf("Connection closed\n");
        exit(0);
    }
}	     