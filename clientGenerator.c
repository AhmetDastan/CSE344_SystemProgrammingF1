// hungry_very_much.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <arpa/inet.h>
#include <time.h>

#define BUFFER_SIZE 2048

typedef struct {
    char *serverIp;
    int serverPort;  
    int x;
    int y; 
    int totalOrderAmount;
    int clientId; 
} Client;
 
void sendOrder(Client *client); 

Client client;

void handle_signal(int signum) { 
    printf("signal .. cancelling orders .. editing log..\n"); 
    exit(0);
}
int main(int argc, char *argv[]) {

    if (argc != 6) {
        printf("Wrong Argument, please enter proper arguments: [ip] [portnumber] [numberOfClients] [p] [q]\n");
        exit(1);
    } 
  
    client.serverIp = argv[1]; 
    client.serverPort = atoi(argv[2]);
    client.totalOrderAmount = atoi(argv[3]);
    int p = atoi(argv[4]);
    int q = atoi(argv[5]);

    srand(time(NULL));
    
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal); 

    printf("PID %d..\n",getpid());
    printf("...\n");
    for (int i = 0; i < client.totalOrderAmount; ++i) {
        if(rand() % 2 == 0)
            client.x = -1 * (rand() % p);
        else client.x = (rand() % p);

        if(rand() % 2 == 0)
            client.y = -1 * (rand() % q);
        else client.y = (rand() % q); 
        
        sendOrder(&client); 
        sleep(0.1);
    }

    // send signal to server to stop
    client.x = -999;
    client.y = -999;
    sendOrder(&client);
    printf("log file written ..\n");
    
    return 0;
} 

void sendOrder(Client *client) { 
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    struct sockaddr_in server;
    server.sin_family = AF_INET; // AF_UNIX or af_local for local communication
    server.sin_port = htons(client->serverPort);

    if (inet_pton(AF_INET, client->serverIp, &server.sin_addr) <= 0) { // it is used when we wanna connect different computer with different ip address
        perror("Invalid server IP");
        close(sock);
        exit(1);
    }

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1) {
        perror("Connection to server failed");
        close(sock);
        exit(1);
    }

    char message[BUFFER_SIZE];
    snprintf(message, sizeof(message), "%d-%d-%d", client->x, client->y, client->totalOrderAmount); 
    send(sock, message, strlen(message), 0);

    char response[BUFFER_SIZE];
    int bytes_received = recv(sock, response, sizeof(response) - 1, 0);
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        printf("%s\n", response);
    } else {
        printf("Failed to receive response from server.\n");
    }

    close(sock);
}
 
