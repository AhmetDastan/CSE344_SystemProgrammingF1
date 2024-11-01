#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h> 
#include <arpa/inet.h>
#include <netdb.h> 
#include <math.h> 
#include <time.h> 


#define MAX_COOKS 10
#define MAX_DELIVERY 1000
#define MAX_OVEN_APARATUS 3
#define MAX_OVEN_CAPACITY 6
#define MAX_DELIVERY_BAG_CAPACITY 4
#define BUFFER_SIZE 1024

typedef struct Order {
    struct Order *next;
    int orderId;
    int customerX, customerY; 
    int orderDistanceConstanst; 
    char customerLocation[BUFFER_SIZE];
    char status[BUFFER_SIZE]; 
} Order;

typedef struct {
    Order *head;
    Order *tail;
    pthread_mutex_t queueLock;
    pthread_cond_t queueCond;
} OrderQueue;

typedef struct {
    int id;
    pthread_t thread; 
} Cook;

typedef struct {
    Order *orders[MAX_DELIVERY_BAG_CAPACITY];
    int id; 
    int speed;
    int deliveryScore; 
    int orderCount; 
    pthread_t thread;
} DeliveryPerson;

typedef struct {
    int capacity;
    int mealsInside;
    pthread_mutex_t placeLock;
    pthread_mutex_t removeLock;
} Oven;

typedef struct {
    int port;
    int cookPoolSize;
    int deliveryPoolSize;
    int speed;
    int serverSocket;
    int numCooks;
    int numDelivery;
    Cook cooks[MAX_COOKS];
    DeliveryPerson delivery[MAX_DELIVERY];
    pthread_t managerThread;
    pthread_mutex_t logLock;
    pthread_mutex_t ovenLock;
    pthread_mutex_t deliveryBagLock;
    Oven ovens[MAX_OVEN_APARATUS];
    OrderQueue orderQueue;
    OrderQueue ovenQueue;
    OrderQueue deliveryQueue;
    FILE *logFile;
} PideShopServer;
 
typedef struct {
    double real;
    double imag;
} Complex;

Complex complex_conj(Complex a) {
    Complex result;
    result.real = a.real;
    result.imag = -a.imag;
    return result;
}

// thread functions
void *cookThread(void *arg);
void *deliveryThread(void *arg);
void *clientHandler(void *arg);
void *managerHandler(void *arg);

// initilise structs and queue
void initCooks(PideShopServer *server);
void initDelivery(PideShopServer *server);
void initServer(PideShopServer *server, int port, int cookPoolSize, int deliveryPoolSize, int speed);
void initQueue(OrderQueue *queue);
void startServer();
void returnTimeOfMatrix();

void enqueue(OrderQueue *queue, Order *order);
Order *dequeue(OrderQueue *queue);
void printBestDeliveryPerson(PideShopServer *server);
void closeServer(PideShopServer *server);  

// global variables
PideShopServer server;
pthread_t managerThread;
int totalOrdersPlaced = 0;
int totalOrdersCompleted = 0; 
pthread_mutex_t countLock;
pthread_mutex_t logMutex; 
pthread_mutex_t cancelOrderMutex;

char logText[BUFFER_SIZE];
int orderState = -1, orderCtrl = -1; // -3 canceled orders, -2 newStart, -1 doesnt start , 0 start , 1 finished , 2 stuck

void logMessage(const char *message) {
    pthread_mutex_lock(&server.logLock);
    fprintf(server.logFile, "%s", message);
    fflush(server.logFile);
    logText[0] = '\0'; 
    pthread_mutex_unlock(&server.logLock);
}

void handleSignal(int signum) {
    logMessage("Server shutting down...");
    closeServer(&server);
    exit(0);
}
int main(int argc, char *argv[]) {

    if (argc != 6) {
        printf("Wrong Argument, please enter proper arguments: [ip] [portnumber] [CookthreadPoolSize] [DeliveryPoolSize] [k] \n");
        exit(1);
    } 
 
    char *ip = argv[1];
    int port = atoi(argv[2]);
    int cookPoolSize = atoi(argv[3]);
    int deliveryPoolSize = atoi(argv[4]);
    int speed = atoi(argv[5]);

    initServer(&server, port, cookPoolSize, deliveryPoolSize, speed);

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);
 

    server.serverSocket = socket(AF_INET, SOCK_STREAM, 0);
    if (server.serverSocket == -1) {
        perror("Socket creation failed");
        exit(1);
    }

    pthread_mutex_init(&countLock, NULL); 
    pthread_mutex_init(&logMutex, NULL);
    pthread_mutex_init(&cancelOrderMutex, NULL);
    

    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = inet_addr(ip); // spesific ip number can be use
    //serverAddr.sin_addr.s_addr = INADDR_ANY; // any ip number can be use

    if (bind(server.serverSocket, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Bind failed");
        close(server.serverSocket);
        exit(1);
    }

    if (listen(server.serverSocket, 10) == -1) {
        perror("Listen failed");
        close(server.serverSocket);
        exit(1);
    }
    
    snprintf(logText, sizeof(logText), "Server listening on port %d\n", port);
    logMessage(logText); 

    // join manager thread
    if(pthread_create(&managerThread, NULL, managerHandler, &managerThread) != 0){
        perror("Manager thread creation failed");
        closeServer(&server);
        exit(1);
    }
 
    printf("PideShop active waiting for connection... \n");
    while (1) { 
        int clientSocket = accept(server.serverSocket, NULL, NULL);
        if (clientSocket == -1) { 
            snprintf(logText, sizeof(logText), "Socket Accept failed\n");
            logMessage(logText); 
            continue;
        }
        pthread_t clientThread; 
        pthread_create(&clientThread, NULL, clientHandler, (void *)(intptr_t)clientSocket);
        pthread_detach(clientThread); 
    } 

    if(pthread_join(managerThread, NULL) != 0){
        perror("Manager thread join failed");
        closeServer(&server);
        exit(1);
    }

    closeServer(&server);
    return 0;
} 

void initQueue(OrderQueue *queue) {
    queue->head = NULL;
    queue->tail = NULL; 
    pthread_mutex_init(&queue->queueLock, NULL);
    pthread_cond_init(&queue->queueCond, NULL);
}

void enqueue(OrderQueue *queue, Order *order) {
    order->next = NULL;
    pthread_mutex_lock(&queue->queueLock);
    if (queue->tail == NULL) {
        queue->head = order;
        queue->tail = order;
    } else {
        queue->tail->next = order;
        queue->tail = order;
    }
    pthread_cond_signal(&queue->queueCond);
    pthread_mutex_unlock(&queue->queueLock);
}

Order *dequeue(OrderQueue *queue) {
    pthread_mutex_lock(&queue->queueLock);
    while (queue->head == NULL) {  
        if(orderState == 2) break;
        pthread_cond_wait(&queue->queueCond, &queue->queueLock);  
    }
    Order *order = queue->head;
    queue->head = order->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    pthread_mutex_unlock(&queue->queueLock);
    return order;
}

void initServer(PideShopServer *server, int port, int cookPoolSize, int deliveryPoolSize, int speed) {
    server->port = port;
    server->cookPoolSize = cookPoolSize;
    server->deliveryPoolSize = deliveryPoolSize;
    server->speed = speed;
    server->numCooks = 0;
    server->numDelivery = 0;
    orderCtrl = -1;
    pthread_mutex_init(&server->logLock, NULL);
    pthread_mutex_init(&server->ovenLock, NULL);
    pthread_mutex_init(&server->deliveryBagLock, NULL);
    for (int i = 0; i < MAX_OVEN_APARATUS; ++i) {
        server->ovens[i].capacity = MAX_OVEN_CAPACITY;
        server->ovens[i].mealsInside = 0;
        pthread_mutex_init(&server->ovens[i].placeLock, NULL);
        pthread_mutex_init(&server->ovens[i].removeLock, NULL);
    }
    initQueue(&server->orderQueue);
    initQueue(&server->ovenQueue);
    initQueue(&server->deliveryQueue);
    initCooks(server);
    initDelivery(server);

    server->logFile = fopen("server.log", "a");
    if (server->logFile == NULL) {
        perror("Failed to open log file");
        exit(1);
    }
}

void startServer(){ 
    server.numCooks = 0;
    server.numDelivery = 0;
    orderCtrl = -1;
    for (int i = 0; i < MAX_OVEN_APARATUS; ++i) {
        server.ovens[i].capacity = MAX_OVEN_CAPACITY;
        server.ovens[i].mealsInside = 0; 
    }
    initQueue(&server.orderQueue);
    initQueue(&server.ovenQueue);
    initQueue(&server.deliveryQueue);
    initCooks(&server);
    initDelivery(&server);
 
}
void initCooks(PideShopServer *server) {
    for (int i = 0; i < server->cookPoolSize; ++i) {
        server->cooks[i].id = i; 
        pthread_create(&server->cooks[i].thread, NULL, cookThread, &server->cooks[i]);
        server->numCooks++;
    }
}

void initDelivery(PideShopServer *server) {
    for (int i = 0; i < server->deliveryPoolSize; ++i) {
        server->delivery[i].id = i;
        server->delivery[i].speed = server->speed; 
        server->delivery[i].deliveryScore = 0; // Initialize delivery score
        server->delivery[i].orderCount = 0;
        pthread_create(&server->delivery[i].thread, NULL, deliveryThread, &server->delivery[i]);
        server->numDelivery++;
    }
}

void closeServer(PideShopServer *server) {
    close(server->serverSocket);

    // Stop cooks
    for (int i = 0; i < server->cookPoolSize; ++i) {
        pthread_cancel(server->cooks[i].thread);
    }

    // Stop delivery personnel
    for (int i = 0; i < server->deliveryPoolSize; ++i) {
        pthread_cancel(server->delivery[i].thread);
    }

    pthread_mutex_destroy(&server->logLock);
    pthread_mutex_destroy(&server->ovenLock);
    pthread_mutex_destroy(&server->deliveryBagLock);
    pthread_mutex_destroy(&cancelOrderMutex);
    pthread_mutex_destroy(&countLock);
    for (int i = 0; i < MAX_OVEN_APARATUS; ++i) {
        pthread_mutex_destroy(&server->ovens[i].placeLock);
        pthread_mutex_destroy(&server->ovens[i].removeLock);
    }
    snprintf(logText, sizeof(logText), "Sever closed successfully \n");
    logMessage(logText); 
    
    // Close the log file
    if (server->logFile) {
        fclose(server->logFile);
        server->logFile = NULL;
    }
}

void *cookThread(void *arg) {
    Cook *cook = (Cook *)arg;
    while (1) {
        pthread_mutex_lock(&cancelOrderMutex);
        if(orderState == -3) break;
        pthread_mutex_lock(&server.ovenLock);
        Order *order = dequeue(&server.orderQueue);
        pthread_mutex_unlock(&server.ovenLock);
        if (order != NULL) {
            pthread_mutex_lock(&logMutex); 
            snprintf(logText, sizeof(logText), "Cook %d: Preparing order %d\n", cook->id, order->orderId);
            logMessage(logText); 
            strcpy(order->status, "Preparing");
            pthread_mutex_unlock(&logMutex);

            returnTimeOfMatrix(); // calculated under the code below

            pthread_mutex_lock(&logMutex); 
            snprintf(logText, sizeof(logText), "Cook %d: Cooking order %d\n", cook->id, order->orderId);
            logMessage(logText); 
            strcpy(order->status, "Cooking");
            pthread_mutex_unlock(&logMutex);

            int placed = 0;
            while (!placed) {
                for (int i = 0; i < MAX_OVEN_APARATUS; ++i) {
                    pthread_mutex_lock(&server.ovens[i].placeLock);
                    if (server.ovens[i].mealsInside < server.ovens[i].capacity) {
                        server.ovens[i].mealsInside++;
                        pthread_mutex_lock(&logMutex);
                        
                        order->orderDistanceConstanst = (order->customerX * order->customerX + order->customerY * order->customerY) / 2;
                        if(order->orderDistanceConstanst == 0) order->orderDistanceConstanst =1;
 
                        snprintf(logText, sizeof(logText), "Cook %d: Placed order %d in oven with aparatus %d\n", cook->id, order->orderId, i);
                        logMessage(logText); 
                        pthread_mutex_unlock(&logMutex);
                        placed = 1;
                        pthread_mutex_unlock(&server.ovens[i].placeLock);
                        break;
                    }
                    pthread_mutex_unlock(&server.ovens[i].placeLock);
                } 
            }

            returnTimeOfMatrix(); // cooking time takes half of preparation time

            int removed = 0;
            while (!removed) {
                for (int i = 0; i < MAX_OVEN_APARATUS; ++i) {
                    pthread_mutex_lock(&server.ovens[i].removeLock);
                    if (server.ovens[i].mealsInside > 0) {
                        server.ovens[i].mealsInside--;
                        pthread_mutex_lock(&logMutex); 
                        snprintf(logText, sizeof(logText), "Cook %d: Removed order %d from oven with aparatus%d\n", cook->id, order->orderId, i);
                        logMessage(logText);
                        
                        strcpy(order->status, "Cooked");
                        pthread_mutex_unlock(&logMutex);
                        removed = 1;
                        pthread_mutex_unlock(&server.ovens[i].removeLock);
                        break;
                    }
                    pthread_mutex_unlock(&server.ovens[i].removeLock);
                } 
            }

            enqueue(&server.ovenQueue, order);
        }
        pthread_mutex_unlock(&cancelOrderMutex);
    }
    return NULL;
}

void *deliveryThread(void *arg) {
    DeliveryPerson *deliveryPerson = (DeliveryPerson *)arg; 
    while (1) { 
        //pthread_mutex_lock(&cancelOrderMutex);
        if(orderState == -3) break;
        pthread_mutex_lock(&server.deliveryBagLock);
        
        while (deliveryPerson->orderCount < MAX_DELIVERY_BAG_CAPACITY) {
            pthread_mutex_lock(&countLock);
            if(orderState == 2) {
                orderState = 1;
                break;
            }
            pthread_mutex_unlock(&countLock);
            Order *order = dequeue(&server.ovenQueue);
            if (order != NULL) {
                deliveryPerson->orders[deliveryPerson->orderCount++] = order;
                pthread_mutex_lock(&logMutex); 
                snprintf(logText, sizeof(logText), "Delivery %d: Picked up order %d\n", deliveryPerson->id, order->orderId);
                logMessage(logText);
                pthread_mutex_unlock(&logMutex);

                // Increment totalOrdersCompleted 
                pthread_mutex_lock(&countLock); 
                ++totalOrdersCompleted;
                pthread_mutex_unlock(&countLock);

            } else {
                break;
            }
        }
 
        if (deliveryPerson->orderCount > 0) {
            pthread_mutex_lock(&logMutex); 
            snprintf(logText, sizeof(logText), "Delivery %d: Starting delivery with %d orders\n", deliveryPerson->id, deliveryPerson->orderCount);
            logMessage(logText);
            pthread_mutex_unlock(&logMutex);

            for (int i = 0; i < deliveryPerson->orderCount; ++i) {
                Order *order = deliveryPerson->orders[i];
                pthread_mutex_lock(&logMutex);
                if(snprintf(logText, sizeof(logText), "Delivery %d: Delivering order %d to %s\n", deliveryPerson->id, order->orderId, order->customerLocation) < 0){
                    printf("buffer problem with snprintf in clientHandler\n");
                } 
                
                logMessage(logText);
                strcpy(order->status, "Delivering");
                pthread_mutex_unlock(&logMutex);

                sleep(server.speed / order->orderDistanceConstanst); //  sleepTime = distance / speed

                pthread_mutex_lock(&logMutex); 
                snprintf(logText, sizeof(logText), "Delivery %d: Delivered order %d\n", deliveryPerson->id, order->orderId);
                logMessage(logText);
                strcpy(order->status, "Delivered");
                pthread_mutex_unlock(&logMutex);

                deliveryPerson->deliveryScore += 10; // Update delivery score for each successful delivery
                free(order); // Clean up the order 
            }

            deliveryPerson->orderCount = 0; // Reset order count after delivery
        }
 

        pthread_mutex_unlock(&server.deliveryBagLock); 
        //pthread_mutex_unlock(&cancelOrderMutex);
    }
    return NULL;
}

void *clientHandler(void *arg) {
    pthread_mutex_lock(&countLock); 
    if(orderState == -1) orderState = -2; 
    pthread_mutex_unlock(&countLock);
    
    int clientSocket = (int)(intptr_t)arg;
    char buffer[BUFFER_SIZE];
    int bytesReceived = recv(clientSocket, buffer, sizeof(buffer) - 1, 0);
     
    if (bytesReceived > 0) {
        buffer[bytesReceived] = '\0'; 

        if (strcmp(buffer, "cancelOrder") == 0) { 
            snprintf(logText, sizeof(logText), "Received cancel order as a request..\n");
            logMessage(logText);
            pthread_mutex_lock(&cancelOrderMutex);
            orderState = -3; 
            pthread_mutex_unlock(&cancelOrderMutex);
            return NULL;
        }

        Order *newOrder = (Order *)malloc(sizeof(Order));
        newOrder->orderId = rand() % 100000; 
        sscanf(buffer, "%d-%d-%d", &newOrder->customerX, &newOrder->customerY, &totalOrdersPlaced);
        
        if(newOrder->customerX == -999 && newOrder->customerY == -999){ // last element come, finished operations 
            logMessage(logText);
            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "All customers served!");
            send(clientSocket, response, strlen(response), 0);
            
            //orderState = 1; 
        }else{  
            snprintf(logText, sizeof(logText), "Customer location: %d %d\n", newOrder->customerX, newOrder->customerY);
            logMessage(logText);
            strcpy(newOrder->status, "Received");

            if(snprintf(logText, sizeof(logText), "Order %d created for location %s\n", newOrder->orderId, newOrder->customerLocation) < 0){
                printf("buffer problem with snprintf in clientHandler\n");
            }
            
            logMessage(logText); 
 
            enqueue(&server.orderQueue, newOrder);

            char response[BUFFER_SIZE];
            snprintf(response, sizeof(response), "Order %d has been placed successfully!", newOrder->orderId);
            send(clientSocket, response, strlen(response), 0);
        } 
    }

    close(clientSocket);
    return NULL;
}

void *managerHandler(void *arg){ 
    while(1){ 
        pthread_mutex_lock(&countLock); 
        if(orderState == -3){  // canceled operation 
            printf("canceling orders..\n");
            orderState = -1;
            totalOrdersPlaced = 0;
            totalOrdersCompleted = 0;
            pthread_mutex_unlock(&countLock);
            startServer(&server);
            continue;
        }
        else if(orderState == -2){
            printf("%d new customer.. Serving ", totalOrdersPlaced);
            orderState = 0;
        }
        else if(orderState == 0){ // order runs  - // if order state equals 0 and totalOrdersCompleted is not changed, orderState = 2
            if((totalOrdersCompleted % MAX_DELIVERY_BAG_CAPACITY) == 0){ 
                if((totalOrdersPlaced - totalOrdersCompleted) < MAX_DELIVERY_BAG_CAPACITY) orderState = 2;                
            }
        }else if(orderState == 1){ // order finished 
            printf("done serving client @ %d\n", getpid()); 
            snprintf(logText, sizeof(logText), "done serving client @ PID %d\n", getpid());
            logMessage(logText);

            snprintf(logText, sizeof(logText), "All operations are done.\n");
            logMessage(logText); 
            printBestDeliveryPerson(&server); // Print best delivery person before shutdown
            orderState = -1;
            startServer(&server);
            printf("active waiting for connections\n");
        }else if(orderState == 2){ // stuck orders
            //bitir isi
            orderState = 1;
        }
        pthread_mutex_unlock(&countLock); 
    }
}

void printBestDeliveryPerson(PideShopServer *server) {
    int bestScore = -1;
    int bestDeliveryPersonId = -1;
    for (int i = 0; i < server->deliveryPoolSize; ++i) {
        if (server->delivery[i].deliveryScore > bestScore) {
            bestScore = server->delivery[i].deliveryScore;
            bestDeliveryPersonId = server->delivery[i].id;
        }
    }
    if (bestDeliveryPersonId != -1) {
        printf("Thanks Cook %d and Moto%d \n",1, bestDeliveryPersonId);
        snprintf(logText, sizeof(logText), "Best Delivery Person: %d with a score of %d\n", bestDeliveryPersonId, bestScore);
        logMessage(logText); 
    } else {
        printf("No deliveries were made.\n");
    }
}
 
void returnTimeOfMatrix() {
    int ROWS = 30, COLS = 40;
    
    Complex A[ROWS][COLS];
    
    // Initialize A with some complex values
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            A[i][j].real = rand() % 10;
            A[i][j].imag = rand() % 10;
        } 
    }

    Complex B[COLS][ROWS];
 
    for (int i = 0; i < ROWS; ++i) {
        for (int j = 0; j < COLS; ++j) {
            B[j][i] = complex_conj(A[i][j]);
        }
    }  
    
}