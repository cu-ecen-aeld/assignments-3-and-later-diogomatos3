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
// #include <pthread.h>    // For POSIX threads
// #include <time.h>       // For time functions
// #include <sys/queue.h>  // For queue functions

#define PORT 9000       // Port number to listen on
#define BACKLOG 10      // Number of pending connections in the listen queue
#define DATA_FILE "/var/tmp/aesdsocketdata" // File to store data

// Global variables
int server_socket = -1;  // Server socket file descriptor
bool running = true;     // Flag to indicate if the server is running
// pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect file access

// typedef struct {
//     int client_socket;
//     struct sockaddr_in client_addr;
// } thread_args_t; // Thread arguments structure

// typedef struct thread_entry {
//     pthread_t thread_id;
//     thread_args_t *args;
//     SLIST_ENTRY(thread_entry) entries;
// } thread_entry_t; // Thread entry structure

// SLIST_HEAD(thread_list, thread_entry) head = SLIST_HEAD_INITIALIZER(head);
// pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

// // Thread function to handle each client connection:
// void* thread_func(void* arg) {
//     thread_args_t *args = (thread_args_t*)arg;
//     handle_client(args->client_socket, &args->client_addr);
//     free(args);
//     return NULL;
// }

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

// Function to handle each client connection
void handle_client(int client_socket, struct sockaddr_in *client_addr) {
    char buffer[1024] = {0};  // Buffer to store received data (zero-initialized)
    memset(buffer, 0, sizeof(buffer));
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
    bool keep_running = true;
    ssize_t bytes_received = 0; // Number of bytes received
    while (keep_running == true) { // Receive data from client
        bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0);
        buffer[bytes_received] = '\0'; // Null-terminate the received data
        syslog(LOG_INFO, "Bytes read: %zd", bytes_received); // Log the ssize_t variable
        syslog(LOG_DEBUG, "Received data: %s", buffer); // Log received data

        if (bytes_received > 0)
        {
            // pthread_mutex_lock(&file_mutex); // Lock the mutex
            if (write(data_fd, buffer, bytes_received) == -1) { // Write data to file
                syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno)); // Log error message
                break; // Break the loop
            }
            // pthread_mutex_unlock(&file_mutex); // Unlock the mutex
        }
        

        if (strchr(buffer, '\n')) { // Check if the received data contains a newline character
            lseek(data_fd, 0, SEEK_SET); // Move file pointer to the beginning of the file
            
            memset(buffer, 0, sizeof(buffer));
            while ((bytes_received = read(data_fd, buffer, sizeof(buffer))) > 0) { // Read data from file
                
                if (send(client_socket, buffer, bytes_received, 0) == -1) { // Send data to client
                    syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno)); // Log error message
                    break; // Break the loop
                }
            }
            keep_running = false;
        }        
    }

    // Close client socket
    close(data_fd);
    close(client_socket);
    syslog(LOG_INFO, "Closed connection from %s", client_ip);
}

// Function to daemonize the process
void daemonize() {
    pid_t pid = fork(); // Fork the process

    if (pid < 0) { // Check if fork failed
        // Fork failed
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    if (pid > 0) { // Check if parent process
        // Parent process, exit
        exit(EXIT_SUCCESS);
    }

    // Child process continues
    if (setsid() < 0) { // Create a new session
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // Redirect standard file descriptors to /dev/null
    close(STDIN_FILENO); // Close standard input
    close(STDOUT_FILENO); // Close standard output
    close(STDERR_FILENO); // Close standard error
    open("/dev/null", O_RDONLY); // Open /dev/null as standard input
    open("/dev/null", O_RDWR); // Open /dev/null as standard output
    open("/dev/null", O_RDWR); // Open /dev/null as standard error
}

// // Thread function to write timestamp to file
// void* timestamp_thread_func(void* arg) {
//     while (running) {
//         sleep(10);
//         time_t now = time(NULL);
//         struct tm *tm_info = localtime(&now);
//         char timestamp[64];
//         strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

//         pthread_mutex_lock(&file_mutex);
//         int data_fd = open(DATA_FILE, O_WRONLY | O_APPEND);
//         if (data_fd != -1) {
//             write(data_fd, timestamp, strlen(timestamp));
//             close(data_fd);
//         }
//         pthread_mutex_unlock(&file_mutex);
//     }
//     return NULL;
// }

// Main function
int main(int argc, char *argv[]) {
    struct sockaddr_in server_addr, client_addr;      // Server and client address structures
    socklen_t client_addr_len = sizeof(client_addr);  // Length of client address structure
    int opt = 1;                                      // Option value for setsockopt
    bool daemon_mode = false;                         // Flag to indicate if daemon mode is enabled
    // pthread_t timestamp_thread;                       // Thread ID variable for timestamp thread

    // Parse command-line arguments
    int c;                                            // Variable to store parsed option
    while ((c = getopt(argc, argv, "d")) != -1) {     // Parse command-line options
        switch (c) {                                  // Check the parsed option
            case 'd':                                 // Daemon mode option
                daemon_mode = true;                   // Enable daemon mode
                break;                                // Break the switch statement
            default:                                  // Invalid option
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]); // Print usage message
                exit(EXIT_FAILURE);                   // Exit with failure code
        }
    }

    openlog("aesdsocket", LOG_PID, LOG_USER);         // Open system log

    // Set up signal handler using sigaction
    struct sigaction sa;                              // Signal action structure
    memset(&sa, 0, sizeof(sa));                       // Clear signal action structure
    sa.sa_handler = signal_handler;                   // Set signal handler function
    sigaction(SIGINT, &sa, NULL);                     // Register signal handler for SIGINT
    sigaction(SIGTERM, &sa, NULL);                    // Register signal handler for SIGTERM
    sigaction(SIGTSTP, &sa, NULL);                    // Register signal handler for SIGTSTP

    // SLIST_INIT(&head);                                // Initialize the thread list

    // pthread_t timestamp_thread;                       // Thread ID variable for timestamp thread
    // // Create a new thread to write timestamp to file
    // if (pthread_create(&timestamp_thread, NULL, timestamp_thread_func, NULL) != 0) {
    //     syslog(LOG_ERR, "Failed to create timestamp thread: %s", strerror(errno));
    //     cleanup();
    //     exit(EXIT_FAILURE);
    // }    

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

    // Daemonize if the -d argument is provided
    if (daemon_mode) {
        daemonize();
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

    //     thread_args_t *args = malloc(sizeof(thread_args_t)); // Allocate memory for thread arguments
    //     args->client_socket = client_socket;                 // Set client socket
    //     args->client_addr = client_addr;                     // Set client address

    //     thread_entry_t *entry = malloc(sizeof(thread_entry_t)); // Allocate memory for thread entry
    //     entry->args = args; // Set thread arguments

    //     // Create a new thread to handle the client connection
    //     if (pthread_create(&entry->thread_id, NULL, thread_func, args) != 0) { // Create thread
    //         syslog(LOG_ERR, "Failed to create thread: %s", strerror(errno));   // Log error message
    //         close(client_socket);                                              // Close client socket
    //         free(args);                                                        // Free thread arguments
    //         free(entry);                                                       // Free thread entry
    //     } else {
    //         pthread_mutex_lock(&list_mutex);                                   // Lock the list mutex
    //         SLIST_INSERT_HEAD(&head, entry, entries);                          // Insert thread entry into list
    //         pthread_mutex_unlock(&list_mutex);                                 // Unlock the list mutex
    //     }
        // Handle client request
        handle_client(client_socket, &client_addr);
    }

    // pthread_mutex_lock(&list_mutex);                                           // Lock the list mutex
    // thread_entry_t *entry;                                                     // Thread entry pointer
    // // Iterate over the list of threads
    // SLIST_FOREACH(entry, &head, entries) {
    //     pthread_join(entry->thread_id, NULL); // Join the thread
    //     free(entry->args);                    // Free thread arguments
    //     free(entry);                          // Free thread entry
    // }
    // pthread_mutex_unlock(&list_mutex);        // Unlock the list mutex

    // pthread_join(timestamp_thread, NULL);     // Join the timestamp thread
    cleanup();                                        // Clean up resources
    return 0;                                         // Return success code
}