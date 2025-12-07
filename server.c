/* server.c
   Central Server for Multi-Campus Communication System
   Features:
   - TCP port 5000: Handles client connections, authentication, and messaging
   - UDP port 6000: Receives heartbeats and sends broadcast messages  
   - Admin console: Commands "list" (show connected campuses) 
     and "broadcast <message>"
   - Department-level routing: Messages can be sent to specific departments within campuses
*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <time.h>

#define MAX_CLIENTS 10
#define TCP_PORT 5000
#define UDP_PORT 6000
#define MAX_NAME 40
#define MAX_MSG 1024

/* credentials */
struct Cred { char campus[MAX_NAME]; char password[MAX_NAME]; };
struct Cred validCreds[] = {
    {"Lahore","NU-LHR-123"},
    {"Karachi","NU-KHI-123"},
    {"Peshawar","NU-PSH-123"},
    {"CFD","NU-CFD-123"},
    {"Multan","NU-MTN-123"}
};
int numCreds = 5;

/* Per-client info */
int tcpSockets[MAX_CLIENTS];
char clientCampus[MAX_CLIENTS][MAX_NAME];
char clientDept[MAX_CLIENTS][MAX_NAME];  /* Department field */
int clientCount = 0;

/* For UDP broadcast: store last known UDP sockaddr_in for each campus and timestamp */
struct sockaddr_in udpAddr[MAX_CLIENTS];
time_t lastSeen[MAX_CLIENTS];
int udpKnown[MAX_CLIENTS]; /* 0 = unknown, 1 = known */

pthread_mutex_t clientsLock = PTHREAD_MUTEX_INITIALIZER;

/* Helper authenticate */
int authenticate(const char *campus, const char *pass) {
    for(int i=0;i<numCreds;i++) {
        if(strcmp(validCreds[i].campus, campus)==0 && strcmp(validCreds[i].password, pass)==0)
            return 1;
    }
    return 0;
}

/* Find client index by campus name, -1 if not found */
int findClientByCampus(const char *campus) {
    for(int i=0;i<clientCount;i++) if(strcmp(clientCampus[i], campus)==0) return i;
    return -1;
}

/* Find client by campus AND department */
int findClientByCampusAndDept(const char *campus, const char *dept) {
    for(int i=0;i<clientCount;i++) {
        if(strcmp(clientCampus[i], campus)==0 && strcmp(clientDept[i], dept)==0)
            return i;
    }
    return -1;
}

/* Handle LIST_REQUEST from client */
void handleListRequest(int sock, int clientIndex) {
    pthread_mutex_lock(&clientsLock);
    
    /* Build list of connected campuses */
    char listMsg[MAX_MSG * MAX_CLIENTS];
    strcpy(listMsg, "[SERVER] Connected Campuses:\n");
    
    if(clientCount == 0) {
        strcat(listMsg, "  No campuses connected.\n");
    } else {
        for(int i = 0; i < clientCount; i++) {
            char entry[100];
            char tsbuf[64] = "never";
            
            if(udpKnown[i]) {
                struct tm *tm = localtime(&lastSeen[i]);
                strftime(tsbuf, sizeof(tsbuf), "%H:%M:%S", tm);
            }
            
            snprintf(entry, sizeof(entry), "  %d. %s - %s (Last seen: %s)\n", 
                    i+1, clientCampus[i], clientDept[i], tsbuf);
            strcat(listMsg, entry);
        }
    }
    
    strcat(listMsg, "----------------------------\n");
    
    /* Send the list to the requesting client */
    send(sock, listMsg, strlen(listMsg), 0);
    
    pthread_mutex_unlock(&clientsLock);
    printf("[SERVER] Sent campus list to %s %s\n", clientCampus[clientIndex], clientDept[clientIndex]);
}

/* handle TCP client messages */
void *clientHandler(void *arg) {
    int index = *((int*)arg);
    int sock = tcpSockets[index];
    char buf[MAX_MSG];
    while(1) {
        ssize_t n = read(sock, buf, sizeof(buf)-1);
        if(n <= 0) {
            printf("[SERVER] %s %s disconnected or socket closed.\n", clientCampus[index], clientDept[index]);
            close(sock);

            pthread_mutex_lock(&clientsLock);
            /* remove client by shifting arrays left */
            for(int j=index;j<clientCount-1;j++) {
                tcpSockets[j] = tcpSockets[j+1];
                strcpy(clientCampus[j], clientCampus[j+1]);
                strcpy(clientDept[j], clientDept[j+1]);  /* Copy department */
                udpAddr[j] = udpAddr[j+1];
                lastSeen[j] = lastSeen[j+1];
                udpKnown[j] = udpKnown[j+1];
            }
            clientCount--;
            pthread_mutex_unlock(&clientsLock);
            pthread_exit(NULL);
        }
        buf[n] = '\0';
        printf("[TCP][%s %s] >> %s\n", clientCampus[index], clientDept[index], buf);

        /* Check if this is a LIST_REQUEST */
        if(strcmp(buf, "LIST_REQUEST") == 0) {
            handleListRequest(sock, index);
            continue;
        }

        char tgtCampus[MAX_NAME], tgtDept[MAX_NAME], message[MAX_MSG];
        
        char *firstPipe = strchr(buf, ',');
        if(firstPipe == NULL) {
            printf("[SERVER] Invalid message format from %s. Use TargetCampus,Dept,Message\n", clientCampus[index]);
            char reply[MAX_MSG] = "[SERVER] Error: Use format TargetCampus,Dept,Message";
            send(sock, reply, strlen(reply), 0);
            continue;
        }
        int pos1 = firstPipe - buf;
        if(pos1 >= MAX_NAME) pos1 = MAX_NAME-1;
        strncpy(tgtCampus, buf, pos1); 
        tgtCampus[pos1] = '\0';
        
        char *secondPipe = strchr(firstPipe + 1, ',');
        if(secondPipe == NULL) {
            printf("[SERVER] Invalid message format from %s. Use TargetCampus,Dept,Message\n", clientCampus[index]);
            char reply[MAX_MSG] = "[SERVER] Error: Use format TargetCampus,Dept,Message";
            send(sock, reply, strlen(reply), 0);
            continue;
        }
        int pos2 = secondPipe - (firstPipe + 1);
        if(pos2 >= MAX_NAME) pos2 = MAX_NAME-1;
        strncpy(tgtDept, firstPipe + 1, pos2);
        tgtDept[pos2] = '\0';
        
        /* Get Message */
        strcpy(message, secondPipe + 1);

        pthread_mutex_lock(&clientsLock);
        int destIdx = findClientByCampusAndDept(tgtCampus, tgtDept);
        if(destIdx == -1) {
            /* Try to find any client from that campus if department not found */
            int campusIdx = findClientByCampus(tgtCampus);
            if(campusIdx == -1) {
                char reply[MAX_MSG];
                snprintf(reply, sizeof(reply), "[SERVER] Target campus %s not connected.", tgtCampus);
                send(sock, reply, strlen(reply), 0);
                printf("[SERVER] Could not route message from %s %s to %s %s (not connected).\n", 
                       clientCampus[index], clientDept[index], tgtCampus, tgtDept);
            } else {
                /* Forward to any department in that campus */
                char forward[MAX_MSG];
                snprintf(forward, sizeof(forward), "[%s %s -> %s %s] %s", 
                        clientCampus[index], clientDept[index], tgtCampus, tgtDept, message);
                send(tcpSockets[campusIdx], forward, strlen(forward), 0);
                printf("[SERVER] Routed message from %s %s to %s (department %s not found, sent to campus).\n", 
                       clientCampus[index], clientDept[index], tgtCampus, tgtDept);
            }
        } else {
            /* Exact match found - send to specific department */
            char forward[MAX_MSG];
            snprintf(forward, sizeof(forward), "[%s %s -> %s %s] %s", 
                    clientCampus[index], clientDept[index], tgtCampus, tgtDept, message);
            send(tcpSockets[destIdx], forward, strlen(forward), 0);
            printf("[SERVER] Routed message from %s %s to %s %s.\n", 
                   clientCampus[index], clientDept[index], tgtCampus, tgtDept);
        }
        pthread_mutex_unlock(&clientsLock);
    }
    return NULL;
}

/*UDP listener for heartbeats (port 6000) */
void *udpListener(void *arg) {
    int udpSock = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in servAddr, cliAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(UDP_PORT);
    servAddr.sin_addr.s_addr = INADDR_ANY;
    bind(udpSock, (struct sockaddr*)&servAddr, sizeof(servAddr));
    char buf[256];
    socklen_t addrLen = sizeof(cliAddr);
    while(1) {
        ssize_t n = recvfrom(udpSock, buf, sizeof(buf)-1, 0, (struct sockaddr*)&cliAddr, &addrLen);
        if(n > 0) {
            buf[n] = '\0';
            /* Parse campus|dept from heartbeat */
            char campusName[MAX_NAME], deptName[MAX_NAME];
            char *pipe = strchr(buf, '|');
            if(pipe == NULL) {

                strncpy(campusName, buf, MAX_NAME-1); 
                campusName[MAX_NAME-1]=0;
                strcpy(deptName, "Unknown");
            } else {
                /* New format: campus|dept */
                int pos = pipe - buf;
                if(pos >= MAX_NAME) pos = MAX_NAME-1;
                strncpy(campusName, buf, pos); 
                campusName[pos] = '\0';
                strncpy(deptName, pipe + 1, MAX_NAME-1); 
                deptName[MAX_NAME-1] = '\0';
            }
            
            pthread_mutex_lock(&clientsLock);
            /* Try to find by campus AND department first */
            int idx = findClientByCampusAndDept(campusName, deptName);
            if(idx >= 0) {
                udpAddr[idx] = cliAddr;
                lastSeen[idx] = time(NULL);
                udpKnown[idx] = 1;
                printf("[UDP][HEARTBEAT] %s %s (stored UDP addr). LastSeen updated.\n", campusName, deptName);
            } else {
                /* Fallback: find by campus only */
                idx = findClientByCampus(campusName);
                if(idx >= 0) {
                    udpAddr[idx] = cliAddr;
                    lastSeen[idx] = time(NULL);
                    udpKnown[idx] = 1;
                    printf("[UDP][HEARTBEAT] %s (department %s, stored UDP addr). LastSeen updated.\n", campusName, deptName);
                } else {
                    printf("[UDP][HEARTBEAT] Received from %s %s but no TCP session found.\n", campusName, deptName);
                }
            }
            pthread_mutex_unlock(&clientsLock);
        }
    }
    return NULL;
}

void *adminConsole(void *arg) {
    char line[1024];
    while(1) {
        if(!fgets(line, sizeof(line), stdin)) continue;
        line[strcspn(line, "\n")] = 0;
        if(strncmp(line, "list", 4)==0) {
            pthread_mutex_lock(&clientsLock);
            printf("---- Connected campuses (%d) ----\n", clientCount);
            for(int i=0;i<clientCount;i++) {
                char tsbuf[64] = "never";
                if(udpKnown[i]) {
                    struct tm *tm = localtime(&lastSeen[i]);
                    strftime(tsbuf, sizeof(tsbuf), "%Y-%m-%d %H:%M:%S", tm);
                }
                printf("%d) %s | Dept: %s | TCPFD=%d | UDP known=%d | lastSeen=%s\n", 
                       i+1, clientCampus[i], clientDept[i], tcpSockets[i], udpKnown[i], tsbuf);
            }
            printf("------------------------------\n");
            pthread_mutex_unlock(&clientsLock);
        } else if(strncmp(line, "broadcast ", 10)==0) {
            char *msg = line + 10;
            pthread_mutex_lock(&clientsLock);
            int udpSock = socket(AF_INET, SOCK_DGRAM, 0);
            for(int i=0;i<clientCount;i++) {
                if(udpKnown[i]) {
                    sendto(udpSock, msg, strlen(msg), 0, (struct sockaddr*)&udpAddr[i], sizeof(udpAddr[i]));
                }
            }
            close(udpSock);
            printf("[ADMIN] Broadcast sent to %d clients: %s\n", clientCount, msg);
            pthread_mutex_unlock(&clientsLock);
        } else {
            printf("Admin commands: 'list' or 'broadcast <message>'\n");
        }
    }
    return NULL;
}

int main() {
    pthread_t up;
    pthread_create(&up, NULL, udpListener, NULL);
    pthread_t adm;
    pthread_create(&adm, NULL, adminConsole, NULL);

    int serverSock = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in servAddr;
    servAddr.sin_family = AF_INET;
    servAddr.sin_port = htons(TCP_PORT);
    servAddr.sin_addr.s_addr = INADDR_ANY;
    bind(serverSock, (struct sockaddr*)&servAddr, sizeof(servAddr));
    listen(serverSock, 5);
    printf("[SERVER] TCP listening on port %d\n", TCP_PORT);
    printf("[SERVER] UDP listening on port %d\n", UDP_PORT);
    printf("[SERVER] Admin console ready. Type 'list' or 'broadcast <message>'\n");

    while(1) {
        int clientSock = accept(serverSock, NULL, NULL);
        printf("[SERVER] New TCP client connected, awaiting credentials...\n");
        char buf[256];
        ssize_t n = read(clientSock, buf, sizeof(buf)-1);
        if(n <= 0) { close(clientSock); continue; }
        buf[n] = '\0';
        
        /* Parse campus:dept:password format */
        char campus[MAX_NAME], dept[MAX_NAME], pass[MAX_NAME];
        
        /* Find first colon (campus:...) */
        char *firstColon = strchr(buf, ':');
        if(!firstColon) { 
            send(clientSock, "BAD_FORMAT: Use Campus:Dept:Password", 35, 0); 
            close(clientSock); 
            continue; 
        }
        
        /* Find second colon (campus:dept:...) */
        char *secondColon = strchr(firstColon + 1, ':');
        if(!secondColon) { 
            send(clientSock, "BAD_FORMAT: Use Campus:Dept:Password", 35, 0); 
            close(clientSock); 
            continue; 
        }
        
        /* Extract campus */
        int pos1 = firstColon - buf;
        if(pos1 >= MAX_NAME) pos1 = MAX_NAME-1;
        strncpy(campus, buf, pos1); 
        campus[pos1] = '\0';
        
        /* Extract department */
        int pos2 = secondColon - (firstColon + 1);
        if(pos2 >= MAX_NAME) pos2 = MAX_NAME-1;
        strncpy(dept, firstColon + 1, pos2); 
        dept[pos2] = '\0';
        
        /* Extract password */
        strncpy(pass, secondColon + 1, MAX_NAME-1); 
        pass[MAX_NAME-1] = '\0';
        pass[strcspn(pass, "\n")] = 0;

        if(!authenticate(campus, pass)) {
            printf("[SERVER] Authentication FAILED for %s %s\n", campus, dept);
            send(clientSock, "AUTH_FAILED", 11, 0);
            close(clientSock);
            continue;
        }
        pthread_mutex_lock(&clientsLock);
        if(clientCount >= MAX_CLIENTS) {
            pthread_mutex_unlock(&clientsLock);
            send(clientSock, "SERVER_FULL", 11, 0);
            close(clientSock);
            continue;
        }
        tcpSockets[clientCount] = clientSock;
        strncpy(clientCampus[clientCount], campus, MAX_NAME-1);
        clientCampus[clientCount][MAX_NAME-1] = 0;
        strncpy(clientDept[clientCount], dept, MAX_NAME-1);  /* Store department */
        clientDept[clientCount][MAX_NAME-1] = 0;
        udpKnown[clientCount] = 0;
        lastSeen[clientCount] = 0;
        clientCount++;
        int indexForThread = clientCount - 1;
        pthread_t th;
        pthread_create(&th, NULL, clientHandler, &indexForThread);
        pthread_detach(th);
        pthread_mutex_unlock(&clientsLock);

        printf("[SERVER] %s %s authenticated and TCP session started.\n", campus, dept);
        send(clientSock, "AUTH_OK", 7, 0);
    }

    return 0;
}
