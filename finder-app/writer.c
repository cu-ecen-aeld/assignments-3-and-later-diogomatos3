#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>

#define MAX_FILE_PATH 256
#define MAX_STRING_LENGTH 1024

// Function to write the string to the file
void write_to_file(const char *file, const char *string) {
    FILE *fp = fopen(file, "w");
    
    if (fp == NULL) {
        // Log error if unable to open the file
        syslog(LOG_ERR, "Error opening file: %s", file);
        exit(EXIT_FAILURE);
    }
    
    if (fputs(string, fp) == EOF) {
        // Log error if writing fails
        syslog(LOG_ERR, "Error writing to file: %s", file);
        fclose(fp);
        exit(EXIT_FAILURE);
    }

    // Log the successful writing operation
    syslog(LOG_DEBUG, "Writing \"%s\" to %s", string, file);
    
    fclose(fp);
}

int main(int argc, char *argv[]) {
    // Open syslog with the LOG_USER facility
    openlog("writer", LOG_PID | LOG_CONS, LOG_USER);

    // Check if the correct number of arguments is passed
    if (argc != 3) {
        syslog(LOG_ERR, "Usage: %s <file> <string>", argv[0]);
        fprintf(stderr, "Usage: %s <file> <string>\n", argv[0]);
        closelog();
        return EXIT_FAILURE;
    }

    const char *file = argv[1];   // File path
    const char *string = argv[2]; // String to write

    // Check for string length constraint (optional but good practice)
    if (strlen(string) >= MAX_STRING_LENGTH) {
        syslog(LOG_ERR, "Error: String length exceeds maximum allowed length.");
        fprintf(stderr, "Error: String length exceeds maximum allowed length.\n");
        closelog();
        return EXIT_FAILURE;
    }

    // Call the function to write to the file
    write_to_file(file, string);

    // Close syslog
    closelog();

    return EXIT_SUCCESS;
}
