
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <errno.h>
#include <sys/dir.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <time.h>
#include <openssl/evp.h>

#define BACKLOG 10
#define MAXLINE 200

int peer_handler(int);
void handle_list_req(int);
void handle_createtracker_req(int, char*);
void handle_get_req(int, char*);
void handle_update_tracker_req(int, char*);
int calculate_md5(const char *path, char *md5_text);

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
	if((!strcmp(read_msg, "<REQ LIST>"))||(!strcmp(read_msg, "req list"))||(!strcmp(read_msg, "<REQ LIST>"))||(!strcmp(read_msg, "<REQ LIST>\n"))){//list command received
		handle_list_req(sock_child);// handle list request
		printf("list request handled.\n");
	}

	else if((strstr(read_msg,"get")!=NULL)||(strstr(read_msg,"GET")!=NULL))
	{// get command received		
		handle_get_req(sock_child, read_msg);		
	}

	else if((strstr(read_msg,"createtracker")!=NULL)||(strstr(read_msg,"Createtracker")!=NULL)||(strstr(read_msg,"CREATETRACKER")!=NULL))
	{// get command received
		handle_createtracker_req(sock_child, read_msg);
		
	}
	else if((strstr(read_msg,"updatetracker")!=NULL)||(strstr(read_msg,"Updatetracker")!=NULL)||(strstr(read_msg,"UPDATETRACKER")!=NULL)){// get command received
		handle_update_tracker_req(sock_child, read_msg);		
	}
	
}//end client handler function

//handles the peer requesting the available files
void handle_list_req(int child_socket)
{
	DIR *dir;
	struct dirent * entry;
	char body[8192] = "";
	int body_len = 0;
	int count = 0;

	//opens the torrents directory
	dir = opendir("torrents");
    if (dir == NULL)
	{
		printf("Cannot open torrents directory\n");
		return;
	}
	else
	{
		//iteratively read each tracker file in the list
		while ((entry = readdir(dir)) != NULL)
		{
			//skip anything that does not end with .track
			char *ext = strrchr(entry->d_name, '.');
			if (ext == NULL || strcmp(ext, ".track") != 0)
			{
				continue;
			}
			char path[512];

			snprintf(path, sizeof(path), "torrents/%s", entry->d_name);

			FILE *fp = fopen(path, "r");

			if (fp == NULL) {printf("Cannot open %s\n", path); continue;}

			char fname[MAXLINE], desc[MAXLINE], md5[64];
			long fsize;

			int got = fscanf(fp, "Filename: %199s Filesize: %ld Description: %199s MD5: %63s", fname, &fsize, desc, md5);

			fclose(fp);

			if (got != 4)
			{
				printf("Bad tracker file %s\n", path);
				continue;
			}

			count++;
			body_len += snprintf(body + body_len, sizeof(body) - body_len,
			                     "<%d %s %ld %s>\n", count, fname, fsize, md5);
		}
	}
	closedir(dir);
	char buf[8300];
	int send_size = snprintf(buf, sizeof(buf), "<REP LIST %d>\n%s<REP LIST END>\n", count, body);
	write(child_socket, buf, strlen(buf));
}

void handle_createtracker_req (int child_soc, char * request)
{
	char fname[200];
	char desc[200];
	char md5[200];
	char ip[16];
	long fsize;
	int port;

	int got = sscanf(request,
		"<createtracker %199s %ld %199s %32s %15s %d",
		fname, &fsize, desc, md5, ip, &port);        //extracs the important values from the request and returns how many were succesfully extracted
	
	if (got != 6) //makes sure all variables were extracted
	{
		char* reply = "<createtracker fail>\n";
		write(child_soc, reply, strlen(reply));
		return;
	}

	printf("Filename: %s\n", fname);
	printf("description: %s\n", desc);
	printf("md5: %s\n", md5);
	printf("File Size: %ld\n", fsize);
	printf("Peer: %s:%d\n", ip, port);

	char path[512];
	snprintf(path, sizeof(path), "torrents/%s.track", fname);

	FILE *fp = fopen(path, "wx"); //create a new file for writing but fail if it already exists

	if (fp == NULL)
	{
		char *reply;

		if (errno == EEXIST)
		{
			reply = "<createtracker ferr>\n";
		}
		else
		{
			reply = "<createtracker fail>\n";
		}

		write(child_soc, reply, strlen(reply));
	}

	long timestamp = (long)time(NULL);

	int result = fprintf(fp,
		"Filename: %s\n"
		"Filesize: %ld\n"
		"Description: %s\n"
		"MD5: %s\n"
		"%s:%d:0:%ld:%ld\n",
		fname, fsize, desc, md5,
		ip, port, fsize, timestamp);

	int close_resuslt = fclose(fp);

	char* reply;

	if (result < 0 || close_resuslt != 0)
	{
		reply = "<createracker fail>\n";
	}
	else
	{
		reply = "<createtracker succ>\n";
	}

	write(child_soc, reply, strlen(reply));
}

void handle_get_req(int child_soc, char* get_message)
{
	char fnamewext[200];
	int got = sscanf(get_message, "<GET %199[^> \t\r\n]", fnamewext); //extracts the file name with the extentsion out of the request

	if (got != 1)
	{
		printf("Invalid GET request\n");
		return;
	}

	printf("File name with Extension: %s\n", fnamewext);

	DIR *dir;
	struct dirent * entry;

	dir = opendir("torrents");

	if (dir == NULL)
	{
		printf("Cannot open torrents directory\n");
		return;
	}
	else
	{
		while ((entry = readdir(dir)) != NULL)
		{
			printf("Current File in iteration: %s\n", entry -> d_name);
			if (!strcmp(fnamewext, entry -> d_name)) //found matchin file
			{
				//return the files contents and compute the checksum for it
				char path[512];
				snprintf(path, sizeof(path), "torrents/%s", entry -> d_name);
				FILE * return_file = fopen(path, "r");

				if (return_file == NULL)
				{
					perror("Cannot open tracker file");
					closedir(dir);
					return;
				}

				char line[512];
				char md5_text[33];

				if (calculate_md5(path, md5_text) == 0)
				{
					printf("Cannot calculate tracker checksum\n");
					fclose(return_file);
					closedir(dir);
					return;
				}

				write(child_soc, "<REP GET BEGIN>\n", 16);
				while (fgets(line, sizeof(line), return_file) != NULL)
				{
					if (write(child_soc, line, strlen(line)) < 0)
					{
						perror("Cannot send file contents");
						break;
					}
				}
				char end_message[80];
				snprintf(end_message, sizeof(end_message), "<REP GET END %s>\n", md5_text);
				write(child_soc, end_message, strlen(end_message));

				fclose(return_file);
			}
		}

		closedir(dir);
	}
}

void handle_update_tracker_req(int child_soc, char* get_message)
{
	char fname[200];
	long start_bytes;
	long end_bytes;
	char ip_address[40];
	int port;

	int got = sscanf(get_message, "<updatetracker %199s %ld %ld %39s %d", fname, &start_bytes, &end_bytes, ip_address, &port);

	if (got != 5)
	{
		printf("Invalid updatetracker request\n");
		return;
	}

	char fail_reply[256];
	snprintf(fail_reply, sizeof(fail_reply), "<updatetracker %s fail>\n", fname);

	DIR * dir;
	struct dirent *entry; 
	dir = opendir("torrents");
	
	if (dir == NULL)
	{
		printf("Could not open the directory\n");
		write(child_soc, fail_reply, strlen(fail_reply));
		return;
	}

	else
	{
		char tracker_name[256];
		snprintf(tracker_name, sizeof(tracker_name), "%s.track", fname);
		int tracker_found = 0;
		while ((entry = readdir(dir)) != NULL)
		{
			if(!strcmp(entry->d_name, tracker_name))
			 //file name found
			{
				tracker_found = 1;
			 	char path[512];
			 	snprintf(path, sizeof(path), "torrents/%s", entry->d_name);
				FILE * update_file = fopen(path, "r");

				if (update_file == NULL)
				{
					perror("Cannot open tracker file");
					closedir(dir);
					write(child_soc, fail_reply, strlen(fail_reply));
					return;
				}

				char temp_path[600];
				snprintf(temp_path, sizeof(temp_path), "%s.%ld.tmp", path, (long)getpid());
				FILE *temp_file = fopen(temp_path, "w");

				if (temp_file == NULL)
				{
					perror("Cannot create temporary tracker file");
					fclose(update_file);
					closedir(dir);
					write(child_soc, fail_reply, strlen(fail_reply));
					return;
				}

				int peer_found = 0;
				long timestamp = (long)time(NULL);

				char line[512];
				while (fgets(line, sizeof(line), update_file) != NULL)
				{
					char saved_ip[40];
					int saved_port;
					long saved_start, saved_end, saved_time;

					int fields = sscanf(line, "%39[^:]:%d:%ld:%ld:%ld", saved_ip, &saved_port, &saved_start, &saved_end, &saved_time);

					if (fields == 5 && strcmp(saved_ip, ip_address) == 0 && saved_port == port)
					{
						fprintf(temp_file, "%s:%d:%ld:%ld:%ld\n", ip_address, port, start_bytes, end_bytes, timestamp);
						peer_found = 1;
					}
					else
					{
						fputs(line, temp_file);
					}
				}

				if (peer_found == 0)
				{
					fprintf(temp_file, "%s:%d:%ld:%ld:%ld\n", ip_address, port, start_bytes, end_bytes, timestamp);
				}

				int failed = ferror(update_file) || ferror(temp_file);
				if (fclose(update_file) != 0)
				{
					failed = 1;
				}
				if (fclose(temp_file) != 0)
				{
					failed = 1;
				}

				if (failed)
				{
					printf("Could not read or write tracker file\n");
					remove(temp_path);
					closedir(dir);
					write(child_soc, fail_reply, strlen(fail_reply));
					return;
				}

				if (rename(temp_path, path) != 0)
				{
					remove(temp_path);
					closedir(dir);
					write(child_soc, fail_reply, strlen(fail_reply));
					return;
				}

				char reply[256];
				snprintf(reply, sizeof(reply), "<updatetracker %s succ>\n", fname);
				write(child_soc, reply, strlen(reply));

				break;
			}
		}
		closedir(dir);

		if (tracker_found == 0)
		{
			char reply[256];
			snprintf(reply, sizeof(reply), "<updatetracker %s ferr>\n", fname);
			write (child_soc, reply, strlen(reply));
		}
	}
}


int calculate_md5(const char *path, char *md5_text)
{
	FILE *fp = fopen(path, "rb");

    if (fp == NULL)
    {
        perror("Cannot open file for checksum");
        return 0;
    }

    // The checksum calculation will go here.
	EVP_MD_CTX *ctx = EVP_MD_CTX_new();

	if (ctx == NULL)
	{
		printf("Cannot create checksum context\n");
		fclose(fp);
		return 0;
	}

	if (EVP_DigestInit_ex(ctx, EVP_md5(), NULL) != 1)
	{
		printf("Cannot initialize MD5\n");
		EVP_MD_CTX_free(ctx);
		fclose(fp);
		return 0;
	}

	// We will read the file and calculate its checksum here.
	unsigned char buffer[1024];
	size_t bytes_read;

	while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0)
	{
		if (EVP_DigestUpdate(ctx, buffer, bytes_read) != 1)
		{
			printf("Cannot update checksum\n");
			EVP_MD_CTX_free(ctx);
			fclose(fp);
			return 0;
		}
	}

	if (ferror(fp))
	{
		printf("Cannot read file for checksum\n");
		EVP_MD_CTX_free(ctx);
		fclose(fp);
		return 0;
	}

	unsigned char digest[EVP_MAX_MD_SIZE];
	unsigned int digest_length;

	if (EVP_DigestFinal_ex(ctx, digest, &digest_length) != 1)
	{
		printf("Cannot finish checksum\n");
		EVP_MD_CTX_free(ctx);
		fclose(fp);
		return 0;
	}

	for (unsigned int i = 0; i < digest_length; i++)
	{
		snprintf(md5_text + i * 2, 3, "%02x",
				(unsigned int)digest[i]);
	}

    EVP_MD_CTX_free(ctx);
	fclose(fp);
    return 1;
}

