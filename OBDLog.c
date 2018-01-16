// Fergus Kelley
// CSC 4410 Embedded Systems

// Car Telemetry Project
// OBDLog.c
// This is a command line utility for communicating with a ELM327 compatible OBD-II UART

// All code by Fergus Kelley, except where otherwise stated

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h> 
#include <errno.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include <time.h>
#include <stdint.h>

FILE* lp; // File pointer for output, set to stdout or a log file given by argument
FILE* sp; // File pointer for semaphore lock file
int port; // File descriptor that will point to the serial port

// open_port() based on code from https://www.cmrr.umn.edu/~strupp/serial.html
// Modified by Fergus Kelley
// Returns the file descriptor on success, or exits on failure
int open_port(char port[]) {

    // fprintf(stdout, "# Attempting to open port: %s\n", port);

    int fd; // File descriptor for the port
    fd = open( port, O_RDWR | O_NOCTTY | O_NDELAY);

    if (fd < 0) {
        // Could not open the port
        fputs("# Unable to open port, exiting...\n", stderr);
        exit(EXIT_FAILURE);
    }

    // Block until characters are availible to read
    fcntl(fd, F_SETFL, 0);

    // Set up the termios settings for the port
    struct termios options;

    // Get current port settings
    tcgetattr(fd, &options); 

    // Set the baud rate
    cfsetispeed(&options, B9600); 
    cfsetospeed(&options, B9600);

    // Enable the receiver and set local mode
    options.c_cflag |= (CLOCAL | CREAD);

    // Set the port to 8 data bits, no parity bit, and 1 stop bit
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;

    // Choose unprocessed raw input (non-canonical input mode)
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);

    // Choose raw output
    options.c_oflag &= ~OPOST;

    // Flush the input and output buffers and apply the new settings
    tcsetattr(fd, TCSAFLUSH, &options);

    return (fd);
}

// Get the time in milliseconds
uint64_t getTime() {
    struct timespec theTime;
    clock_gettime(CLOCK_REALTIME, &theTime);
    return ( theTime.tv_sec * 1000 ) + ( theTime.tv_nsec / 1000000 );
}

// Cleanup files and output a nice message on exit
void doExit(void) {
    fprintf(lp, "\n# ...Exiting\n");
    close(port);
    fclose(lp);
    int rm = remove("semaphorelock");
    if (rm == -1) {
        fputs("# Failed to remove semaphore lock file on exit\n", stderr);
    }
}

// Handle SIGINT (ctrl-c)
void doSigInt(int n) {
    fprintf(lp, "\n# SIGINT Recieved");
    exit(EXIT_SUCCESS);
}

// Handle SIGTERM (signal sent to kill this process)
void doSigTerm(int n) {
    fprintf(lp, "\n# SIGTERM Recieved\n# ...Exiting\n");
    close(port);
    fclose(lp);
    raise(SIGKILL);
}

// Send a command to the port
// Returns the number of bytes sent, or exits on failure
int send(char * theCommand) {

    char commandToSend[strlen(theCommand)+2];
    commandToSend[0] = 0;
    strcat(commandToSend, theCommand);
    strcat(commandToSend, "\r");

    int n = write(port, commandToSend, strlen(commandToSend));
    if (n < 0) {
        // Couldn't write any chars to the port
        fputs("# Write failed, exiting...\n", stderr);
        exit(EXIT_FAILURE);
    }

    return n;
}

// Recieve a response from the port
// Reads data from the port character by character until the > character is recieved
// The response is placed into the passed buffer
// Returns the number of chars in the response, or exits on failure
int recieve(char * buffer, int bufferSize) {

    char nextChar[1];
    int bp = 0; // Location in the buffer
    int m;
    while (true) {
        m = read(port, nextChar, 1);
        if (m < 0) {
            // Couldn't read any chars from the port
            fputs("# Read failed, exiting...\n", stderr);
            exit(EXIT_FAILURE);
        }

        if (nextChar[0] == '\r') {
            // We got a carriage return, ignore it if repeated
            if (buffer[bp-1] == ',') {
                continue;
            }
            nextChar[0] = ',';
        } else if (nextChar[0] == '>') {
            // The OBD-II UART is done sending data and is waiting for a command
            break;
        }

        buffer[bp] = nextChar[0];
        bp += m;

        if (bp > bufferSize) {
            fputs("# Response exceeded buffer size, exiting...\n", stderr);
            exit(EXIT_FAILURE);
        }
    }

    // bp++;
    buffer[bp] = 0; // Null terminate the string read from the port so we can print it

    return bp;
}

int main (int argc, char **argv) {

    // Set default logging file to stdout
    lp = stdout;

    // Make sure that only one process of this is running
    // Check for existance of a semaphore lock file
    // If it does exist, send an SIGTERM interrupt to kill the previous process
    if( access( "semaphorelock", F_OK ) != -1 ) {
        // File exists, read the previous process's PID from the semaphore file
        sp = fopen( "semaphorelock", "r");
        int PID = 0;
        fscanf(sp, "%d", &PID);
        kill(PID, SIGTERM);
        fclose(sp);
    }

    // Create a new lock file and place this PID in it
    sp = fopen( "semaphorelock", "w");
    fprintf(sp, "%d", (int) getpid());
    if (sp == 0) {
        // Couldn't create the lock file
        fputs("# Failed to create semaphore lock file, exiting...\n", stderr);
        exit(EXIT_FAILURE);
    }
    fclose(sp);

    // Initialize default options
    // TODO Figure out best way of handling long filenames. Right now anything over 1024 bytes will crash.
    //      probably need to use malloc()
    char portName[1024] = "/dev/ttyUSB0"; // The port to connect to
    char logFile[1024] = ""; // Destination file for logging
    int useLog = false;
    char command[128] = "010C"; // Command to issue to the OBD-II UART
    int interval = 1000; // Number of milliseconds to wait between each command
    int duration = 60; // Number of seconds to continuously issue commands
    int setupUART = false;

    // Parse command line options
    int c;
    char *end;
    extern char *optarg;
    extern int optind, optopt, opterr;
    opterr = 0;
    while ((c = getopt(argc, argv, ":hsp:l:c:i:d:")) != -1) {
        switch(c) {
            case 'h':
                // TODO Output help information
                break;
            case 's':
                setupUART = true;
                break;
            case 'p':
                strcpy(portName, optarg); // TODO replace strcpy() with a safer function
                break;
            case 'l':
                strcpy(logFile, optarg);
                useLog = true;
                break;
            case 'c':
                strcpy(command, optarg);
                break;
            case 'i':
                interval = strtol(optarg, &end, 10);
                // Check that interval is within a sane range, so we don't accidently DoS the car
                if ( interval < 200 ) {
                    interval = 200;
                }
                break;
            case 'd':
                // duration = 0 means never stop until an interrupt is recieved 
                duration = strtol(optarg, &end, 10);
                break;
            case ':':
                printf("# -%c requires an option argument, exiting...\n", optopt);
                exit(EXIT_FAILURE);
                break;
            case '?':
                printf("# Unknown option %c, exiting...\n", optopt);
                exit(EXIT_FAILURE);
                break;
        }
    }

    // If the option to use a file for logging has been set,
    // make that the output file instead of stdout
    if ( useLog ) {
        lp = fopen(logFile, "a+");
        if (lp == 0) {
            // Couldn't open the log file
            fputs("# Failed to open log file, exiting...\n", stderr);
            exit(EXIT_FAILURE);
        }
    }

    // Register the exit functions to gracefully close everything
    atexit(doExit);
    signal(SIGINT, doSigInt);
    signal(SIGTERM, doSigTerm);

    // Log the provided options
    fprintf(lp, "# Port: %s\n", portName);
    fprintf(lp, "# Command: %s\n", command);
    fprintf(lp, "# Interval: %d ms\n", interval);
    fprintf(lp, "# Duration: %d s\n", duration);
    fflush(lp);

    // Open and configure the serial port
    port = open_port(portName);

    // Create a buffer to hold the response from each command
    char responseBuffer[2048];

    // Initalize UART if the setup parameter is set
    // TODO Add error checking to verify that the UART can communicate with the vehicle
    if ( setupUART ) {
        send( "ATZ" ); // Restart the UART
        recieve( responseBuffer, sizeof(responseBuffer) );
        fprintf(lp, "# ATZ Response: %s\n", responseBuffer);
        fflush(lp);

        send( "ATD" ); // Reset the settings to factory defaults
        recieve( responseBuffer, sizeof(responseBuffer) );
        fprintf(lp, "# ATD Response: %s\n", responseBuffer);
        fflush(lp);

        send( "ATH1" ); // Enable headers, so we can see which ECUs are sending responses
        recieve( responseBuffer, sizeof(responseBuffer) );
        fprintf(lp, "# ATH1 Response: %s\n", responseBuffer);
        fflush(lp);

        send( "ATS0" ); // Disable spaces
        recieve( responseBuffer, sizeof(responseBuffer) );
        fprintf(lp, "# ATS0 Response: %s\n", responseBuffer);
        fflush(lp);
    }

    // Setup the variables we need for timing.
    uint64_t currentTime = getTime();
    uint64_t finishTime = currentTime + ( duration * 1000 );
    uint64_t intervalTime = 0;

    // Start polling: send a command, read response, continue until duration
    while (true) {

        // Get the current time
        currentTime = getTime();

        // Check if the required duration has been met
        if ( (duration > 0) && (currentTime > finishTime) ) {
            // We've completed the duration required, so we can stop polling
            fprintf(lp, "\n# Duration completed\n");
            break;
        }

        // Check if the required interval has passed to send another command
        if ( (currentTime - intervalTime) < interval ) {
            // Not enough time has passed, wait some more
            continue;
        }

        // Enough time has passed to send another command, so reset the time
        intervalTime = currentTime;

        // Write the current time to the log, making a new data point
        fprintf(lp, "\n%llu,", (long long unsigned int) intervalTime);

        // Send the command to the serial port
        send( command );

        // Read the response and write it to the log
        recieve(responseBuffer, sizeof(responseBuffer));
        fprintf(lp, "%s", responseBuffer);
        fflush(lp);

    }

    exit(EXIT_SUCCESS);
}
