
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <arpa/inet.h>



#define TRACKER_SERVER_IP "10.173.185.74"
#define TRACKER_SERVER_PORT 3490 //should be in the config file

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
		char* list_req= "REQ LIST";
		if((write(sockid,"REQ LIST",strlen(list_req))) < 0){ //exclude null pointer because the server will add it
			printf("Send_request  failure\n"); exit(0);
		}
		printf("List request written to server\n");

        // if((read(sockid,(char *)&msg,sizeof(msg)))< 0){// read what server has said
		// 	printf("Read  failure\n"); exit(0); 
		// }
		
		close(sockid);
		printf("Connection closed\n");
	    exit(0);
	}//end close

    
}	     
  
