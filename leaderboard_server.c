#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <time.h>
#include <signal.h>

#define PORT 8080
#define MAX_ENTRIES 100
#define BUFFER_SIZE 1024

typedef struct {
    char player_name[32];
    int score;
    time_t timestamp;
    char client_ip[16];
} leaderboard_entry;

leaderboard_entry leaderboard[MAX_ENTRIES];
int entry_count = 0;
int server_running = 1;

// Function to handle SIGINT for graceful shutdown
void handle_signal(int sig) {
    printf("\nShutting down server gracefully...\n");
    server_running = 0;
}

// Add or update score in leaderboard
void update_leaderboard(const char* name, int score, const char* client_ip) {
    // Check if player already exists
    for (int i = 0; i < entry_count; i++) {
        if (strcmp(leaderboard[i].player_name, name) == 0) {
            if (score > leaderboard[i].score) {
                leaderboard[i].score = score;
                leaderboard[i].timestamp = time(NULL);
                strcpy(leaderboard[i].client_ip, client_ip);
            }
            return;
        }
    }
    
    // Add new entry if there's space
    if (entry_count < MAX_ENTRIES) {
        strcpy(leaderboard[entry_count].player_name, name);
        leaderboard[entry_count].score = score;
        leaderboard[entry_count].timestamp = time(NULL);
        strcpy(leaderboard[entry_count].client_ip, client_ip);
        entry_count++;
    }
}

// Sort leaderboard by score (descending)
void sort_leaderboard() {
    for (int i = 0; i < entry_count - 1; i++) {
        for (int j = 0; j < entry_count - i - 1; j++) {
            if (leaderboard[j].score < leaderboard[j + 1].score) {
                leaderboard_entry temp = leaderboard[j];
                leaderboard[j] = leaderboard[j + 1];
                leaderboard[j + 1] = temp;
            }
        }
    }
}

// Format leaderboard as string for sending to client
void format_leaderboard(char* buffer, int buffer_size) {
    sort_leaderboard();
    
    char temp[256];
    snprintf(buffer, buffer_size, "LEADERBOARD");
    
    int max_entries = (entry_count > 10) ? 10 : entry_count; // Send top 10
    for (int i = 0; i < max_entries; i++) {
        snprintf(temp, sizeof(temp), "|%s:%d", 
                 leaderboard[i].player_name, leaderboard[i].score);
        strncat(buffer, temp, buffer_size - strlen(buffer) - 1);
    }
}

// Process client message
void process_client_message(int client_socket, const char* message, const char* client_ip) {
    char response[BUFFER_SIZE];
    
    if (strncmp(message, "SUBMIT|", 7) == 0) {
        // Format: SUBMIT|PlayerName|Score
        char player_name[32];
        int score;
        
        if (sscanf(message + 7, "%31[^|]|%d", player_name, &score) == 2) {
            update_leaderboard(player_name, score, client_ip);
            snprintf(response, sizeof(response), "OK|Score submitted: %s - %d", player_name, score);
            printf("Score submitted: %s - %d from %s\n", player_name, score, client_ip);
        } else {
            snprintf(response, sizeof(response), "ERROR|Invalid SUBMIT format");
        }
    }
    else if (strncmp(message, "GET_LEADERBOARD", 15) == 0) {
        format_leaderboard(response, sizeof(response));
        printf("Leaderboard requested by %s\n", client_ip);
    }
    else {
        snprintf(response, sizeof(response), "ERROR|Unknown command");
    }
    
    send(client_socket, response, strlen(response), 0);
}

int main() {
    int server_fd, client_socket;
    struct sockaddr_in address;
    int opt = 1;
    int addrlen = sizeof(address);
    
    // Setup signal handler for graceful shutdown
    signal(SIGINT, handle_signal);
    
    // Create socket file descriptor
    if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        perror("setsockopt");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    // Bind socket to port
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    // Start listening
    if (listen(server_fd, 3) < 0) {
        perror("listen");
        close(server_fd);
        exit(EXIT_FAILURE);
    }
    
    printf("Leaderboard Server started on port %d\n", PORT);
    printf("Waiting for connections...\n");
    
    // Main server loop
    while (server_running) {
        // Accept incoming connection with timeout
        fd_set readfds;
        struct timeval timeout;
        
        FD_ZERO(&readfds);
        FD_SET(server_fd, &readfds);
        
        timeout.tv_sec = 1;  // 1 second timeout
        timeout.tv_usec = 0;
        
        int activity = select(server_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (activity < 0 && server_running) {
            perror("select error");
            continue;
        }
        
        if (activity > 0 && FD_ISSET(server_fd, &readfds)) {
            // Accept new connection
            if ((client_socket = accept(server_fd, (struct sockaddr *)&address, 
                                       (socklen_t*)&addrlen)) < 0) {
                perror("accept");
                continue;
            }
            
            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &address.sin_addr, client_ip, INET_ADDRSTRLEN);
            printf("New connection from %s:%d\n", client_ip, ntohs(address.sin_port));
            
            // Handle client communication
            char buffer[BUFFER_SIZE] = {0};
            int bytes_read = read(client_socket, buffer, BUFFER_SIZE - 1);
            
            if (bytes_read > 0) {
                buffer[bytes_read] = '\0';
                printf("Received: %s\n", buffer);
                process_client_message(client_socket, buffer, client_ip);
            }
            
            close(client_socket);
        }
    }
    
    printf("Server shutdown complete.\n");
    close(server_fd);
    return 0;
}
