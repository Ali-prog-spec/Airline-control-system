#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <signal.h>

typedef enum { COMMERCIAL, CARGO, EMERGENCY, VIP } FlightType;

typedef struct {
    int id;
    int status;
    int amount;
    FlightType airlinetype;
    int airlineId;
    char airlineName[30];
} TicketData;

typedef struct {
    TicketData data[200];
    int count;
} SharedTicketInfo;

const char* avn_fifo = "AVNtoSP";
const char* atc_to_sp_fifo = "ATCtoSP";
const char* sp_to_atc_fifo = "SPtoATC";

void cleanup_fifos(int signo) {
    unlink(avn_fifo);
    unlink(atc_to_sp_fifo);
    unlink(sp_to_atc_fifo);
    printf("\nFIFOs cleaned up. Exiting.\n");
    exit(0);
}

typedef struct {
TicketData ticket[200];
int count;
} TotalData;

int main() {
    /*signal(SIGINT, cleanup_fifos);

    // Remove old FIFOs if they exist
    unlink(avn_fifo);
    unlink(atc_to_sp_fifo);
    unlink(sp_to_atc_fifo);

    // Create FIFOs
    if (mkfifo(avn_fifo, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create AVNtoSP FIFO");
        exit(1);
    }
    if (mkfifo(atc_to_sp_fifo, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create ATCtoSP FIFO");
        exit(1);
    }
    if (mkfifo(sp_to_atc_fifo, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create SPtoATC FIFO");
        exit(1);
    }

    int pipe_data[2];
    if (pipe(pipe_data) < 0) {
        perror("Failed to create pipe");
        exit(1);
    }

    printf("Stripe Payment System Initialized\n");

    pid_t f = fork();
    if (f < 0) {
        perror("Fork failed");
        exit(1);
    }

    if (f == 0) { // Child process
        close(pipe_data[1]); // Close write end

        SharedTicketInfo shared;
        memset(&shared, 0, sizeof(shared));

        // Make pipe non-blocking
        fcntl(pipe_data[0], F_SETFL, O_NONBLOCK);

        int fd_airline = open(atc_to_sp_fifo, O_RDONLY);
        if (fd_airline < 0) {
            perror("Failed to open ATCtoSP FIFO");
            exit(1);
        }

        while (1) {
            // Process all available tickets
            int tickets_processed = 0;
            while (1) {
                TicketData newTicket;
                ssize_t bytes_read = read(pipe_data[0], &newTicket, sizeof(TicketData));
                if (bytes_read == sizeof(TicketData)) {
                    if (shared.count < 200) {
                        shared.data[shared.count++] = newTicket;
                        tickets_processed++;
                        printf("Received ticket for %s\n", newTicket.airlineName);
                    }
                } else {
                    break;
                }
            }

            // Handle requests
            char airlineName[30] = {0};
            ssize_t bytes_read = read(fd_airline, airlineName, sizeof(airlineName)-1);
            if (bytes_read > 0) {
                airlineName[bytes_read] = '\0';
                
                if (strlen(airlineName) > 0) {
                    int total = 0;
                    for (int i = 0; i < shared.count; i++) {
                        if (strcmp(shared.data[i].airlineName, airlineName) == 0 && shared.data[i].status == 0) {
                            total += shared.data[i].amount;
                            shared.data[i].status = 1;
                        }
                    }
                    
                    printf("Processing payment for %s: $%d\n", airlineName, total);
                    
                    int fd_back = open(sp_to_atc_fifo, O_WRONLY);
                    if (fd_back >= 0) {
                        write(fd_back, &total, sizeof(int));
                        close(fd_back);
                    }
                }
            }
            usleep(100000); // 10ms delay
        }
    } else { // Parent process
        close(pipe_data[0]); // Close read end

        while (1) {
            int fd_avn = open(avn_fifo, O_RDONLY);
            if (fd_avn < 0) {
                perror("Failed to open AVN FIFO");
                sleep(1);
                continue;
            }

            TicketData td;
            ssize_t bytes_read = read(fd_avn, &td, sizeof(TicketData));
            close(fd_avn);
                printf("Received new booking for %s\n", td.airlineName);
            
            if (bytes_read == sizeof(TicketData)) {

                if (write(pipe_data[1], &td, sizeof(TicketData)) < 0) {
                    perror("Failed to send to child");
                }
            }
            usleep(100000); // 15ms delay
        }
    }

    return 0;
    */
    // Create FIFOs
    if (mkfifo(avn_fifo, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create AVNtoSP FIFO");
        exit(1);
    }
    if (mkfifo(atc_to_sp_fifo, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create ATCtoSP FIFO");
        exit(1);
    }
    if (mkfifo(sp_to_atc_fifo, 0666) == -1 && errno != EEXIST) {
        perror("Failed to create SPtoATC FIFO");
        exit(1);
    }
    TotalData total;
    total.count = 0;
    TicketData latest_td = {0};
    int fd_avn, fd_atc;
    fd_set read_fds;
    int max_fd;

    while (1) {
        // Open AVN and ATC FIFOs non-blocking
        fd_avn = open(avn_fifo, O_RDONLY | O_NONBLOCK);
        fd_atc = open(atc_to_sp_fifo, O_RDONLY | O_NONBLOCK);
        if (fd_avn < 0 || fd_atc < 0) {
            perror("Failed to open one or more FIFOs");
            sleep(1);
            continue;
        }

        FD_ZERO(&read_fds);
        FD_SET(fd_avn, &read_fds);
        FD_SET(fd_atc, &read_fds);
        max_fd = (fd_avn > fd_atc) ? fd_avn : fd_atc;

        int activity = select(max_fd + 1, &read_fds, NULL, NULL, NULL);
        if (activity < 0) {
            perror("select() error");
            continue;
        }

        // Handle AVN → SP
        if (FD_ISSET(fd_avn, &read_fds)) {
            ssize_t r = read(fd_avn, &latest_td, sizeof(TicketData));
            if (r > 0) {
                printf("[SP] Received booking from AVN: %s\n", latest_td.airlineName);
                total.ticket[total.count++] = latest_td;
            }
        }

        // Handle ATC → SP (request)
        if (FD_ISSET(fd_atc, &read_fds)) {
            char Name[30];
            ssize_t r = read(fd_atc, Name, sizeof(Name));
            if (r > 0) {
                Name[r] = '\0';
                int amount = 0;
                    FILE* logFile = fopen("avn_report.log", "a");
                    if (!logFile) 
                    {
                        perror("Failed to open log file");
                        exit(EXIT_FAILURE);
                    }
                printf("[SP] Received request from ATC: %s\n", Name);
                printf("Following are the Details of the tickets Unpaid --> \n");
                for(int i=0;i<total.count;i++)
                { 
                      if (strcmp(total.ticket[i].airlineName, Name) == 0 && total.ticket[i].status == 0) 
                      {
                              printf("\033[34m[SP] Ticket Received:\n TicketID: %d\nStatus: Paid\n  Amount: %d\n  Airline Type: %d\n  Airline ID: %d\n  Airline Name: \033[31m%s\033[34m\n\033[0m",total.ticket[i].id
               , total.ticket[i].amount, total.ticket[i].airlinetype, total.ticket[i].airlineId, total.ticket[i].airlineName);
                              fprintf(logFile, "\033[34m[SP] Ticket Received:\n TicketID: %d\nStatus: Paid\n  Amount: %d\n  Airline Type: %d\n  Airline ID: %d\n  Airline Name: \033[31m%s\033[34m\n\033[0m",total.ticket[i].id, total.ticket[i].amount, total.ticket[i].airlinetype, total.ticket[i].airlineId, total.ticket[i].airlineName);
                      }
                }
                fprintf(logFile, "\n");
                fclose(logFile);
                int fd_resp = open(sp_to_atc_fifo, O_WRONLY);
                if (fd_resp >= 0) {
                    
                    for(int i=0;i<total.count;i++)
                    {
                        if (strcmp(total.ticket[i].airlineName, Name) == 0 && total.ticket[i].status == 0) 
                        {
                            amount += total.ticket[i].amount;
                            total.ticket[i].status = 1;
                        }
                    }
                    write(fd_resp, &amount, sizeof(int));
                    close(fd_resp);
                    printf("[SP] Sent booking to ATC: %s\n", latest_td.airlineName);
                } else {
                    perror("Failed to open SPtoATC FIFO");
                }
            }
        }

        close(fd_avn);
        close(fd_atc);
    }

    return 0;
    
}
