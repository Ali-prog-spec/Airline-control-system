#include <pthread.h>
#include <semaphore.h>
#include <stdbool.h>
#include <time.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>
#include <SFML/Graphics.h>

typedef enum { COMMERCIAL, CARGO, EMERGENCY, VIP } FlightType;
typedef enum { HOLDING, APPROACH, LANDING, TAXI, AT_GATE, TAKEOFF_ROLL, CLIMB, CRUISE } FlightPhase;
typedef enum { INACTIVE, ACTIVE } AVNStatus;
typedef enum { RWY_A, RWY_B, RWY_C, NO_RUNWAY } Runway;
typedef enum { NORTH, SOUTH, EAST, WEST, UNDEFINED_DIR } Direction;

typedef struct {
    char name[30];
    char id[20];
    int airlineId;
    FlightType type;
    FlightPhase phase;
    Runway assignedRunway;
    Direction direction;
    int speed;
    bool hasFault;
    bool isEmergency;
    int fuelLevel;
    bool isDeparture;
    AVNStatus avnStatus;
    time_t lastUpdated;
    int avnCount;
    int priority;
    int scheduledTime;
    int estimatedWait;
    int altitude;
    int position;
    float x; 
    float y;
    float targetX; 
    float targetY;
    float velocityX;
    float velocityY;
    bool isVIP;
    time_t lastReportedViolation;
    sfSprite* sprite;
} Flight;


typedef struct {
Flight flight;
} AVNData;

typedef struct {
    int min;
    int max;
} PositionRange;

const char* getFlightTypeString(FlightType t) {
    if (t == COMMERCIAL) return "COMMERCIAL";
    else if (t == CARGO) return "CARGO";
    else if (t == EMERGENCY) return "EMERGENCY";
    else return "UNKNOWN";
}

typedef struct {
    int id;
    int status;
    int amount;
    FlightType airlinetype;
    int airlineId;
    char airlineName[30];
} TicketData;
TicketData td;

int getMinAllowedSpeed(FlightPhase phase) {
    if (phase == HOLDING) return 400;
    else if (phase == APPROACH) return 240;
    else if (phase == LANDING) return 30;
    else if (phase == TAXI) return 15;
    else if (phase == AT_GATE) return 0;
    else if (phase == TAKEOFF_ROLL) return 0;
    else if (phase == CLIMB) return 250;
    else if (phase == CRUISE) return 800;
    else return 0;
}

int getMaxAllowedSpeed(FlightPhase phase) {
    switch (phase) {
        case HOLDING:       return 600;
        case APPROACH:      return 290;
        case LANDING:       return 240;
        case TAXI:          return 30;
        case AT_GATE:       return 0;
        case TAKEOFF_ROLL:  return 290;
        case CLIMB:         return 463;
        case CRUISE:        return 900;
        default:            return 0;
    }
}

int getMaxAllowedAltitude(FlightPhase phase) {
    switch (phase) {
        case HOLDING:       return 15000;
        case APPROACH:      return 10000;
        case LANDING:       return 3000;
        case TAXI:          
        case AT_GATE:       return 0;
        case TAKEOFF_ROLL:  return 100;
        case CLIMB:         return 30000;
        case CRUISE:        return 40000;
        default:            return 0;
    }
}


int getSafeAltitudeForPhase(FlightPhase phase) {
    switch (phase) {
        case HOLDING: return 10000;
        case APPROACH: return 3000;
        case LANDING: return 0;
        case TAXI: return 0;
        case AT_GATE: return 0;
        case TAKEOFF_ROLL: return 0;
        case CLIMB: return 1000;
        case CRUISE: return 30000;
        default: return 0;
    }
}
PositionRange getSafePositionRangeForPhase(FlightPhase phase) {
    switch (phase) {
        case HOLDING: return (PositionRange){200, 400};
        case APPROACH: return (PositionRange){100, 300};
        case LANDING: return (PositionRange){50, 200};
        case TAXI: return (PositionRange){10, 50};
        case AT_GATE: return (PositionRange){0, 10};
        case TAKEOFF_ROLL: return (PositionRange){0, 100};
        case CLIMB: return (PositionRange){100, 500};
        case CRUISE: return (PositionRange){500, 1000};
        default: return (PositionRange){0, 1000};
    }
}

int main() {
    const char* fifo_path = "ATCtoAVN";

    if (mkfifo(fifo_path, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo failed");
        exit(EXIT_FAILURE);
    }

    int fd = open(fifo_path, O_RDONLY);
    if (fd < 0) {
        perror("open failed");
        exit(EXIT_FAILURE);
    }

    AVNData a;
    int i = 1;
    while (1) {
        ssize_t bytesRead = read(fd, &a, sizeof(AVNData));
        if (bytesRead == sizeof(AVNData)) {
            FILE* logFile = fopen("avn_report.log", "a");
            if (!logFile) {
                perror("Failed to open log file");
                exit(EXIT_FAILURE);
            }
            printf("-----AVN is Generating-----\n");
            fprintf(logFile, "-----AVN is Generating-----\n");
            const char* flightType = getFlightTypeString(a.flight.type);
            td.id = i;
            printf("AVN ID: %d, Airline Name: %s, Flight Number:%d, Aircraft Type:%s\n", i, a.flight.name,          a.flight.airlineId, flightType);
fprintf(logFile, "AVN ID: %d, Airline Name: %s, Flight Number:%d, Aircraft Type:%s\n", i, a.flight.name, a.flight.airlineId, flightType);
i++;
            int minSpeed = getMinAllowedSpeed(a.flight.phase);
            int maxSpeed = getMaxAllowedSpeed(a.flight.phase);
            int minAltitude = getSafeAltitudeForPhase(a.flight.phase);
            int maxAltitude = getMaxAllowedAltitude(a.flight.phase);
            PositionRange safePos = getSafePositionRangeForPhase(a.flight.phase);

            printf("Permissible Speed: %d - %d | Recorded Speed: %d\n", minSpeed, maxSpeed, a.flight.speed);
            printf("Permissible Altitude: %d - %d | Current Altitude: %d\n", minAltitude, maxAltitude, a.flight.altitude);
            printf("Permissible Position Range: %d - %d | Current Position: %d\n", safePos.min, safePos.max, a.flight.position);

            fprintf(logFile, "Permissible Speed: %d - %d | Recorded Speed: %d\n", minSpeed, maxSpeed, a.flight.speed);
            fprintf(logFile, "Permissible Altitude: %d - %d | Current Altitude: %d\n", minAltitude, maxAltitude, a.flight.altitude);
            fprintf(logFile, "Permissible Position Range: %d - %d | Current Position: %d\n", safePos.min, safePos.max, a.flight.position);

           char avnTimeStr[64];
          struct tm* avnTimeInfo = localtime(&a.flight.lastReportedViolation);
          strftime(avnTimeStr, sizeof(avnTimeStr), "%Y-%m-%d %H:%M:%S", avnTimeInfo);

          time_t dueDateTime = a.flight.lastReportedViolation + 3 * 24 * 60 * 60;
          char dueDateStr[64];
          struct tm* dueDateInfo = localtime(&dueDateTime);
          strftime(dueDateStr, sizeof(dueDateStr), "%Y-%m-%d %H:%M:%S", dueDateInfo);

          int baseChallan = 0;
          if (a.flight.type == COMMERCIAL) baseChallan = 500000;
          else if (a.flight.type == CARGO) baseChallan = 700000;

          float adminFee = baseChallan * 0.15f;
          float totalFine = baseChallan + adminFee;

          printf("AVN Time Issued: %s\n", avnTimeStr);
          fprintf(logFile, "AVN Time Issued: %s\n", avnTimeStr);
          printf("Due Date: %s\n", dueDateStr);
          fprintf(logFile, "Due Date: %s\n", dueDateStr);
          td.status = 0;
          td.amount = totalFine;
          td.airlinetype = a.flight.type;
          strcpy(td.airlineName, a.flight.name);
          td.airlineId = a.flight.airlineId;
          if (baseChallan > 0) {
              printf("Base Challan: RS.%d\n", baseChallan);
              printf("Admin Fee (15%%): RS.%.2f\n", adminFee);
              printf("Total Fine: RS.%.2f\n", totalFine);

              fprintf(logFile, "Base Challan: RS.%d\n", baseChallan);
              fprintf(logFile, "Admin Fee (15%%): RS.%.2f\n", adminFee);
              fprintf(logFile, "Total Fine: RS.%.2f\n", totalFine);
              
              printf("Status: Unpaid\n");
              fprintf(logFile, "Status: Unpaid\n");
          } else {
              printf("No challan applicable for this flight type.\n");
              fprintf(logFile, "No challan applicable for this flight type.\n");
          }
            
            fprintf(logFile, "\n");  
            fclose(logFile);         
            char fifo_path1[20] = "AVNtoSP";
            
            if (mkfifo(fifo_path1, 0666) < 0 && errno != EEXIST) {
            perror("mkfifo1 failed");
            exit(EXIT_FAILURE);
            }
            int fd1 = open(fifo_path1, O_WRONLY);
            if (fd1 < 0) {
                perror("open failed");
                exit(EXIT_FAILURE);
            }
            
            write(fd1, &td, sizeof(TicketData));
            close(fd1);
            
            
        } else if (bytesRead == 0) {
            printf("\n");
            close(fd);
            fd = open(fifo_path, O_RDONLY);  
        } else {
            perror("read error");
            break;
        }
    }

    close(fd);
    return 0;
}
