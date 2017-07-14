#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include "packet.h"
#include <time.h>

int main(int argc, char *argv[]){
  int sock_fd, port_no, bytes_recv = 0, total_bytes = 0, curr_seq = 0, base = 0, mem_loc = 0, contiguous = 0, num_pkts = MAX_SEQ / PACKET_SIZE, rwnd, c_control;
        int buffer_size =  1012 * num_pkts;
	struct sockaddr_in server_addr;
	struct hostent *server;
	struct packet request_pkt, recv_pkt, ack_pkt;
	socklen_t server_length;
	char *hostname, *filename;
	char buffer[buffer_size];
	int mem_filled [num_pkts];
	int mem_size [num_pkts];
	double p_loss, p_crpt;
	FILE *file;

	if(argc != 8 && argc != 7 && argc != 6){
		error("Usage: <hostname> <port_num> <file_name> <p_loss: 0 to 1> <p_corrupt: 0 to 1> [<rwnd in bytes>] [<congestion control: 1>]");
	}
	
	hostname = argv[1];
	port_no = atoi(argv[2]);
	filename = argv[3];
	p_loss = atof(argv[4]);
	p_crpt = atof(argv[5]);
	
	if(argc > 6)
		c_control = atoi(argv[6]);
	if(argc > 7)
		rwnd = atoi(argv[7]);
	else
		rwnd = 15360;
		
	server = gethostbyname(hostname);
	if(server == NULL){
	  error("Error: Host does not exist.");
	}
	
	sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (sock_fd < 0){
		error("Error: Cannot open socket.");
	}
	
	memset((char *) &server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	bcopy((char *)server->h_addr, (char *)&server_addr.sin_addr.s_addr, server->h_length);
	server_addr.sin_port = htons(port_no);
	server_length = sizeof(server_addr);
	
	//Make request to server
	memset((char *) &request_pkt, 0, sizeof(request_pkt));
	request_pkt.p_type = 0;
	request_pkt.seq_number = 0;
	strcpy(request_pkt.content, filename);
	request_pkt.length = strlen(filename) + 3 * sizeof(int);
		
	printf("Sending request %s to server\n", filename); //include details 
	if(sendto(sock_fd, &request_pkt, request_pkt.length, 0, (struct sockaddr*) &server_addr, server_length) < 0){
		error("Error: Cannot write to socket");
	}
     
	//Make ack packet 
	memset((char *) &ack_pkt, 0, sizeof(ack_pkt));
	ack_pkt.length = sizeof(int) * 3;
	
	//File to store requested data
      	file = fopen(strcat(filename, "_recv"), "wb");
	
	int i;
	for(i = 0; i < num_pkts; i++){
	  mem_filled[i] = 0;
	}

	for(i = 0; i < num_pkts; i++){
	  mem_size[i] = 0;
	}

	srand(time(NULL));

	int read = 1;
	int packet_counter = 0;

       	//Read data from server and send ACK
	while(read == 1){
	  bytes_recv = recvfrom(sock_fd, &recv_pkt, sizeof(recv_pkt), 0, (struct sockaddr *) &server_addr, &server_length);
		printf("\nwin %d \n\n", base);
		if(bytes_recv < 0){
		  printf("Error: Cannot read from server.");
		}
		
		//packet loss: do nothing
		else if((error_gen(p_loss) == 1) && (recv_pkt.p_type != 3)){
		  printf("Packet %d lost\n", recv_pkt.seq_number);
		}

		//packet corrupt: do nothing  	
		else if((error_gen(p_crpt) == 1) && (recv_pkt.p_type != 3)){
		  printf("Packet %d corrupted\n", recv_pkt.seq_number);
		}

		//packet ok
		else{
		  curr_seq = recv_pkt.seq_number; 

		  //packet_counter++;
		  
		  if (recv_pkt.p_type != 3) 
		    printf("Received packet %d from server. Bytes read: %d\n", curr_seq, recv_pkt.length);

		  if (recv_pkt.p_type == 3){
		    ack_pkt.p_type = 3;
		    ack_pkt.seq_number = curr_seq;
		   
		    if(sendto(sock_fd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*) &server_addr, server_length) < 0){
		      error("Error: Cannot send packet");
		    }
		    int pkt_size = sizeof(recv_pkt.content);
		    //  printf("Size of last data pack: %d\n", pkt_size);
		    printf("Received final ACK. File transfer complete. Receieved %d bytes in %d packets. Exiting.\n", total_bytes, packet_counter);
		    read = 0;
		  }

		  else if (( (curr_seq >= (base - rwnd - PACKET_SIZE)  ) && (curr_seq <= (base + rwnd - PACKET_SIZE) )) || (curr_seq <= (base + rwnd - PACKET_SIZE) % MAX_SEQ) || ( curr_seq >= (base + MAX_SEQ - rwnd - PACKET_SIZE))) {
				
				ack_pkt.p_type = 1;

				if(c_control == 1)
				  ack_pkt.seq_number = base;
				
				else
				  ack_pkt.seq_number = curr_seq;
				
				if(sendto(sock_fd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*) &server_addr, server_length) < 0)
				  error("Error: Cannot send packet");

				printf("Sent ACK %d to server\n", ack_pkt.seq_number); //include details
							
				if (curr_seq == base){ // write to file, including buffer
				  //fwrite(recv_pkt.content, 1, recv_pkt.length, file);
				  // base = (base + PACKET_SIZE) % MAX_SEQ;
				  memcpy(&buffer[mem_loc], &recv_pkt.content, recv_pkt.length);
				  mem_size[base / PACKET_SIZE] = recv_pkt.length;
				  printf("Copied %d bytes from packet %d data to buffer\n", recv_pkt.length, base);
					contiguous = 1;

				   while(contiguous)
				   {
				    fwrite(&buffer[mem_loc], 1, mem_size[base / PACKET_SIZE], file);
				    //memset(&buffer[mem_loc], 0, PACKET_SIZE - 3 * sizeof(int)); 
				    printf("Wrote %d bytes from packet %d data to file\n", mem_size[base / PACKET_SIZE], base);
				        mem_filled[base / PACKET_SIZE] = 0;
					total_bytes += mem_size[base / PACKET_SIZE];
					base = (base + PACKET_SIZE) % MAX_SEQ;
				        mem_loc = (mem_loc + recv_pkt.length) % buffer_size;
					contiguous = mem_filled[base / PACKET_SIZE];
					//printf("Contiguous: %d\n", contiguous);
					packet_counter++;
				    }
				      printf("Base: %d\n", base);
				}
				
				else if ( ((curr_seq > base) && (curr_seq < (base + rwnd))) || ((curr_seq < ((base + rwnd) % MAX_SEQ)) && (base + rwnd >= MAX_SEQ)) ){
					if(mem_filled[curr_seq / PACKET_SIZE] == 0){
					  memcpy(&buffer[1012 * (curr_seq / PACKET_SIZE)], &recv_pkt.content, recv_pkt.length);
					  mem_filled[curr_seq / PACKET_SIZE] = 1;
					  mem_size[curr_seq / PACKET_SIZE] = recv_pkt.length;
					  printf("Buffering out of order packet %d.\n", curr_seq);
					}
					else
					  printf("Packet %d already buffered\n", recv_pkt.seq_number);
				}
		  
				else
				  printf ("Packet %d below base. Data already buffered\n", recv_pkt.seq_number);

		  }
	     
		  else {
		    printf("Ignored packet %d. Base: %d\n", recv_pkt.seq_number, base); //include details
			/*ack_pkt.p_type = 1;
			ack_pkt.seq_number = recv_pkt.seq_number;
			if(sendto(sock_fd, &ack_pkt, sizeof(ack_pkt), 0, (struct sockaddr*) &server_addr, server_length) < 0)
				  error("Error: Cannot send packet");*/
		  }

		}		  
	}
	
	fclose(file);
	return 0;
}

/* Notes 

Use Pc Pl in calculating whether packet is lost or corrupted. If either, send note to server. Use random number generator with arg as Pl or Pc.
If packet has been received already, ignore it. Otherwise, write it. 
Keep total for for sequences and use modulo to restart after reaching the window. 
Use array to keep track of parts of buffer with data to be written.


*/
