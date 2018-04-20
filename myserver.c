

#include "unp.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>

#define ARRAY_SIZE 100
#define HEADER_SIZE 1000
#define DELIM " "
#define INFINITY 20
#define GET_NODE_ADDRESSES 1
#define GET_NODE_DISTANCES 2
#define MAX_NODES 20
#define DEFAULT_SEND_TIME 3
#define MAX_WAITING_TIME 16
#define MAX_RANDOM_WAIT_TIME 3
#define MAX_RANDOM_NUMBER_VALUE 4
#define DEFAULT_RANDOM_NUMBER_VALUE 2

struct neighbor {
	char name, ip_addr[ARRAY_SIZE], port[ARRAY_SIZE], next_hop;
	bool adjacent, connected, alive;
	int distance, initial_distance;
	time_t receive_time;
} neighbors[MAX_NODES];

struct node {
	char name, ip_addr[ARRAY_SIZE], port[ARRAY_SIZE];
	int distance, neighbor_count;
	time_t send_time;
} home_node;

int get_neighbor_count(char *);
void node_init(struct node *);
void neighbor_init(struct neighbor *);
void network_init(struct node *);
int parse_file(int, char *, struct node *);
void set_home_node_address(struct node *, char *);
void add_neighbor_node(char *, int);
void add_node_direct_neighbor_distance(struct node *, char *, int);
void add_distance(char, int, bool);
int get_node_number(char);
int make_header(char *);
int send_header(char *, int, char *, char *);
int update_adjacent_neighbors(char *, int);
int update_network(char, char, int, char, int);
int parse_header(char *, int);
int update_old_distances(int, int);
void clean_receive_header(char *);
time_t get_time();
int readable_timeo(int, int);
int default_send(char *, int);
void update_receive_time(char );
void check_neighbor_timeout(int);
void prefix_timestamp(char *);
int get_random(int, int, int);
void check_neighbor_alive();

void print_node(struct node);
void print_neighbor(struct neighbor node, int);
void print_all_nodes();
void print_table();

int max_neighbor_count, message_number;
bool found_home_node;

int main(int argc, char **argv)
{
	FILE * fp;
	char node_file[ARRAY_SIZE], neighbor_file[ARRAY_SIZE], * c, send_header[HEADER_SIZE], receive_header[HEADER_SIZE];
	struct sockaddr_in home_node_address;
	int i, neighbor_count, sockfd, bytes_sent, hour, min, sec;
	
	message_number = 0;
	
	//check for appropriate argument count
	if(argc != 2) {
		printf("usage: myserver <virtual address>\n");
		exit(1);
	}
	
	//set node and neighbor file names
	strcpy(node_file, "../src/node.config");
	strcpy(neighbor_file, "../src/neighbor.config");
	
	//get number of nodes to be used from file
	max_neighbor_count = get_neighbor_count(node_file);
	
	//initialize home node and neighbor nodes
	network_init(&home_node);
	
	//retrieve home node name from command line
	home_node.name = *argv[1];

	//fill node network and look for home node from command line arg
	found_home_node = false;
	parse_file(GET_NODE_ADDRESSES, node_file, &home_node);
	
	//if home node does not exist in file then exit
	if(!found_home_node) {
		printf("Node '%c' not found. Select node from %s\n", home_node.name, node_file);
		exit(1);
	}

	//get distances to adjacent neighbors
	parse_file(GET_NODE_DISTANCES, neighbor_file, &home_node);
	
	//initialize socket
	if( (sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		err_quit("socket error");
	}
	
	bzero(&home_node_address, sizeof(home_node_address));
	home_node_address.sin_family = AF_INET;
	intmax_t port = strtoimax(home_node.port, &c, 10);
	home_node_address.sin_port = htons((uint16_t) port);
	inet_pton(AF_INET, home_node.ip_addr, &home_node_address.sin_addr);
		
	//bind socket
	Bind(sockfd, (SA *) &home_node_address, sizeof(home_node_address));
	
	//make header with distance info
	make_header(&send_header[0]);
	prefix_timestamp(&send_header[0]);
	
	//send header with distance info to all adjacent neighbor nodes
	bytes_sent = update_adjacent_neighbors(send_header, sockfd);
	
	//receive header from neighbor
	while(1) {
		check_neighbor_timeout(sockfd);
		
		//send if the send time has passed
		if(difftime(get_time(), home_node.send_time) >= DEFAULT_SEND_TIME) {
			default_send(send_header, sockfd);
			
		}	
		
		//wait for random amount of time and if no packet comes in then send
		if (readable_timeo(sockfd, DEFAULT_SEND_TIME + get_random(MAX_RANDOM_NUMBER_VALUE, MAX_RANDOM_WAIT_TIME,
			DEFAULT_RANDOM_NUMBER_VALUE)) <= 0) {
			
			continue;
		} 
		
		//if packet comes in before random time has passed then process it
		else {
			
			strcpy(receive_header, "");
			
			recvfrom(sockfd, receive_header, HEADER_SIZE, 0, NULL, NULL);
			
			clean_receive_header(receive_header);
			parse_header(receive_header, sockfd);
			check_neighbor_timeout(sockfd);
		}
	}
	
	return 0;	
}

void check_neighbor_timeout(int sockfd) {
	double difference;
	time_t current_time = get_time();
	int i;
	char header[HEADER_SIZE];
	
	check_neighbor_alive();
	for(i = 0; i < max_neighbor_count; i++) {
		difference = difftime(current_time, neighbors[i].receive_time);
		if( (difference >= MAX_WAITING_TIME) && (neighbors[i].adjacent) && (neighbors[i].alive) ) {
			neighbors[i].alive = false;
			neighbors[i].distance = INFINITY;
			make_header(&header[0]);
			prefix_timestamp(&header[0]);
			print_table();
		}
	}
}	

void check_neighbor_alive() {
	int i;
	
	for(i = 0; i < max_neighbor_count; i++) {
		if( (neighbors[i].distance < INFINITY) && !neighbors[i].alive) {
			neighbors[i].alive = true;
			neighbors[i].receive_time = (get_time() + MAX_WAITING_TIME / 2);
		}
	}
}

int default_send(char * send_header, int sockfd) {
	make_header(&send_header[0]);
	prefix_timestamp(&send_header[0]);
	update_adjacent_neighbors(send_header, sockfd);
	
	return 1;
}

int readable_timeo(int fd, int sec) {
	fd_set rset;
	struct timeval tv;
	
	FD_ZERO(&rset);
	FD_SET(fd, &rset);
	
	tv.tv_sec = sec;
	tv.tv_usec = 0;
	
	return (select(fd + 1, &rset, NULL, NULL, &tv));
}

time_t get_time() {
	time_t ticks;
	
	ticks = time(NULL);
	return ticks;
}

void clean_receive_header(char * header) {
	char *e;

	e = strchr(header, '*');
	*(e + 1) = 0;
}
	

int parse_header(char * header, int sockfd) {
	char temp[HEADER_SIZE], name, sender_name, d[HEADER_SIZE], next_hop, * token;
	int distance, header_number;
	
	//copy into disposable variable
	strcpy(temp, header);
	
	//extract message number
	token = strtok(temp, DELIM);
	header_number = atoi(token);
	
	//extract sender name
	token = strtok(NULL, DELIM);
	sender_name = token[0];
	
	//update receive time of the sender
	update_receive_time(sender_name);
	
	//parse distances
	while(1) {
	
		//check for ending delimeter
		strcpy(temp, strtok(NULL, DELIM));
		if(strcmp(temp, "*") == 0) {
			return 1;
		}
		
		//extract next hop from string
		next_hop = temp[0];
		
		//extract destination name from string
		strcpy(temp, strtok(NULL, DELIM));
		name = temp[0];
		
		//extract distance to destination from string
		strcpy(d, strtok(NULL, DELIM));
		distance = atoi(d);
		
		//process updated values
		update_network(name, sender_name, distance, next_hop, sockfd);
	}
}

void update_receive_time(char name) {
	int neighbor_number;
	
	neighbor_number = get_node_number(name);
	neighbors[neighbor_number].receive_time = get_time();
	neighbors[neighbor_number].alive = true;
}

int update_network(char neighbor_name, char sender_name, int distance_from_sender_to_neighbor, char next_hop, int sockfd) {
	int neighbor_number, sender_number;
	char old_header[HEADER_SIZE], new_header[HEADER_SIZE];
	
	//record previous header
	make_header(&old_header[0]);
		
	//get neighbor numbers
	neighbor_number = get_node_number(neighbor_name);
	sender_number = get_node_number(sender_name);
	//ignore update if home node has a direct path to the neighbor in question
	
	//if node in question is home node then confirm that sender is alive
	if(neighbor_name == home_node.name) {
		neighbors[sender_number].alive = true;
		neighbors[sender_number].distance = neighbors[sender_number].initial_distance;
		
	}	
	else {
		if(next_hop != home_node.name) {
		
			//if new distance is shorter than old distance then update
			if(neighbors[neighbor_number].distance > (neighbors[sender_number].distance + distance_from_sender_to_neighbor) ) {
				neighbors[neighbor_number].distance = neighbors[sender_number].distance + distance_from_sender_to_neighbor;
				if(neighbors[neighbor_number].distance > INFINITY) {
					neighbors[neighbor_number].distance = INFINITY;
				}
				neighbors[neighbor_number].connected = true;
				neighbors[neighbor_number].next_hop = sender_name;
			}
		}
	
		if(!neighbors[neighbor_number].adjacent) {
				neighbors[neighbor_number].distance = neighbors[sender_number].distance + distance_from_sender_to_neighbor;
				if(neighbors[neighbor_number].distance > INFINITY) {
					neighbors[neighbor_number].distance = INFINITY;
				}
		}
	}
	make_header(&new_header[0]);
	
	if(strcmp(new_header, old_header) != 0) {
		prefix_timestamp(&new_header[0]);
		print_table();
	}
	
	return 1;
}

int update_old_distances(int sender_number, int new_distance) {
	int i;

	for(i = 0; i < max_neighbor_count; i++) {
		if(neighbors[i].connected && !neighbors[i].adjacent) {
			if(neighbors[i].distance > (neighbors[sender_number].distance + new_distance) ) {
				neighbors[i].distance = (neighbors[sender_number].distance + new_distance);
			}
		}
	}
	
	return 1;
}
int get_neighbor_count(char * file_name) {
	char c;
	int line_count = 0;
	
	//check for bad file name
	FILE *fp = fopen(file_name,"r");
	if(fp == NULL) {
		err_quit("bad file\n");
	}
	
	while(1) {
		c = fgetc(fp);
		if(c == '\n')
			line_count++;
		if(feof(fp)) {
			line_count++;
			break;
		}
	}
	return line_count - 1;
}

void node_init(struct node * node) {

	//distance to self is always 0
	node->distance = 0;
	
	//at first the node is not aware of its neighbors
	node->neighbor_count = 0;
	
	node->name = '-';
	node->send_time = get_time();
	strcpy(node->ip_addr, "-");
	strcpy(node->port, "-");
}

void neighbor_init(struct neighbor * node) {
	
	//starting distance is infinity
	node->distance = INFINITY;
	node->initial_distance = INFINITY;
	node->adjacent = false;
	node->next_hop = '-';
	node->connected = false;
	node->name = '-';
	node->alive = true;
	node->receive_time = get_time();
	strcpy(node->ip_addr, "-");
	strcpy(node->port, "-");
}

void network_init(struct node * node) {
	int i;
	
	//initialize home node
	node_init(node);

	//initialize neighbor nodes
	for(i = 0; i < max_neighbor_count; i++) {
		neighbor_init(&neighbors[i]);
	}
}

int parse_file(int action, char * file_name, struct node * node) {
	char ch;
	int j = 0, neighbor_count = 0;
	char line[ARRAY_SIZE];
	
	//check for bad file name
	FILE *fp = fopen(file_name,"r");
	if(fp == NULL) {
		err_quit("bad file\n");
	}
	
	do {
		ch = fgetc(fp);
		if(feof(fp)) {
			line[j] = 0;
			if(j > 0) {
				if(action == GET_NODE_ADDRESSES) {
					if(line[0] != node->name) {
						add_neighbor_node(line, neighbor_count);
					} else {
						set_home_node_address(node, line);
					}
				} else {
					add_node_direct_neighbor_distance(node, line, neighbor_count);
				}
			}
			break;
		}
		else if(ch == '\n') {
			line[j] = 0;
			j = 0;
			if(action == GET_NODE_ADDRESSES) {
				if(line[0] != node->name) {
					add_neighbor_node(line, neighbor_count);
					neighbor_count++;
				} else {
					set_home_node_address(node, line);
				}
			} else {
				add_node_direct_neighbor_distance(node, line, neighbor_count);
			}
		}
		else {
			line[j] = ch;
			j++;
		}
	} while(1);
	
	return neighbor_count;
}

void set_home_node_address(struct node * node, char * node_info) {
	char temp[ARRAY_SIZE];
	
	//copy line to disposable variable
	strcpy(temp, node_info);
	
	//skip node name as we already received it from command line
	strtok(temp, DELIM);
	
	//extract home node ip address
	strcpy(node->ip_addr, strtok(NULL, DELIM));
	
	//extract home node port number
	strcpy(node->port, strtok(NULL, DELIM));
	
	//confirm that node is in file
	found_home_node = true;
}

void add_neighbor_node(char * neighbor_info, int neighbor_number) {
	char temp[ARRAY_SIZE], c;
	
	//copy line to disposable variable
	strcpy(temp, neighbor_info);

	//extract node name
	c = strtok(temp, DELIM)[0];
	neighbors[neighbor_number].name = c;
	
	//extract node ip address
	strcpy(neighbors[neighbor_number].ip_addr, strtok(NULL, DELIM));
	
	//extract node port number
	strcpy(neighbors[neighbor_number].port, strtok(NULL, DELIM));
}
	
void add_node_direct_neighbor_distance(struct node * node, char * line, int neighbor_number) {
	char temp[ARRAY_SIZE], node_a, node_b;
	int distance;
	
	//copy line to disposable variable
	strcpy(temp, line);
	
	//extract name of first node in line
	node_a = *(strtok(temp, DELIM));
	
	//extract name of second node in line
	node_b = *(strtok(NULL, DELIM));
	
	//extract distance between two nodes in line
	distance = atoi(strtok(NULL, DELIM));
	
	//if line contains information about the home node, then record the distance and set to adjacent
	if(node_a == node->name) {
		add_distance(node_b, distance, true);
	}
	if(node_b == node->name) {
		add_distance(node_a, distance, true);
	}
}

void add_distance(char node_name, int distance, bool adjacent) {
	int node_num;
	
	//check for valid node names
	if( (node_num = get_node_number(node_name)) < 0) {
		printf("bad node name: %c\n", node_name);
		exit(1);
	}

	//set distance to node
	neighbors[node_num].distance = distance;
	
	//set adjacency
	if(adjacent) {
		neighbors[node_num].adjacent = true;
		neighbors[node_num].connected = true;
		neighbors[node_num].initial_distance = distance;
		neighbors[node_num].next_hop = home_node.name;
	}
}

int get_node_number(char node_name) {
	int i;
	
	//loop through all nodes and find node who's number matches the name
	for(i = 0; i < max_neighbor_count; i++) {
		if(neighbors[i].name == node_name) {
			return i;
		}
	}
	
	//return -1 if node not found
	return -1;
}

int make_header(char * header) {
	int i;
	char temp[ARRAY_SIZE];
	
	//initialize header
	strcpy(header, "");
	
	//put name of sending node as first element
	sprintf(temp, "%c", home_node.name);
	strcat(header, temp);
	strcat(header, DELIM);
	
	//put names of nodes followed by corresponding distances
	for(i = 0; i < max_neighbor_count; i++) {
		sprintf(temp, "%c", neighbors[i].next_hop);
		strcat(header, temp);
		strcat(header, DELIM);
		sprintf(temp, "%c", neighbors[i].name);
		strcat(header, temp);
		strcat(header, DELIM);
		sprintf(temp, "%d", neighbors[i].distance);
		strcat(header, temp);
		strcat(header, DELIM);
	}
	
	//special character denotes end of header
	strcat(header, "*");
	
	return 1;
}

void prefix_timestamp(char * str) {
	char temp[ARRAY_SIZE];
	
	sprintf(temp, "%d%s%s", message_number, DELIM, str);
	strcpy(str, temp);
	message_number++;
}

int send_header(char * header, int sockfd, char * ip_addr, char * port_num) {
	struct sockaddr_in servaddr;
	char *c;
	
	intmax_t port = strtoimax(port_num, &c, 10);
	
	//initialize socket
	bzero(&servaddr, sizeof(servaddr));
	servaddr.sin_family = AF_INET;
	servaddr.sin_port = htons((uint16_t) port);
	inet_pton(AF_INET, ip_addr, &servaddr.sin_addr);
	
	//set send time
	home_node.send_time = get_time();
	
	//send header to neighbor
	return sendto(sockfd, header, strlen(header), 0, (struct sockaddr *) &servaddr, sizeof(servaddr));
	
}

int update_adjacent_neighbors(char * header, int sockfd) {
	int i, bytes_sent;
	
	//send header to all adjacent neighbor nodes
	for(i = 0; i < max_neighbor_count; i++) {
		if(neighbors[i].adjacent) {
			bytes_sent = send_header(header, sockfd, neighbors[i].ip_addr, neighbors[i].port);
		}
	}
	
	return bytes_sent;
}

		

void print_node(struct node node) {
	printf("name: %c\n", node.name);
	printf("distance: %d\n", node.distance);
	printf("ip addr: %s\n", node.ip_addr);
	printf("port: %s\n", node.port);
	printf("neighbor count: %d\n", node.neighbor_count);
	printf("\n");
}

void print_neighbor(struct neighbor node, int i) {
	printf("Neighbor node %d: \n", i);
	printf("name: %c\n", neighbors[i].name);
	printf("current distance: %d\n", neighbors[i].distance);
	printf("initial distance: %d\n", neighbors[i].initial_distance);
	printf("receive time: %d\n", neighbors[i].receive_time);
	printf("ip addr: %s\n", neighbors[i].ip_addr);
	printf("port: %s\n", neighbors[i].port);
	
	printf("adjacent: ");
	if(neighbors[i].adjacent) {
		printf("yes\n");
	} else {
		printf("no\n");
	}
	
	printf("connected: ");
	if(neighbors[i].connected) {
		printf("yes\n");
	} else {
		printf("no\n");
	}
	
	printf("\n");
}

void print_all_nodes() {
	int i;
	
	printf("Home node\n");
	print_node(home_node);
	for(i = 0; i < max_neighbor_count; i++) {
		print_neighbor(neighbors[i], i);
	}
}

void print_table() {
	int i;
	
	printf("Routing table for node %c at time %d\n\n", home_node.name, get_time());
	printf("Node\tCost\n\n");
	for(i = 0; i < max_neighbor_count; i++) {
		printf("%c\t", neighbors[i].name);
		if(neighbors[i].distance >= INFINITY) {
			printf("Infinity");
		} else {
			printf("%d", neighbors[i].distance);
		}
		printf("\n");
	}
	printf("\n");
}

int get_random(int max_num, int max_wait, int default_num) {
	int i, r;
   	time_t t, t_start;
   
   /* Intializes random number generator */
   	srand((unsigned) time(&t));
   	
   	//get a random number until random number is between 0 and max
   	t_start = get_time();
   	do {
   		
   		//if it takes longer than 2 seconds to find a random number then just return default num
		if(difftime(get_time(), t_start) > max_wait) {
			return default_num;
		}
		r = rand();
   	} while( (r > max_num)  || (r < 0) );
   
   	return(r);
}