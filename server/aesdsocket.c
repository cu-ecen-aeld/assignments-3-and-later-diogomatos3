#include <signal.h>     // For signal handling
#include <stdio.h>      // For standard I/O functions
#include <stdlib.h>     // For standard library functions
#include <string.h>     // For string manipulation functions
#include <errno.h>      // For error number definitions
#include <unistd.h>     // For POSIX API functions
#include <sys/types.h>  // For data types used in system calls
#include <sys/socket.h> // For socket API
#include <netinet/in.h> // For Internet address family
#include <arpa/inet.h>  // For definitions for internet operations
#include <syslog.h>     // For system logging
#include <fcntl.h>      // For file control options
#include <stdbool.h>    // For boolean data type
#include <pthread.h>    // For POSIX threads
#include <time.h>       // For time functions
#include <sys/queue.h>  // For queue functions
#include <sys/time.h>   // For struct timeval
#include <sys/ioctl.h> // For ioctl
#include "../aesd-char-driver/aesd_ioctl.h" // For AESDCHAR_IOCSEEKTO

#define PORT 9000       // Port number to listen on
#define BACKLOG 10      // Number of pending connections in the listen queue
#ifndef USE_AESD_CHAR_DEVICE
#define USE_AESD_CHAR_DEVICE 1
#endif

#if USE_AESD_CHAR_DEVICE
#define DATA_FILE "/dev/aesdchar"
#else
#define DATA_FILE "/var/tmp/aesdsocketdata" // File to store data
#endif
#define TIMESTAMP_INTERVAL 10
#define BUFFER_SIZE 1024

// Global variables
volatile sig_atomic_t running_signal = 1; // Used only in signal handler
bool running = true; // Used in threads, protected by mutex
pthread_mutex_t running_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER; // Mutex to protect file access
#include <search.h> // For hsearch, hcreate, hdestroy

pthread_mutex_t list_mutex = PTHREAD_MUTEX_INITIALIZER;

// Thread entry structure for managing active threads
struct thread_entry {
    pthread_t thread_id;
    SLIST_ENTRY(thread_entry) entries;
};
SLIST_HEAD(thread_list, thread_entry) head = SLIST_HEAD_INITIALIZER(head);

// Signal handler to set shutdown flag for graceful termination
void signal_handler(int signal) {
    running_signal = 0;
}

// Thread function: handles a single client connection
void *client_handler(void *arg) {
    // Unpack client info from argument
    struct {
        int client_socket;
        struct sockaddr_in client_addr;
        socklen_t client_addr_len;
    } *client_info = arg;
    int client_socket = client_info->client_socket;
    struct sockaddr_in client_addr = client_info->client_addr;
    free(client_info);

    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    syslog(LOG_INFO, "Accepted connection from: %s, port: %d", client_ip, ntohs(client_addr.sin_port));
    int data_fd;

    // Main receive loop for this client
    // Keep the file descriptor open for the entire session
    data_fd = open(DATA_FILE, O_RDWR | O_APPEND
#if !USE_AESD_CHAR_DEVICE
        | O_CREAT
#endif
        , 0644);
    if (data_fd == -1) {
        syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
        return NULL;
    }

    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer), 0)) > 0) {
        syslog(LOG_INFO, "Received %zd bytes of data", bytes_received);
        // Lock file access to ensure thread safety
        pthread_mutex_lock(&file_mutex);

        // Check if this is a seek command
        if (bytes_received > 16 && strncmp(buffer, "AESDCHAR_IOCSEEKTO:", 19) == 0) {
            // Parse X,Y values
            unsigned int cmd_num, cmd_offset;
            if (sscanf(buffer + 19, "%u,%u", &cmd_num, &cmd_offset) == 2) {
                struct aesd_seekto seekto;
                seekto.write_cmd = cmd_num;
                seekto.write_cmd_offset = cmd_offset;
                
                // Perform the ioctl
                if (ioctl(data_fd, AESDCHAR_IOCSEEKTO, &seekto) != 0) {
                    syslog(LOG_ERR, "ioctl AESDCHAR_IOCSEEKTO failed: %s", strerror(errno));
                } else {
                    syslog(LOG_INFO, "Successfully performed seek to command %u offset %u", 
                           cmd_num, cmd_offset);
                }
                
                // Read and send back the content from the current position (already set by IOCTL)
                ssize_t bytes_read;
                while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
                    ssize_t bytes_sent = send(client_socket, buffer, bytes_read, 0);
                    if (bytes_sent < 0) {
                        syslog(LOG_ERR, "Failed to send data: %s", strerror(errno));
                        break;
                    }
                }
                pthread_mutex_unlock(&file_mutex);
                continue; // Skip the normal write handling
            }
            // If sscanf failed, treat as normal input
        }
        if (data_fd == -1) {
            syslog(LOG_ERR, "Failed to open data file: %s", strerror(errno));
            pthread_mutex_unlock(&file_mutex);
            break;
        }
        // Write received data to file/device
        if (write(data_fd, buffer, bytes_received) == -1) {
            syslog(LOG_ERR, "Failed to write to data file: %s", strerror(errno));
        }
        // If a newline is found, echo file/device contents back to client
        if (memchr(buffer, '\n', bytes_received)) {
            syslog(LOG_INFO, "CR char was found...");
            lseek(data_fd, 0, SEEK_SET);
            ssize_t bytes_read;
            ssize_t total_sent = 0;
            // Read file/device and send to client in chunks
            while ((bytes_read = read(data_fd, buffer, sizeof(buffer))) > 0) {
                syslog(LOG_INFO, "Data read from the file...");
                ssize_t bytes_sent = send(client_socket, buffer, bytes_read, 0);
                if (bytes_sent < 0) {
                    syslog(LOG_ERR, "Failed to send data to client: %s", strerror(errno));
                    break;
                } else {
                    syslog(LOG_INFO, "Message sent to the client...");
                    syslog(LOG_INFO, "Sent %zd bytes", bytes_sent);
                    total_sent += bytes_sent;
                }
            }
            if (total_sent > 0) {
                syslog(LOG_INFO, "Total sent to client: %zd bytes", total_sent);
            }
        }
        close(data_fd);
        pthread_mutex_unlock(&file_mutex);
    }
    close(client_socket);
    syslog(LOG_INFO, "Closed connection from: %s", client_ip);

    // Remove this thread from the active thread list
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

// Daemonize the process: fork, setsid, chdir, redirect stdio
void daemonize(void) {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Fork failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (chdir("/") < 0) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    int fd_null = open("/dev/null", O_RDWR);
    if (fd_null == -1) {
        syslog(LOG_ERR, "Failed to open /dev/null: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    dup2(fd_null, STDIN_FILENO);
    dup2(fd_null, STDOUT_FILENO);
    dup2(fd_null, STDERR_FILENO);
    if (fd_null > STDERR_FILENO) {
        close(fd_null);
    }
}

#if !USE_AESD_CHAR_DEVICE
// Thread function: periodically writes timestamp to file
void* timestamp_thread_func(void* arg) {
    syslog(LOG_INFO, "Starting timestamp_thread_func...");
    while (1) {
        pthread_mutex_lock(&running_mutex);
        bool local_running = running;
        pthread_mutex_unlock(&running_mutex);
        if (!local_running) break;
        sleep(TIMESTAMP_INTERVAL);
        time_t now = time(NULL);
        struct tm *tm_info = localtime(&now);
        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "timestamp:%a, %d %b %Y %H:%M:%S %z\n", tm_info);
        // Lock file for safe timestamp write
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
#endif

int main(int argc, char *argv[]) {
    bool daemon_mode = false;
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Parse command-line arguments for daemon mode
    int c;
    while ((c = getopt(argc, argv, "d")) != -1) {
        switch (c) {
            case 'd':
                daemon_mode = true;
                break;
            default:
                fprintf(stderr, "Usage: %s [-d]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    if (daemon_mode) {
        syslog(LOG_INFO, "Starting daemon mode...");
        daemonize();
    }

    // Set up signal handlers for clean shutdown
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

#if !USE_AESD_CHAR_DEVICE
    // Start timestamp thread if not using char device
    syslog(LOG_INFO, "Creating timestamp thread...");
    pthread_t timestamp_tid;
    if (pthread_create(&timestamp_tid, NULL, timestamp_thread_func, NULL) != 0) {
        syslog(LOG_ERR, "Failed to create timestamp thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Timestamp thread created successfully.");
#endif

    // Create server socket and set options
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        syslog(LOG_ERR, "Failed to create socket: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "Failed to set SO_REUSEADDR: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    struct sockaddr_in server_addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(PORT),
    };
    syslog(LOG_INFO, "Binding to address: %s, port: %d",
           inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Failed to bind socket: %s", strerror(errno));
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Socket successfully bound to address: %s, port: %d",
           inet_ntoa(server_addr.sin_addr), ntohs(server_addr.sin_port));
    if (listen(server_socket, BACKLOG) == -1) {
        syslog(LOG_ERR, "Failed to listen on socket: %s", strerror(errno));
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    syslog(LOG_INFO, "Listening for connections...");
    // Main server loop: accept and handle client connections
    while (1) {
        pthread_mutex_lock(&running_mutex);
        bool local_running = running;
        pthread_mutex_unlock(&running_mutex);
        if (!local_running) break;

        // Allocate and initialize client info struct for new connection
        struct {
            int client_socket;
            struct sockaddr_in client_addr;
            socklen_t client_addr_len;
        } *client_info = malloc(sizeof(*client_info));
        if (!client_info) {
            syslog(LOG_ERR, "Memory allocation failed");
            continue;
        }
        client_info->client_addr_len = sizeof(client_info->client_addr);
        client_info->client_socket = accept(server_socket, (struct sockaddr *)&client_info->client_addr, &client_info->client_addr_len);
        if (client_info->client_socket < 0) {
            if (errno == EINTR) {
                free(client_info);
                break;
            }
            syslog(LOG_ERR, "Failed to accept connection: %s", strerror(errno));
            free(client_info);
            continue;
        }

        // Create a thread to handle the new client
        struct thread_entry *entry = malloc(sizeof(struct thread_entry));
        if (!entry) {
            syslog(LOG_ERR, "Memory allocation failed");
            close(client_info->client_socket);
            free(client_info);
            continue;
        }
        if (pthread_create(&entry->thread_id, NULL, (void *(*)(void *))client_handler, client_info) != 0) {
            syslog(LOG_ERR, "Thread creation failed: %s", strerror(errno));
            close(client_info->client_socket);
            free(client_info);
            free(entry);
            continue;
        }
        // Add thread to the active thread list
        pthread_mutex_lock(&list_mutex);
        SLIST_INSERT_HEAD(&head, entry, entries);
        pthread_mutex_unlock(&list_mutex);
    }
    close(server_socket);

    // Wait for all client threads to finish and clean up
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
    return 0;
}