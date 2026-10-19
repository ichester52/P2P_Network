
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/dir.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define BACKLOG 10
#define MAXLINE 200

int peer_handler(int);
void handle_list_req(int);

int main(){
   pid_t pid;
   int sockid, sock_child;
   int server_port = 3490;
   struct sockaddr_in server_addr, client_addr;   
   if ((sockid = socket(AF_INET,SOCK_STREAM,0)) < 0){//create socket connection oriented
	   printf("socket cannot be created \n"); exit(0);  
   }
    
   //socket created at this stage
   //now associate the socket with local port to allow listening incoming connections
   server_addr.sin_family = AF_INET;// assign address family
   server_addr.sin_port = htons(server_port);//change server port to NETWORK BYTE ORDER
   server_addr.sin_addr.s_addr = htons(INADDR_ANY); // use defualt address
   memset(&(server_addr.sin_zero), '\0', 8);
    
   if (bind(sockid ,(struct sockaddr *) &server_addr,sizeof(server_addr)) ==-1){//bind and check error
	   printf("bind  failure\n"); exit(0); 
   }
    
   printf("Tracker SERVER READY TO LISTEN INCOMING REQUEST.... \n");
   if (listen(sockid, BACKLOG) < 0){ //(parent) process listens at sockid and check error
	   printf(" Tracker  SERVER CANNOT LISTEN\n"); exit(0);
   }                                        
   
   int sockaddr_size = sizeof(struct sockaddr_in);

   while(1) { //accept  connection from every requester client
	   if ((sock_child = accept(sockid ,(struct sockaddr *) &client_addr, &(sockaddr_size)))==-1){ /* accept connection and create a socket descriptor for actual work */
		   printf("Tracker Cannot accept...\n"); exit(0); 
	   }

	   printf("Connection accepted\n");

	   if ((pid=fork())==0){//New child process will serve the requester client. separate child will serve separate client
		   close(sockid);   //child does not need listener
		   peer_handler(sock_child);//child is serving the client.		   
		   close (sock_child);// printf("\n 1. closed");
		   exit(0);         // kill the process. child process all done with work
        }
	   close(sock_child);  // parent all done with client, only child will communicate with that client from now
	   
     }  //accept loop ends            
} // main fun ends  
     



int peer_handler(int sock_child){ // function for file transfer. child process will call this function     
    //start handiling client request
	printf("Peer handler reached\n");	
	int length;
	char read_msg[MAXLINE];
	length=read(sock_child,read_msg,MAXLINE);			
	read_msg[length]='\0';
	printf("read_msg: %s\n", read_msg);
	if((!strcmp(read_msg, "REQ LIST"))||(!strcmp(read_msg, "req list"))||(!strcmp(read_msg, "<REQ LIST>"))||(!strcmp(read_msg, "<REQ LIST>\n"))){//list command received
		handle_list_req(sock_child);// handle list request
		printf("list request handled.\n");
	}
	// else if((strstr(read_msg,"get")!=NULL)||(strstr(read_msg,"GET")!=NULL)){// get command received
	// 	xtrct_fname(read_msg, " ");// extract filename from the command		
	// 	handle_get_req(sock_child, fname);		
	// }
	// else if((strstr(read_msg,"createtracker")!=NULL)||(strstr(read_msg,"Createtracker")!=NULL)||(strstr(read_msg,"CREATETRACKER")!=NULL)){// get command received
	// 	tokenize_createmsg(read_msg);
	// 	handle_createtracker_req(sock_child);
		
	// }
	// else if((strstr(read_msg,"updatetracker")!=NULL)||(strstr(read_msg,"Updatetracker")!=NULL)||(strstr(read_msg,"UPDATETRACKER")!=NULL)){// get command received
	// 	tokenize_updatemsg(read_msg);
	// 	handle_updatetracker_req(sock_child);		
	// }
	
}//end client handler function

void handle_list_req(int child_socket)
{
	char *list_response = "<REP LIST> X\n"
						  "<1 file1name 2000 xccds>\n"
						  "<2 file2name file2MD5>\n";
	if (write(child_socket, list_response, strlen(list_response)) < 0)
	{
		printf("LIST RESPONSE FAILURE"); exit(0);
	}
}


