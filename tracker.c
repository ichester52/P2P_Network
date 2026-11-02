
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/dir.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <sys/stat.h>
#include <time.h>


#define BACKLOG 10
#define MAXLINE 200

int peer_handler(int);
void handle_list_req(int);

// for creating tracker 
// variables to hold parsed createtracker data
char t_filename[100];
char t_filesize[50];
char t_description[150];
char t_md5[100];
char t_ip[50];
char t_port[20];


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
   
	socklen_t sockaddr_size = sizeof(struct sockaddr_in); // address size of type socklen_t is expected

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


// function handling tokenization of the message "<createtracker movie1.avi 47 Ghost_and_the_Darkness,_DVD_Ri..."
// bsically takes raw string sent by peer and breaks it apart so the server can extract individual info from it
void tokenize_createmsg(char *msg) {
	msg[strcspn(msg, "\r\n")] = 0;

    char *token = strtok(msg, " "); // skips "<createtracker"
    if (token != NULL) 
		token = strtok(NULL, " "); // skips the space
    if (token != NULL) 
		strcpy(t_filename, token); // grabs the filename

    token = strtok(NULL, " ");
    if (token != NULL) 
		strcpy(t_filesize, token); // grabs the file size

    token = strtok(NULL, " ");
    if (token != NULL) 
		strcpy(t_description, token); // grabs the file description

    token = strtok(NULL, " ");
    if (token != NULL) 
		strcpy(t_md5, token); // grabs the MD5 hash

    token = strtok(NULL, " ");
    if (token != NULL) 
		strcpy(t_ip, token); // grabs the ip address

    token = strtok(NULL, ">"); // last token before closing bracket
    if (token != NULL) {
        // trim leading space 
        while (*token == ' ') 
			token++;
        strcpy(t_port, token);
    }
}

// handles the tracker server side of createtracker request

void handle_createtracker_req(int child_socket) {
	// intilize empty struct
    struct stat st = {0};

	// checks if the torrents directory exists, if it doesn't it creates one
    if (stat("torrents", &st) == -1) {
        mkdir("torrents", 0700); // 0700 - gives user rwx perms
    }

    // makes track filename: torrents/<filename>.track
    char track_filepath[256];
    snprintf(track_filepath, sizeof(track_filepath), "torrents/%s.track", t_filename);

    // checks for dplicates 
    if (access(track_filepath, F_OK) != -1) {
        char *err_response = "<createtracker ferr>\n";
        write(child_socket, err_response, strlen(err_response));
        printf("Tracker file already exists: %s\n", track_filepath);
        return;
    }

    // create / write the tracker file in the specified format from project:
    // Filename: <file-name>
    // Filesize: <size>
    // Description: <description>
    // MD5: <md5>
    // #list of peers follows next
    // ip:port:start_byte:end_byte:timestamp

	// open the file for writing
    FILE *f = fopen(track_filepath, "w");
    if (f == NULL) {
        char *fail_response = "<createtracker fail>\n";
        write(child_socket, fail_response, strlen(fail_response));
        return;
    }

	// formt specified above 
    fprintf(f, "Filename: %s\n", t_filename);
    fprintf(f, "Filesize: %s\n", t_filesize);
    fprintf(f, "Description: %s\n", t_description);
    fprintf(f, "MD5: %s\n", t_md5);
    fprintf(f, "#list of peers follows next\n");
    
    // time stuff
    long timestamp = (long)time(NULL);
    fprintf(f, "%s:%s:0:%s:%ld\n", t_ip, t_port, t_filesize, timestamp);

    fclose(f);

    // Send success response: <createtracker succ>\n
    char *succ_response = "<createtracker succ>\n";
    write(child_socket, succ_response, strlen(succ_response)); // write back to peer (someone gotta implement the child stuff)
    printf("Tracker file created successfully: %s\n", track_filepath);
}

int peer_handler(int sock_child){     
    // start handiling client request
	printf("Peer handler reached\n");	

	int length;
	char read_msg[MAXLINE];

	length = read(sock_child,read_msg,MAXLINE);			
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
	else if((strstr(read_msg,"createtracker")!=NULL)||(strstr(read_msg,"Createtracker")!=NULL)||(strstr(read_msg,"CREATETRACKER")!=NULL)){// get command received
	 	tokenize_createmsg(read_msg);
	 	handle_createtracker_req(sock_child);
	}
	// else if((strstr(read_msg,"updatetracker")!=NULL)||(strstr(read_msg,"Updatetracker")!=NULL)||(strstr(read_msg,"UPDATETRACKER")!=NULL)){// get command received
	// 	tokenize_updatemsg(read_msg);
	// 	handle_updatetracker_req(sock_child);		
	// }

	return 0; // for now to handle the warning of non-void function returning a value
	
} //end client handler function

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


