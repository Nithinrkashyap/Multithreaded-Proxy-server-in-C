#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>

#define MAX_CLIENTS 400
#define MAX_BYTES 4096
#define MAX_SIZE 200 * (1 << 20)
#define MAX_ELEMENT_SIZE 10 * (1 << 20)
typedef struct cache_element
{
    //* data stores response web page
    char *data;
    //* sizeof(data)
    int len;
    //^ url stores the request
    char *url;
    time_t lru_time_track;
    struct cache_element *next;

} Cache_element;

Cache_element *find(char *url);
int add_cache_element(char *data, int size, char *url);

void remove_cache_element();

int port_number = 8080;

int proxy_socketId;

//* stores thread ids of clients in array
pthread_t tid[MAX_CLIENTS];

//& clients requests goes out of  the maxclients semaphores puts the waiting threads to sleep state ,
//&when the traffic in the queue decreses
//& it wakes up the waiting threads
sem_t semaphores;

//! used to create a mutex (mutual exclusion) object in C, which is essential for ensuring synchronization between threads.
pthread_mutex_t lock; //~ Locking shared resources

//* header for cache elements
Cache_element *head;

//& denotes current size of cache
int cache_size;

int sendErrorMessage(int socket, int status_code)
{
    char str[1024];
    char currentTime[50];
    time_t now = time(0);

    struct tm data = *gmtime(&now);
    strftime(currentTime, sizeof(currentTime), "%a, %d %b %Y %H:%M:%S %Z", &data);

    switch (status_code)
    {
    case 400:
        snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
        printf("400 Bad Request\n");
        send(socket, str, strlen(str), 0);
        break;

    case 403:
        snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
        printf("403 Forbidden\n");
        send(socket, str, strlen(str), 0);
        break;

    case 404:
        snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
        printf("404 Not Found\n");
        send(socket, str, strlen(str), 0);
        break;

    case 500:
        snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
        // printf("500 Internal Server Error\n");
        send(socket, str, strlen(str), 0);
        break;

    case 501:
        snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
        printf("501 Not Implemented\n");
        send(socket, str, strlen(str), 0);
        break;

    case 505:
        snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
        printf("505 HTTP Version Not Supported\n");
        send(socket, str, strlen(str), 0);
        break;

    default:
        return -1;
    }
    return 1;
}

//& return type
int connectRemoteServer(char *host_addr, int port_num)
{

    int remoteSocket = socket(AF_INET, SOCK_STREAM, 0);

    if (remoteSocket < 0)
    {
        printf("Error in Creating Socket.\n");
        return -1;
    }

    struct hostent *host = gethostbyname(host_addr);

    if (host == NULL)
    {
        fprintf(stderr, "No such host exists.\n");
        return -1;
    }

    struct sockaddr_in server_addr;

    bzero((char *)&server_addr, sizeof(server_addr));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_num);

    bcopy((char *)host->h_addr, (char *)&server_addr.sin_addr.s_addr, host->h_length);

    //* establish connection between clinet and remote server

    //& This line attempts to establish a connection from the client (identified by remoteSocket) to the server whose address is specified in the server_addr structure.
    if (connect(remoteSocket, (struct sockaddr *)&server_addr, (size_t)sizeof(server_addr)) < 0)
    {
        fprintf(stderr, "Error in connecting !\n");
        return -1;
    }

    return remoteSocket;
}

int handle_request(int socket_connected_id, struct ParsedRequest *request, char *tempreq)
{

    char *buf = (char *)malloc(sizeof(char) * MAX_BYTES);
    strcpy(buf, "GET ");
    strcat(buf, request->path);
    strcat(buf, " ");
    strcat(buf, request->version);
    strcat(buf, "\r\n");

    size_t len = strlen(buf);

    if (ParsedHeader_set(request, "Connection", "close") < 0)
    {
        printf("set header key not work\n");
    }

    if (ParsedHeader_get(request, "Host") == NULL)
    {
        if (ParsedHeader_set(request, "Host", request->host) < 0)
        {
            printf("Set \"Host\" header key not working\n");
        }
    }

    if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0)
    {
        printf("unparse failed\n");
    }

    int server_port = 80; // default port

    if (request->port != NULL)
    {
        server_port = atoi(request->port);
    }
    //*Host: www.example.com
    int remoteSocketID = connectRemoteServer(request->host, server_port);
    if (remoteSocketID < 0)
    {
        return -1;
    }

    int bytes_send = send(remoteSocketID, buf, strlen(buf), 0);
    bzero(buf, MAX_BYTES);

    //& it recevies data upto MAXBYTES-1 and leaving 1 space for '\0'
    bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);

    char *temp_buffer = (char *)malloc(sizeof(char) * MAX_BYTES);
    int temp_buffer_size = MAX_BYTES;
    int temp_buffer_index = 0;

    //^ when all the data is sent as respose to remotesocket it automatically closes the connection for the remotesocket
    while (bytes_send > 0)
    {

        bytes_send = send(socket_connected_id, buf, bytes_send, 0);

        for (int i = 0; i < bytes_send / sizeof(char); i++)
        {

            temp_buffer[temp_buffer_index] = buf[i];
            temp_buffer_index++;
        }
        temp_buffer_size = temp_buffer_size + MAX_BYTES;
        temp_buffer = (char *)realloc(temp_buffer, temp_buffer_size);
        if (bytes_send < 0)
        {
            perror("Error in sending data to client socket.\n");
            break;
        }
        bzero(buf, MAX_BYTES);

        bytes_send = recv(remoteSocketID, buf, MAX_BYTES - 1, 0);
    }

    temp_buffer[temp_buffer_index] = '\0';

    free(buf);
    add_cache_element(temp_buffer, strlen(temp_buffer), tempreq);
    printf("Done......\n");
    free(temp_buffer);

    close(remoteSocketID);
    return 0;
}

//~ HTTP request version
int checkHTTPversion(char *msg)
{

    int version = -1;

    if (strncmp(msg, "HTTP/1.1", 8) == 0)
    {
        version = 1;
    }
    else if (strncmp(msg, "HTTP/1.0", 8) == 0)
    {
        version = 1;
    }
    else
        version = -1;

    return version;
}

//! this function does is it takes a Connected_client to main socket  and creates a new socket and syncronises the threads
void *thread_fn(void *SocketNew)
{
    //! structure of syncronization
    //* -----Entry------
    //* Critical section(Shared code ,shared resources,etc)
    //* -----exit-------
    //& Aim is to achieve Mutual exclusion to have Syncronization
    //^ when google.com client in that new thread(socket) comes into Critical section for execution first it goes to Entry section
    //~ Initially semaphore = MAX_CLIENTS(10) WHEN IT GOES TO entry according to this scenario we have to decrease the value of semaphore value
    sem_wait(&semaphores);
    int p;
    sem_getvalue(&semaphores, &p);

    printf(" when wait is done it decreases the value of semaphores : %d", semaphores);

    //! know we want the id(discriptor) of the connected client
    int *pointer_to_get_id_connected_client = (int *)SocketNew;
    int socket_connected_id = *pointer_to_get_id_connected_client;

    //& len is current buffer length
    int bytes_send_client, len;

    //& sending the message in bytes 4kb
    //! this buffer is basically a socket buffer
    char *buffer = (char *)calloc(MAX_BYTES, sizeof(char));

    bzero(buffer, MAX_BYTES);

    //? bytes send by the client
    //& recv() does is it reads the buffer of the socket upto to MAX_BYTES
    //* Receives Data from a Socket: It attempts to receive data from the connected socket (represented by socket) into the buffer you provide.
    //~Buffer: The data received from the network is stored in the memory area pointed to by buffer. This is where the incoming data will be placed for further processing by your program.

    //^ recv(), the program will pause and wait until data is available on the socket to be read.
    bytes_send_client = recv(socket_connected_id, (char *)buffer, MAX_BYTES, 0);
    printf("After receving from the recv function buffer is %s and bytes send %d", buffer, bytes_send_client);

    //! why we have to use while loop because
    //&The amount of data sent by the client might be larger than the buffer size or might arrive in chunks.
    // & Since recv() only retrieves data that is currently available in the socket buffer,
    //&   there might be more data to receive if the client sends a large request in multiple packets.

    while (bytes_send_client > 0)
    {

        len = strlen(buffer);
        if (strstr(buffer, "\r\n\r\n") == NULL)
        {

            bytes_send_client = recv(socket_connected_id, buffer + len, MAX_BYTES - len, 0);
        }

        else
        {
            break;
        }
    }

    printf("After receving all data %s and bytes send is %d \n", buffer, bytes_send_client);

    //^ creating copy of buffer
    char *tempreq = (char *)malloc(strlen(buffer) * sizeof(char) + 1);
    for (int i = 0; i < strlen(buffer); i++)
    {
        tempreq[i] = buffer[i];
    }

    //* tempreq it stores the request from buffer
    printf("temp req is : %s \n", tempreq);

    //& basically finding the request from the LRU cache
    Cache_element *temp = find(tempreq);

    //~ if u get response from lru cache it is not NuLL

    //* find() is used to find whether this request is already there in the LRU cache
    if (temp != NULL)
    {
        //! return the response

        //* size is the len of response
        int size = temp->len / sizeof(char);

        //& WE CAN SEND THE response till MAXBYTES
        char response[MAX_BYTES];

        //* User Space : The application (your proxy server) operates in user space and calls send().
        //! System Call : send() is a system call that transitions control from user space to kernel space to handle the actual data transmission.
        //^ kernal maintains a send buffer
        int pos = 0;

        while (pos < size)
        {
            bzero(response, MAX_BYTES);
            int i = 0;

            while (i < MAX_BYTES && pos < size)
            {
                response[i] = temp->data[pos];
                pos++;
                i++;
            }

            send(socket_connected_id, response, MAX_BYTES, 0);
        }
        printf("Data retrived from the cache \n");
        printf("%s \n", response);
    }

    //* connection is still active the client can still send the request like GET,POST

    //! we need to parse the data

    else if (bytes_send_client > 0)
    {
        //& parse the request
        len = strlen(buffer);

        struct ParsedRequest *request = ParsedRequest_create();

        if (ParsedRequest_parse(request, buffer, len) < 0)
        {
            printf("Parsing failed \n");
        }

        //~ request contains essential feautes like method ,version etc

        else
        {
            bzero(buffer, MAX_BYTES);

            printf("Request method is :%s", request->method);
            //* strcmp(request->method, "GET") returns 0 if strings are equal
            if (!strcmp(request->method, "GET"))
            {
                if (request->host && request->path && checkHTTPversion(request->version) == 1)
                {
                    //* in this project we are handling GET request
                    //& handle_request() function is to process client requests that are not found in the proxy server's cache
                    //! retrevies the response from the server and stores in cache
                    //? and sends the response back to the client
                    bytes_send_client = handle_request(socket_connected_id, request, tempreq);

                    if (bytes_send_client == -1)
                    {
                        sendErrorMessage(socket_connected_id, 500);
                    }
                }
                else
                {
                    sendErrorMessage(socket_connected_id, 500);
                }
            }
            else
            {
                printf("WE have designed for GET request only \n");
            }
        }
        ParsedRequest_destroy(request);
    }
    else if (bytes_send_client < 0)
    {
        perror("Error in receiving from client.\n");
    }
    else if (bytes_send_client == 0)
    {
        printf("Client disconnected!\n");
    }

    shutdown(socket_connected_id, SHUT_RD);
    close(socket_connected_id);

    free(buffer);
    free(tempreq);
    sem_post(&semaphores);
    sem_getvalue(&semaphores, &p);

    printf("Semaphores post value is: %d \n", p);

    return NULL;
}

int main(int argc, char *argv[])
{

    //^ when client opens the socket
    int client_socketId, client_len;

    //? these are used to store information about the address(Ip) of client,server
    struct sockaddr_in server_addr, client_addr;

    sem_init(&semaphores, 0, MAX_CLIENTS);
    printf("semaphores initialized value is : %d \n", semaphores);
    //& System allocates memory for lock ,lock status if it is unlocked any thread can access without waiting
    int p = pthread_mutex_init(&lock, NULL);

    printf("%d", p); // no error

    if (argc == 2)
    {
        port_number = atoi(argv[1]);
    }
    else
    {
        printf("Too few arguments\n");
        exit(1); //* SYSTEM CALL ENDS THE WHOLE PROGRAM
    }

    printf("Setting Proxy server port %d \n", port_number);

    //& A socket is a communication endpoint in a network. It's like a virtual port through which data can be sent or received between two applications running on different computers.

    //*Thread Spawning is the process of creating a new thread of execution within a running program.
    //* Independent execution: Each thread has its own execution stack, program counter, and registers, allowing them to execute code concurrently.
    //* Shared resources: Threads can share access to global variables and other resources, making communication and data sharing possible.

    //! proxy has only 1 socket in which many clients requests
    //~ when there is only 1 socket called proxysocket when the request is accepted it spawns the thread
    //* communication takes place in spawned thread(new thread)

    //* SOCK_STREAM TCP communication(secure) it tells to server are u available then we will start
    //* socket() is used to create the socket
    //* basically this code is creating a socket within the proxyserver were multiple clients can get coonected
    //& it is main thread which controls and manages the new threads
    proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

    // AF_INET uses IPv4 (Internet Protocol version 4) addressing scheme.
    if (proxy_socketId < 0)
    {
        perror("Failed to create a socket \n");
        exit(1);
    }

    //& u are enabling SO_REUSEADDR as 1
    int resuse = 1;

    if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char *)&resuse, sizeof(resuse)) < 0)
    {
        perror("Failed to set the socket OPtion \n");
    }

    //* set server_addr of allbytes to 0 it works on contiguos memory block for sequence of data like charecters
    bzero((char *)&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;          // Address family
    server_addr.sin_port = htons(port_number); // htons is used because port_number should be understood for network

    //*struct in_addr: This is a structure that holds an IPv4 address.
    server_addr.sin_addr.s_addr = INADDR_ANY; //~ any available address

    //* bind() binds a socket to a specific IP address and port,
    if (bind(proxy_socketId, (const struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        perror("Port is not free \n");
        exit(1);
    }

    printf("Binding on Port number %d \n", port_number);

    //*When you call listen(), the server socket goes into "listening" mode.
    // *Clients can start trying to connect, but they will wait in a queue until the server decides to accept them one by one.
    int listen_status = listen(proxy_socketId, MAX_CLIENTS);
    printf("Listening is : %d \n", listen_status);

    if (listen_status < 0)
    {
        perror("While listening Error occured\n");
        exit(1);
    }

    int i = 0;
    int Connected_socketId[MAX_CLIENTS];

    //! Infinete loop for accepting the connections
    while (1)
    {

        bzero((char *)&client_addr, sizeof(client_addr));
        client_len = sizeof(client_addr);

        //&accept() waits for an incoming client connection on the listening socket When a client attempts to connect, accept() establishes the connection.
        //^Upon successful connection, accept() creates a new socket (client_socketId) dedicated to communicating with that specific client.
        client_socketId = accept(proxy_socketId, (struct sockaddr *)&client_addr, (socklen_t *)&client_len);
        printf("Client Socket is connected ");

        if (client_socketId == -1)
        {
            printf("Not able for client to connect to  proxy socket");
            exit(1);
        }

        else
        {
            Connected_socketId[i] = client_socketId;
        }

        struct sockaddr_in *client_pt_helper = (struct sockaddr_in *)&client_addr;

        //& BASICALLY A client pointer
        struct sockaddr_in *client_pt = client_pt_helper;

        //^ accesing an ip address from client pointer it contains binary format 32 bit
        struct in_addr ip_addr = client_pt->sin_addr;

        char str[INET_ADDRSTRLEN];

        //& convert into human redable Ip address
        inet_ntop(AF_INET, &ip_addr, str, INET_ADDRSTRLEN);

        printf("Human redable Ip address %s \n", str);

        //* uint16_t is compatible with in_addr_t
        printf("Client is connected with port number %d and Ip address %s", ntohs(client_addr.sin_port), str);
        //~ thread_fn takes argument as (void *)&Connected_socketId[i]
        pthread_create(&tid[i], NULL, thread_fn, (void *)&Connected_socketId[i]);
        i++;
    }

    //& closes the main socket when the communication is over
    close(proxy_socketId);

    return 0;
}

//! For finding the cache element returns Cache_element
Cache_element *find(char *url)

{
    Cache_element *site = NULL;

    //& when thread A accuries the lock it can enter into critical section and at the same time when thread B  tries to
    //& call pthread_mutex_lock(&lock) it cannot execute any parts of the code
    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);

    if (head != NULL)
    {
        site = head;

        while (site != NULL)
        {

            if (!strcmp(site->url, url))
            {
                printf("LRU Time Track Before : %ld", site->lru_time_track);

                printf("\nurl found\n");

                site->lru_time_track = time(NULL);
                printf("LRU Time Track After : %ld", site->lru_time_track);
                break;
            }
            site = site->next;
        }
    }

    else
    {

        printf("\nurl not found\n");
    }

    temp_lock_val = pthread_mutex_unlock(&lock);

    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);

    return site;
}

//! Removes oldest Cache_element from the cache
void remove_cache_element()
{

    Cache_element *p;    // previous
    Cache_element *q;    // next
    Cache_element *temp; // to delete oldest cache

    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Remove Cache Lock Acquired %d\n", temp_lock_val);
    for (p = head, q = head, temp = head; q->next != NULL; q = q->next)
    {
        if ((q->next)->lru_time_track < temp->lru_time_track)
        {
            temp = q->next;
            p = q;
        }
    }

    if (temp == head)
    {
        head = head->next;
    }
    else
    {
        p->next = temp->next;
    }
    cache_size = cache_size - temp->len - sizeof(Cache_element) - strlen(temp->url) - 1;
    free(temp->data);
    free(temp->url);
    temp->next = NULL;
    free(temp);

    temp_lock_val = pthread_mutex_unlock(&lock);
    printf("Remove Cache Lock Unlocked %d\n", temp_lock_val);
}

//! Adds the Cache_element in cache
int add_cache_element(char *data, int size, char *url)
{

    int temp_lock_val = pthread_mutex_lock(&lock);
    printf("Add Cache Lock Acquired %d\n", temp_lock_val);

    int element_size = 1 + size + strlen(url) + sizeof(Cache_element);

    if (element_size > MAX_ELEMENT_SIZE)
    {

        temp_lock_val = pthread_mutex_unlock(&lock);

        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        return 0;
    }
    else
    {

        //* cache_size denotes  current size of the cache
        while (cache_size + element_size > MAX_SIZE)
        {

            remove_cache_element();
        }
        //* creating the 1 block element of type Cache_element
        Cache_element *element = (Cache_element *)malloc(sizeof(Cache_element));

        element->data = (char *)malloc(size + 1);

        strcpy(element->data, data);

        element->url = (char *)malloc(1 + (strlen(url) * sizeof(char)));
        strcpy(element->url, url);
        element->lru_time_track = time(NULL);
        element->next = head;
        element->len = size;
        head = element;
        cache_size = cache_size + element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
        printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
        return 1;
    }
    return 0;
}