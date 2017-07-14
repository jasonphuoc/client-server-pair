#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h> 
#include "packet.h"
//TODO: Maybe the client should ACK the final FIN packet with a FINACK???
//TODO: Change timer to ms
//TODO: Fix Address Family error

int main(int argc, char *argv[]) 
{
	//TODO: check finalizing packet submissions
	//TODO: check probabilities for first and last packet/over seq num and also timeout and retrans
	//Base variables for establishing the server connection and storing input arguments
        int sockfd, clilen, portno, window_size, c_window_size, c_control, total_bytes = 0;
	double loss_prob, corruption_prob;
	struct sockaddr_in serv_addr, cli_addr;

	//The window stores all packets currently transmitted and each has a timer in timer at the corresponding index to check for packet loss 
	/*	struct packet window[WINDOW_SIZE/PACKET_SIZE];
		time_t timers[WINDOW_SIZE/PACKET_SIZE];*/
	struct packet current_packet, current_out_packet;
	struct stat stats;

	FILE *resource;

	//Display error if not all 5 command line arguments were provided accordingly
	if (argc != 6 && argc != 5) 
	{
		fprintf(stderr, "Usage: %s <port> <window_size> <loss_probability> <corruption_proability> [<congestion control: 1>]\n", argv[0]);
		exit(1);
	}

	//Parse command line arguments
	portno = atoi(argv[1]);
	//TODO: check window size
	window_size = atoi(argv[2]);
	loss_prob = atof(argv[3]);
	corruption_prob = atof(argv[4]);
	if(argc > 5)
		c_control = atoi(argv[5]);
	
	struct packet *window = (struct packet *) malloc (((window_size / PACKET_SIZE) + 1) * sizeof (struct packet));
	struct timeval *timers = (struct timeval *) malloc (((window_size / PACKET_SIZE) + 1) * sizeof(struct timeval));
	
	//Print errors if probabilities and port number are out of range
	if(portno > 49151 || portno < 1024)
		error("ERROR Enter valid port number.\n");
	if((loss_prob < 0 || loss_prob > 1) || (corruption_prob < 0 || corruption_prob > 1))
		error("ERROR loss_prob and corruption_prob must lie between 0 and 1.\n");

	//Open and bind udp server socket connection on localhost at user-provided port
	if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
		error("ERROR opening socket");

	memset(&serv_addr, 0, sizeof(serv_addr));

	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	serv_addr.sin_port = htons(portno);

	if (bind(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0)
		error("ERROR while binding");

	clilen = sizeof(cli_addr);
	
	if(c_control == 1)
	  c_window_size = PACKET_SIZE;

	else
	  c_window_size = window_size;
	
	while(1)
	{
	  int i, no_of_packets = 0, no_packets_in_window = 0, current_seq_number, base = 0, ssthresh = window_size / 2, c_counter = 0;
	  //printf("ssthresh: %d\n", ssthresh);

		//Clear out any unwanted data in the new packet to be sent when client request is received
		bzero((char *) &current_out_packet, sizeof(current_out_packet));
        	current_out_packet.p_type = 2;

		//Wait for client request packet
		if (recvfrom(sockfd, &current_packet, sizeof(current_packet), 0, (struct sockaddr*) &cli_addr, (socklen_t*) &clilen) < 0)
			error("ERROR while receiving packet");
		
		printf("Received %d bytes from %s.\nPacket content:\n%s\n\n", current_packet.length, inet_ntoa(cli_addr.sin_addr), current_packet.content);

		//Open file requested for transmission by client
		if (!(resource = fopen(current_packet.content, "rb")))
			sockfd = socket(AF_INET, SOCK_DGRAM, 0);

		//Determine total number of packets necessary for transmission
		stat(current_packet.content, &stats);
		no_of_packets = stats.st_size / (PACKET_SIZE - 3*sizeof(int)) + ((stats.st_size % (PACKET_SIZE - 3*sizeof(int))) ? 1 : 0);
		printf("Sending a total of %d packets.\n", no_of_packets);

		//		int current_window_range;
		current_seq_number = 0;
		//Loop until all packets of the requested file were sent and corresponding ACKs from the client were received
		//Each iteration will send all new packets in current window, receive client ACKs and retransmit packets if necessary
		while(current_seq_number < (stats.st_size + 3 * sizeof(int) * no_of_packets) || no_packets_in_window != 0) 
		{
			int timeout = -1;
			//Compute end of current window		
			//Send all packets in within current windows that have not been sent yet
      			
			for (; (/*(current_seq_number <= (base + c_window_size - PACKET_SIZE)%MAX_SEQ && base + c_window_size - PACKET_SIZE >= MAX_SEQ) || */current_seq_number <= (base + c_window_size - PACKET_SIZE)) && no_packets_in_window < no_of_packets && no_packets_in_window < c_window_size/PACKET_SIZE && current_seq_number < (stats.st_size + 3 * sizeof(int) * no_of_packets);)
			{
			  //			  printf("Congestion window: %d\n", c_window_size);	
			  //        printf("Base: %d, window size: %d, current seq: %d\n", base, c_window_size, current_seq_number);
				//Read data from request file into current packet corresponding to its sequence number anset it
				bzero((char *) &current_out_packet, sizeof(current_out_packet));
				current_out_packet.length = fread(current_out_packet.content, 1, PACKET_SIZE - 3*sizeof(int), resource);
				current_out_packet.seq_number = current_seq_number % MAX_SEQ;

				//Update sequence number for next file, store output packet in window start corresponding timer
				current_seq_number += PACKET_SIZE;
				window[no_packets_in_window] = current_out_packet;
				gettimeofday(&timers[no_packets_in_window++], NULL);

				printf("Sent %d bytes in packet with sequence number %d.\n", current_out_packet.length, current_out_packet.seq_number);
				
				//Send current output packet to client
				if (sendto(sockfd, &current_out_packet,  current_out_packet.length + 3*sizeof(int), 0, (struct sockaddr *) &cli_addr, clilen) < 0){
				  printf("Packet: %d\n", current_out_packet.seq_number);
				  error("ERROR while sending packet.");
				}

				//Update number of bytes transmitted
				total_bytes += current_out_packet.length;
			}

			struct timeval cur_time;
			gettimeofday(&cur_time, NULL);

			

			//Check whether any packet in the window has timed out and prepare for retransmitting it
			for(i = 0; timeout == -1 && i < no_packets_in_window; i++)
			  if((timers[i].tv_sec + (timers[i].tv_usec / 1000000.0)) * 1000.0 + TIMEOUT < (cur_time.tv_sec + (cur_time.tv_usec / 1000000.0)) * 1000.0)
				{
					timeout = i;
					//sets the packet for retransmission
					current_packet = window[i];

					if(c_control == 1){
					  ssthresh = window_size / 2;
					  c_window_size = PACKET_SIZE;
					  printf("Base: %d, window size: %d, current seq: %d\n", base, c_window_size, current_seq_number);
					}

					printf("Timeout on %d, Base: %d\n", current_packet.seq_number, base%MAX_SEQ);
				}

			//set packet receive to non-blocking so the server can continue checking for timeouts if no ACK was received yet
			fcntl(sockfd, F_SETFL, O_NONBLOCK);

			//if a packet in the window timed out, retransmit it. Otherwise, check for ACK from client
			if (timeout != -1 || recvfrom(sockfd, &current_packet, sizeof(current_packet), 0, (struct sockaddr*) &cli_addr, (socklen_t*) &clilen) > 0) 
			{
				//Variable to check whether ACK or retransmit was successful
				int old_no_packets = no_packets_in_window;
			
				//Skip procedure for dealing with ACK packets if we merely transmit a packet that timed out
				if(timeout == -1)
				{	
					//Simulate packet loss with user-provided loss probability
					if(error_gen(loss_prob))
					{
						//If loss shall be simulated, pretend nothing was received and continue
						printf("Lost %d\n", current_packet.seq_number);
						continue;
					}
					//Simulate packet loss with user-provided loss probability
					else if (error_gen(corruption_prob))
					{
						printf("Corruption in %d\n", current_packet.seq_number);
						timeout = i;
					}
					//if ACK was received
					else
						//check whether the ACKed packets sequence number corresponds to a packet in our window
						for(i = 0; i < no_packets_in_window; i++)
							if(window[i].seq_number == current_packet.seq_number)
							{
								//if it does correspond, remove packet and timer from window without altering
								//order of packets in window

								  if(c_control == 1){ 
									c_counter += PACKET_SIZE;
									if(c_window_size >= ssthresh) {
									  if(c_counter >= c_window_size){
									    c_window_size += PACKET_SIZE;
									    c_counter = 0;
									  }
									}
							       
									else
									  c_window_size += PACKET_SIZE;

									if(c_window_size > window_size)
									  c_window_size = window_size;

								  }

 
								printf("Received ACK for packet %d\n", current_packet.seq_number);

								//if(no_packets_in_window > 0) 
									no_packets_in_window--;

								for(; i <= no_packets_in_window; i++)
								{
									//If first packet in window was ACKed, adjust window base to the new lowest
									//sequence number in current window
									/*if(!i)
									{
										if(no_packets_in_window > 1)
											if (window[i+1].seq_number > window[i].seq_number)
												base = window[i+1].seq_number;
											else
												exit(0);//base += c_window_size - PACKET_SIZE;//MAX_SEQ + window[i+1].seq_number - window[i].seq_number;
										else
										{
											int j;
											for(j=0; j < c_window_size/PACKET_SIZE; j++)
												if(window[j].seq_number > base)
													base = window[j].seq_number;
											//base = window[c_window_size/PACKET_SIZE-1].seq_number;//base += c_window_size ;//- PACKET_SIZE;
										}
										//base = window[c_window_size/PACKET_SIZE].seq_number;
								        }*/
									if(no_packets_in_window)
									{
										window[i] = window[i+1];
										timers[i] = timers[i+1];	
									}
								}
								
								if (no_packets_in_window == 0) 
									base += PACKET_SIZE;
								else	
								{							
									int j;
									for(j=0; j < no_packets_in_window; j++)
										if(window[j].seq_number < base)
											base = window[j].seq_number;
								}
								/*
										int j;
								for(j=0; j < no_packets_in_window; j++)
									if(window[j].seq_number < base)
											base = window[j].seq_number;
									if(no_packets_in_window == 0)
									{
										if(window[j].seq_number > base)
											base = window[j].seq_number;
									}
									else
										i*/
							}
				}
				//If timeout or corruption ocurred in the server, or the client resent previously ACKed sequence number in request
				//for retransmission of a corrupted file, retransmit that packet
				if(old_no_packets == no_packets_in_window)
					//Find packet to be resent
					for(i = 0; i < no_packets_in_window; i++)
						//if corruption on server side or timeout, look for same sequence number as current packet to resend
						//if clinet requested retransmission, look for the next packet he is missing
						if(window[i].seq_number == (current_packet.seq_number + (timeout == -1 ? current_packet.length : 0))%MAX_SEQ)
						{
							//reset timer and retransmit appropriate packet
							gettimeofday(&timers[i], NULL);
							
							if (sendto(sockfd, &window[i], window[i].length + 3 * sizeof(int), 0, (struct sockaddr *) &cli_addr, clilen) < 0){//CHANGED JL
							  printf("Packet: %d", window[i].seq_number);
							  error("ERROR while resending packet");
							}

							printf("Resent %d bytes in packet with sequence number %d.\n", window[i].length, window[i].seq_number);
							//set variables to indicate successful retransmission
							old_no_packets--;
							timeout = -1;
						}

				//If ACK, timeout or retransmission was not successful, print error message
				/*if(old_no_packets == no_packets_in_window || timeout != -1){
					printf("Packet: %d", current_packet.seq_number);
					error("ERROR while sending packet");
				}*/
			}
		}

		bzero((char *) &current_out_packet, sizeof(current_out_packet));

		//Send FIN packet to client after successful transmission and reicept of ACKs for the entire requested file
		current_out_packet.p_type = 3;
		printf("Sending FIN. Sent %d bytes in %d packets.\n", total_bytes, no_of_packets); //CHANGED JL
		sendto(sockfd, &current_out_packet, sizeof(current_out_packet), 0, (struct sockaddr *) &cli_addr, clilen);
	
		//recvfrom(sockfd, &current_packet, sizeof(current_packet), 0, (struct sockaddr *) &cli_addr, (socklen_t*) &clilen);
		//printf("closing connection.\n"); 
	
		//Close socket and exit server after successful transmission
		close(sockfd);
		return 0;

	}
	return 1;	
}
