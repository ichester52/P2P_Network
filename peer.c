
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>



#define TRACKER_SERVER_IP "192.168.5.73"
#define PEER_SERVER_IP "192.168.5.73"
#define TRACKER_SERVER_PORT 3490 //should be in the config file
#define MAXLINE 200

int main(int argc,char *argv[]){
	struct sockaddr_in server_addr;
	int sockid;
        
	if ((sockid = socket(AF_INET,SOCK_STREAM,0))==-1){//create socket
		printf("socket cannot be created\n"); exit(0);
	}
   
    server_addr.sin_family = AF_INET;//host byte order
    server_addr.sin_port = htons(TRACKER_SERVER_PORT);// convert to network byte order
	server_addr.sin_addr.s_addr = inet_addr(TRACKER_SERVER_IP);
	memset(&(server_addr.sin_zero), '\0', 8); // pad the rest of the struct with zeros
    if (connect(sockid ,(struct sockaddr *) &server_addr,sizeof(struct sockaddr))==-1){//connect and error check. connect should auto assing a port number for the client to
		printf("Cannot connect to server\n"); exit(0);
	}

   /* If connected successfully*/
   printf("Socket Connected Successfully\n");
    
	if(!strcmp(argv[1],"list")){// if this is LIST command
		printf("list request\n");
		char* list_req= "<REQ LIST>";
		if((write(sockid,"<REQ LIST>",strlen(list_req))) < 0){ //exclude null pointer because the server will add it
			printf("Send_request  failure\n"); exit(0);
		}
		printf("List request written to server\n");

		char msg[MAXLINE];
		int list_response_length = read(sockid,msg,MAXLINE);

        if(list_response_length < 0){// read what server has said
			printf("Read  failure\n"); exit(0); 
		}

		//message was read succesfully
		msg[list_response_length] = '\0';

		printf("%s", msg);

		
		close(sockid);
		printf("Connection closed\n");
	    exit(0);
	}

	else if (!strcmp(argv[1], "create"))
	{
		printf("Create Tracker Message Sending...\n");
		char* create_req = "<createtracker test_filename.avi 100400 Test_description 3d3434samsdxxasdada3232, 10.160.182.115 3123>\n";
		if (write(sockid, create_req, strlen(create_req)) < 0)
		{
			printf("Send_request failure\n"); exit(0);
		}

		printf("Create Request Succefully Sent\n");

		char msg[MAXLINE];

		int response_length = read(sockid, msg, MAXLINE);

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
		char* get_req_example = "<GET test_filename.avi.track>\n";
		if (write(sockid, get_req_example, strlen(get_req_example)) < 0)
		{
			printf("Failed to write the get request to server\n"); exit(0);
		}

		printf("Get request was sent\n");

		char get_response[1000];

		int read_bytes = read(sockid, get_response, sizeof(get_response) - 1);

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
		printf("Connection closed");
		exit(0);

	}

	else if (!strcmp(argv[1], "update"))
	{
		char *ip = PEER_SERVER_IP;
		char update_example[200];
		snprintf(update_example, sizeof(update_example), "<updatetracker test.txt 2848 4200 %s 1234>\n", ip);

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

		close(sockid);
		printf("Connection Closed\n");
	}
	
	// else 
	// {
	// 	close(sockid);
	// }//end close

    
}	     