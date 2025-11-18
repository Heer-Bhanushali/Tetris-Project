#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include "tetris_network.h"

#define BUFFER_SIZE 1024

leaderboard_entry top_scores[10];
int score_count = 0;
int last_leaderboard_update = 0;

int connect_to_server() {
    int sock = 0;
    struct sockaddr_in serv_addr;
    
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        return -1;
    }
    
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(SERVER_PORT);
    
    if (inet_pton(AF_INET, SERVER_IP, &serv_addr.sin_addr) <= 0) {
        close(sock);
        return -1;
    }
    
    struct timeval timeout;
    timeout.tv_sec = 3;
    timeout.tv_usec = 0;
    
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        close(sock);
        return -1;
    }
    
    return sock;
}

int submit_score(const char* player_name, int score) {
    int sock = connect_to_server();
    if (sock < 0) {
        return -1;
    }
    
    char message[BUFFER_SIZE];
    char response[BUFFER_SIZE];
    
    snprintf(message, sizeof(message), "SUBMIT|%s|%d", player_name, score);
    
    if (send(sock, message, strlen(message), 0) < 0) {
        close(sock);
        return -1;
    }
    
    int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
    }
    
    close(sock);
    return 0;
}

int fetch_leaderboard() {
    int sock = connect_to_server();
    if (sock < 0) {
        return -1;
    }
    
    char message[] = "GET_LEADERBOARD";
    char response[BUFFER_SIZE];
    
    if (send(sock, message, strlen(message), 0) < 0) {
        close(sock);
        return -1;
    }
    
    int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
    if (bytes_received > 0) {
        response[bytes_received] = '\0';
        parse_leaderboard_response(response);
    } else {
        close(sock);
        return -1;
    }
    
    close(sock);
    return 0;
}

void parse_leaderboard_response(const char* response) {
    if (strncmp(response, "LEADERBOARD", 11) != 0) {
        return;
    }
    
    score_count = 0;
    const char* ptr = response + 11;
    
    while (*ptr == '|' && score_count < 10) {
        ptr++;
        
        char name[32];
        int score;
        int scanned = sscanf(ptr, "%31[^:]:%d", name, &score);
        
        if (scanned == 2) {
            strcpy(top_scores[score_count].name, name);
            top_scores[score_count].score = score;
            score_count++;
            
            while (*ptr && *ptr != '|') ptr++;
        } else {
            break;
        }
    }
}

int update_leaderboard_nonblocking() {
    fd_set readfds;
    struct timeval timeout;
    int sock = connect_to_server();
    
    if (sock < 0) return -1;
    
    char message[] = "GET_LEADERBOARD";
    send(sock, message, strlen(message), 0);
    
    FD_ZERO(&readfds);
    FD_SET(sock, &readfds);
    
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;
    
    int activity = select(sock + 1, &readfds, NULL, NULL, &timeout);
    
    if (activity > 0) {
        char response[BUFFER_SIZE];
        int bytes_received = recv(sock, response, BUFFER_SIZE - 1, 0);
        if (bytes_received > 0) {
            response[bytes_received] = '\0';
            parse_leaderboard_response(response);
            close(sock);
            return 0;
        }
    }
    
    close(sock);
    return -1;
}
