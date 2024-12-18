#include <stdio.h>      // For standard I/O functions
#include <stdlib.h>     // For standard library functions
#include <string.h>     // For string manipulation functions
#include <errno.h>      // For error number definitions
#include <unistd.h>     // For POSIX API functions
#include <sys/types.h>  // For data types used in system calls
#include <sys/socket.h> // For socket API
#include <netinet/in.h> // For Internet address family
#include <arpa/inet.h>  // For definitions for internet operations
#include <signal.h>     // For signal handling
#include <syslog.h>     // For system logging
#include <fcntl.h>      // For file control options
#include <stdbool.h>    // For boolean data type

#define PORT 9000       // Port number to listen on
#define BACKLOG 10      // Number of pending connections in the listen queue
#define DATA_FILE "/var/tmp/aesdsocketdata" // File to store data

// Global variables
int server_socket = -1;  // Server socket file descriptor
bool running = true;     // Flag to indicate if the server is running

// Signal handler function definition
void signal_handler(int signal) { 
    printf("Caught signal %d\n", signal); // Print signal number
    running = false; // Set running flag to false
}

// Cleanup function definition
void cleanup() {
    if (server_socket != -1) { // Check if server socket is valid
        close(server_socket);  // Close server socket
        server_socket = -1;    // Reset server socket
    }
    unlink(DATA_FILE);         // Delete data file
    syslog(LOG_INFO, "Caught signal, exiting"); // Log cleanup message
    closelog();                // Close system log
}


void handle_client(int client_socket, struct sockaddr_in *client_addr) {
    char buffer[1024];  // Buffer to store received data
    int data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0644); // Open data file

    if (data_fd == -1) { // Check if data file was opened
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno)); // Log error message
        close(client_socket); // Close client socket
        return;
    }

    char client_ip[INET_ADDRSTRLEN]; // Buffer to store client IP address
    inet_ntop(AF_INET, &client_addr->sin_addr, client_ip, INET_ADDRSTRLEN); // Get client IP address
    syslog(LOG_INFO, "Accepted connection from %s", client_ip);

    // Receive data from client
    ssize_t bytes_received; // Number of bytes received
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) { // Receive data from client
        buffer[bytes_received] = '\0'; // Null-terminate the received data
        if (write(data_fd, buffer, bytes_received) == -1) { // Write data to file
            syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno)); // Log error message
            break; // Break the loop
        }

        if (strchr(buffer, '\n')) { // Check if the received data contains a newline character
            lseek(data_fd, 0, SEEK_SET); // Move file pointer to the beginning of the file
            while ((bytes_received = read(data_fd, buffer, sizeof(buffer))) > 0) { // Read data from file
                if (send(client_socket, buffer, bytes_received, 0) == -1) { // Send data to client
                    syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno)); // Log error message
                    break; // Break the loop
                }
            }
        }
    }

    // Close client socket
    close(data_fd);
    close(client_socket);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

// Main function
int main() {
    struct sockaddr_in server_addr, client_addr;      // Server and client address structures
    socklen_t client_addr_len = sizeof(client_addr);  // Length of client address structure
    int opt = 1;                                      // Option value for setsockopt

    openlog("aesdsocket", LOG_PID, LOG_USER);         // Open system log

    // Set up signal handler using sigaction
    struct sigaction sa;                              // Signal action structure
    memset(&sa, 0, sizeof(sa));                       // Clear signal action structure
    sa.sa_handler = signal_handler;                   // Set signal handler function
    sigaction(SIGINT, &sa, NULL);                     // Register signal handler for SIGINT
    sigaction(SIGTERM, &sa, NULL);                    // Register signal handler for SIGTERM
    sigaction(SIGTSTP, &sa, NULL);                    // Register signal handler for SIGTSTP

    server_socket = socket(AF_INET, SOCK_STREAM, 0);  // Create a new socket
    if (server_socket == -1) {                        // Check if socket creation was successful
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno)); // Log error message
        return -1;                                    // Return error code
    }

    // Set the SO_REUSEADDR option
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno)); // Log error message
        cleanup();                                    // Clean up resources
        return -1;                                    // Return error code
    }

    memset(&server_addr, 0, sizeof(server_addr));     // Clear server address structure
    server_addr.sin_family = AF_INET;                 // Set address family to AF_INET
    server_addr.sin_port = htons(PORT);               // Set port number, converting to network byte order
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // Allow connections from any IP address

    // Print the server address details to the terminal
    printf("Binding to address: %s, port: %d\n",
           inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) { // Bind socket to address
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno)); // Log error message
        cleanup();                                    // Clean up resources
        return -1;                                    // Return error code
    }

    // // Log success message
    // syslog(LOG_INFO, "Socket successfully bound to address: %s, port: %d",
    //        inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    if (listen(server_socket, BACKLOG) == -1) { // Listen for incoming connections
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno)); // Log error message
        cleanup();                                     // Clean up resources
        return -1;                                     // Return error code
    }

    while (running)                                    // Loop to accept incoming connections
    {
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len); // Accept connection
        if (client_socket == -1) {                     // Check if connection was accepted
            if (errno == EINTR) {
                break;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            continue;                                 // Return error code
        }

        // Log success message
        syslog(LOG_INFO, "Accepted connection from: %s, port: %d",
               inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

        // Handle client request
        handle_client(client_socket, &client_addr);
    }

    cleanup();
    return 0;                                         // Return success code
}