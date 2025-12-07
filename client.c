/* client.c
   FAST-NUCES Campus Messaging Client
   This is the campus client application that connects 
   to the central server.
   Think of it as a local post office for each campus that:
   1. Connects to the main headquarters (server)
   2. Lets departments send/receive messages
   3. Shows announcements from admin
   4. Keeps track of all conversations
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>

#define TCP_PORT 5000
#define SERVER_IP "127.0.0.1"   
#define UDP_SERVER_PORT 6000
#define CLIENT_UDP_PORT 7000
#define MAX_MSG 1024
#define MAX_NAME 40
#define MAX_HISTORY 50

/* Global variables for department and message history */
char campusName[MAX_NAME];
char department[MAX_NAME];  /*Department field */
int tcpSock = -1;
int udpSock = -1;

/*Message history storage */
char messageHistory[MAX_HISTORY][MAX_MSG];
int historyCount = 0;

void showMenu() {
    printf("\n===== %s Campus - %s Department =====\n", campusName, department);
    printf("1. Send message to another campus\n");
    printf("2. View message history\n");
    printf("3. Check online campuses (from server)\n");
    printf("4. Exit\n");
    printf("Choice: ");
}

/*View message history function */
void viewMessageHistory() {
    printf("\n===== MESSAGE HISTORY (%d messages) =====\n", historyCount);
    if(historyCount == 0) {
        printf("No messages yet.\n");
    } else {
        for(int i = 0; i < historyCount; i++) {
            printf("%d. %s\n", i+1, messageHistory[i]);
        }
    }
    printf("=====================================\n");
}

/* UDP: send heartbeat every 10s */
void *udpHeartbeat(void *arg) {
    struct sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(UDP_SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    while(1) {
        /* Send campus|department format */
        char heartbeat[MAX_NAME * 2];
        snprintf(heartbeat, sizeof(heartbeat), "%s|%s", campusName, department);
        sendto(udpSock, heartbeat, strlen(heartbeat), 0, (struct sockaddr*)&serverAddr, sizeof(serverAddr));
        sleep(10);
    }
    return NULL;
}

/* UDP receive: listen for broadcasts */
void *udpReceiver(void *arg) {
    char buf[MAX_MSG];
    struct sockaddr_in from;
    socklen_t flen = sizeof(from);
    while(1) {
        ssize_t n = recvfrom(udpSock, buf, sizeof(buf)-1, 0, (struct sockaddr*)&from, &flen);
        if(n > 0) {
            buf[n] = '\0';
            printf("\n[ADMIN BROADCAST] %s\n", buf);
            
            /* Store broadcast in history */
            if(historyCount < MAX_HISTORY) {
                char formatted[MAX_MSG];
                snprintf(formatted, sizeof(formatted), "[BROADCAST] %s", buf);
                strcpy(messageHistory[historyCount++], formatted);
            }
        }
    }
    return NULL;
}

/* TCP  receive direct messages routed by server */
void *tcpReceiver(void *arg) {
    char buf[MAX_MSG];
    while(1) {
        ssize_t n = read(tcpSock, buf, sizeof(buf)-1);
        if(n <= 0) {
            printf("[CLIENT] Server closed TCP connection.\n");
            close(tcpSock);
            exit(0);
        }
        buf[n] = '\0';
        printf("\n[MSG] %s\n", buf);
        
        /* Store message in history */
        if(historyCount < MAX_HISTORY) {
            strcpy(messageHistory[historyCount++], buf);
        }
    }
    return NULL;
}

int main() {
    char password[60];
    char choice[10];

    printf("===== FAST-NUCES Campus Client =====\n");
    
    /* Get campus name */
    printf("Enter Campus Name (e.g., Lahore): ");
    fgets(campusName, sizeof(campusName), stdin);
    campusName[strcspn(campusName, "\n")] = 0;
    
    /* department selection */
    printf("\nSelect Department:\n");
    printf("1. Admissions\n");
    printf("2. Academics\n");
    printf("3. IT\n");
    printf("4. Sports\n");
    printf("Choice (1-4): ");
    fgets(choice, sizeof(choice), stdin);
    choice[strcspn(choice, "\n")] = 0;
    
    /* Set department based on choice */
    switch(choice[0]) {
        case '1': strcpy(department, "Admissions"); break;
        case '2': strcpy(department, "Academics"); break;
        case '3': strcpy(department, "IT"); break;
        case '4': strcpy(department, "Sports"); break;
        default: strcpy(department, "General"); break;
    }
    
    /* Get password */
    printf("Enter Password for %s: ", campusName);
    fgets(password, sizeof(password), stdin);
    password[strcspn(password, "\n")] = 0;

    /* Create TCP socket and connect */
    tcpSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(TCP_PORT);
    inet_pton(AF_INET, SERVER_IP, &servAddr.sin_addr);
    if(connect(tcpSock, (struct sockaddr*)&servAddr, sizeof(servAddr)) < 0) {
        perror("connect");
        return 1;
    }

    /* Send credentials */
    char cred[120];
    snprintf(cred, sizeof(cred), "%s:%s:%s", campusName, department, password);
    send(tcpSock, cred, strlen(cred), 0);

    /* Wait for auth response */
    char authResponse[50];
    ssize_t authBytes = read(tcpSock, authResponse, sizeof(authResponse)-1);
    if(authBytes > 0) {
        authResponse[authBytes] = '\0';
        if(strcmp(authResponse, "AUTH_OK") != 0) {
            printf("Authentication failed: %s\n", authResponse);
            close(tcpSock);
            return 1;
        }
    }

    /* Create UDP socket and bind to CLIENT_UDP_PORT so server can send broadcast here */
    udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in localAddr;
    localAddr.sin_family = AF_INET;
    localAddr.sin_port = htons(CLIENT_UDP_PORT);
    localAddr.sin_addr.s_addr = INADDR_ANY;
    bind(udpSock, (struct sockaddr*)&localAddr, sizeof(localAddr));

    /* Start UDP heartbeat and receiver threads */
    pthread_t uh, ur, tr;
    pthread_create(&uh, NULL, udpHeartbeat, NULL);
    pthread_create(&ur, NULL, udpReceiver, NULL);

    /* Start TCP receive thread */
    pthread_create(&tr, NULL, tcpReceiver, NULL);

    /* Main menu loop */
    printf("\nConnected and authenticated as %s - %s Department\n", campusName, department);
    printf("Instructions:\n");
    printf("- To send message: TargetCampus,TargetDept,Message\n");
    printf("- Example: Karachi,IT,Hello from Lahore Admissions\n");
    printf("- Departments: Admissions, Academics, IT, Sports\n");
    
    while(1) {
        showMenu();
        fgets(choice, sizeof(choice), stdin);
        choice[strcspn(choice, "\n")] = 0;
        
        switch(choice[0]) {
            case '1': {
                /* Send message */
                char line[MAX_MSG];
                printf("\nEnter message (TargetCampus,TargetDept,Message):\n> ");
                if(!fgets(line, sizeof(line), stdin)) continue;
                line[strcspn(line, "\n")] = 0;
                if(strlen(line) == 0) continue;
                send(tcpSock, line, strlen(line), 0);
                printf("Message sent.\n");
                break;
            }
            case '2': {
                /* View message history */
                viewMessageHistory();
                break;
            }
            case '3': {
                /* Check online campuses, send a request to server */
                char request[] = "LIST_REQUEST";
                send(tcpSock, request, strlen(request), 0);
                printf("Request sent to server. Check received messages.\n");
                break;
            }
            case '4': {
                /* Exit */
                printf("Exiting...\n");
                close(tcpSock);
                close(udpSock);
                exit(0);
            }
            default: {
                printf("Invalid choice. Please enter 1-4.\n");
                break;
            }
        }
    }

    return 0;
}
