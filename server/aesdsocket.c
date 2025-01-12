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
#include <pthread.h>    // For POSIX threads
#include <time.h>       // For time functions
#include <sys/queue.h>  // For queue functions

#define PORT 9000       // Port number to listen on
#define BACKLOG 10      // Number of pending connections in the listen queue
#define DATA_FILE "/var/tmp/aesdsocketdata" // File to store data
#define TIMESTAMP_INTERVAL 10
#define BUFFER_SIZE 1024

// Global variables
volatile sig_atomic_t running = 1; // Flag to indicate if the server is running

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect file access
pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef struct thread_entry {
    pthread_t thread_id;
    SLIST_ENTRY(thread_entry) entries;
} thread_entry_t; // Thread entry structure


SLIST_HEAD(thread_list, thread_entry) head = SLIST_HEAD_INITIALIZER(head);

// Signal handler function definition
void signal_handler(int signal) { 
    printf("Caught signal %d\n", signal); // Print signal number
    running = 0; // Set running flag to 0
}

// Handle client connection
void *client_handler(void *arg) {
    int client_socket = *(int *)arg;
    free(arg);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    if (getpeername(client_socket, (struct sockaddr *)&client_addr, &client_len) == -1) {
        syslog(LOG_ERR, "Failed to get client address: %s", strerror(errno));
        close(client_socket);
        return NULL;
    }

    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    
    syslog(LOG_INFO, "Accepted connection from: %s, port: %d", client_ip, ntohs(client_addr.sin_port));

    pthread_mutex_lock(&file_mutex); // Lock the mutex
    int data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0644); // Open data file
    if (data_fd == -1) { // Check if data file was opened
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno)); // Log error message
        close(client_socket); // Close client socket
        return NULL; // Return instead of exiting
    }

    // Receive data from client
    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0'; // Null-terminate the received data
        syslog(LOG_INFO, "Received data: %s", buffer); // Log received data

        if (write(data_fd, buffer, bytes_received) == -1) { // Write data to file
            syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno)); // Log error message
        }

        if (strchr(buffer, '\n')) { // Check if the received data contains a newline character
            syslog(LOG_INFO, "CR char was found...");
            lseek(data_fd, 0, SEEK_SET); // Move file pointer to the beginning of the file
            
            memset(buffer, 0, sizeof(buffer));
            while ((bytes_received = read(data_fd, buffer, sizeof(buffer))) > 0) { // Read data from file
                syslog(LOG_INFO, "Data read from the file...");
                syslog(LOG_INFO, "Read data: %s", buffer); // Log received data
                ssize_t bytes_sent = send(client_socket, buffer, bytes_received, 0);
                if (bytes_sent < 0) { // Send data to client
                }
                else
                {
                    syslog(LOG_INFO, "Message sent to the client...");
                    syslog(LOG_INFO, "Sent %zd bytes", bytes_sent);
                }             
            }
        }
    }        

    syslog(LOG_INFO, "Clean-up...");

    // Close data file
    close(data_fd);
    pthread_mutex_unlock(&file_mutex); // Unlock the mutex
    // Close client socket
    close(client_socket);

    syslog(LOG_INFO, "Closed connection from: %s", client_ip); // Log client disconnection

    pthread_mutex_lock(&list_mutex);
    struct thread_entry *entry;
    SLIST_FOREACH(entry, &head, entries) {
        if (pthread_equal(entry->thread_id, pthread_self())) {
            SLIST_REMOVE(&head, entry, thread_entry, entries);
            free(entry);
            break;
        }
    }
    pthread_mutex_unlock(&list_mutex);

    return NULL;
}

// Daemonize the process
void daemonize(void) {
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

    // Change working directory to root ("/") to avoid blocking filesystem unmounts
    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }

    // Redirect standard file descriptors to /dev/null
    close(STDIN_FILENO); // Close standard input
    close(STDOUT_FILENO); // Close standard output
    close(STDERR_FILENO); // Close standard error
    open("/dev/null", O_RDWR); // Open /dev/null to use as a file descriptor for stdin, stdout, and stderr
    dup2(STDIN_FILENO, STDOUT_FILENO); // Redirect stdout to /dev/null
    dup2(STDIN_FILENO, STDERR_FILENO); // Redirect stderr to /dev/null
}

// Thread function to write timestamp to file
void* timestamp_thread_func(void* arg) {
    syslog(LOG_INFO, "Starting timestamp_thread_func...");

    while (running) {
        sleep(TIMESTAMP_INTERVAL);
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);

        pthread_mutex_lock(&file_mutex);
        int data_fd = open(DATA_FILE, O_CREAT | O_RDWR | O_APPEND, 0644);
        if (data_fd != -1) {
            write(data_fd, timestamp, strlen(timestamp));
            close(data_fd);
        }
        pthread_mutex_unlock(&file_mutex);
    }
    return NULL;
}

// Main function
int main(int argc, char *argv[]) {
    bool daemon_mode = false;                         // Flag to indicate if daemon mode is enabled

    openlog("aesdsocket", LOG_PID, LOG_USER);         // Open system log

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

    // Daemonize if the -d argument is provided
    if (daemon_mode) {
        syslog(LOG_INFO, "Starting daemon mode...");
        daemonize();
    }

    // Set up signal handler using sigaction
    struct sigaction sa;                              // Signal action structure
    memset(&sa, 0, sizeof(sa));                       // Clear signal action structure
    sa.sa_handler = signal_handler;                   // Set signal handler function
    sigaction(SIGINT, &sa, NULL);                     // Register signal handler for SIGINT
    sigaction(SIGTERM, &sa, NULL);                    // Register signal handler for SIGTERM

    // Create a new thread to write timestamp to file
    syslog(LOG_INFO, "Creating timestamp thread...");
    pthread_t timestamp_tid;
    if (pthread_create(&timestamp_tid, NULL, timestamp_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Timestamp thread created successfully.");    

    int server_socket = socket(AF_INET, SOCK_STREAM, 0);  // Create a new socket
    if (server_socket < 0) {                        // Check if socket creation was successful
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno)); // Log error message
        exit(EXIT_FAILURE);
    }

    // Set the SO_REUSEADDR option
    int opt = 1; // Option value for setsockopt
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno)); // Log error message
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in server_addr = {
        .sin_family = AF_INET, // Set address family to AF_INET
        .sin_addr.s_addr = htonl(INADDR_ANY), // Allow connections from any IP address
        .sin_port = htons(PORT), // Set port number, converting to network byte order
    };

    // Print the server address details to the terminal
    printf("Binding to address: %s, port: %d\n",
           inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) { // Bind socket to address
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno)); // Log error message
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Log success message
    syslog(LOG_INFO, "Socket successfully bound to address: %s, port: %d",
           inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));

    if (listen(server_socket, BACKLOG) == -1) { // Listen for incoming connections
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno)); // Log error message
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    while (running)                                    // Loop to accept incoming connections
    {
        int *client_socket = malloc(sizeof(int));
        if (!client_socket) {
            syslog(LOG_ERR, "Memory allocation failed");
            continue;
        }

        *client_socket = accept(server_socket, NULL, NULL); // Accept connection
        if (client_socket < 0) {                     // Check if connection was accepted
            if (errno == EINTR) {
                break;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            free(client_socket);
            continue;                                 // Return error code
        }

        struct thread_entry *entry = malloc(sizeof(struct thread_entry));
        if (!entry) {
            syslog(LOG_ERR, "Memory allocation failed");
            close(*client_socket);
            free(client_socket);
            continue;
        }

        if (pthread_create(&entry->thread_id, NULL, client_handler, client_socket) != 0) {
            syslog(LOG_ERR, "Thread creation failed: %s", strerror(errno));
            close(*client_socket);
            free(client_socket);
            free(entry);
            continue;
        }

        pthread_mutex_lock(&list_mutex);
        SLIST_INSERT_HEAD(&head, entry, entries);
        pthread_mutex_unlock(&list_mutex);
    }

    close(server_socket);
    pthread_join(timestamp_tid, NULL);

    struct thread_entry *entry;
    while (!SLIST_EMPTY(&head)) {
        entry = SLIST_FIRST(&head);
        pthread_join(entry->thread_id, NULL);
        SLIST_REMOVE_HEAD(&head, entries);
        free(entry);
    }

    pthread_mutex_destroy(&list_mutex);
    pthread_mutex_destroy(&file_mutex);
    closelog();

    // Delete the file
    if (unlink(DATA_FILE) == 0) {
        printf("File %s deleted successfully.\n", DATA_FILE);
    } else {
        perror("Error deleting file");
    }

    return 0;                                       // Return success code
}