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

#define MAX_FLIGHTS 20
#define MAX_AIRLINES 6
#define MAX_RUNWAYS 3
#define FUEL_THRESHOLD 20
#define MAX_VIOLATION_MSG 512

time_t simulationStartTime;
bool simulationRunning = false;

int getCurrentSimulationTime() {
    return (int)(time(NULL) - simulationStartTime);
}

bool avnTriggered = false;
pthread_mutex_t runwayLocks[MAX_RUNWAYS];
pthread_mutex_t flightDataMutex;
volatile bool flightDataReady = true; 

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

Flight assignFlight(Flight* src) {
    Flight dest;
    strcpy(dest.id, src->id);
    strcpy(dest.name, src->name);
    dest.airlineId = src->airlineId;
    dest.type = src->type;
    dest.phase = src->phase;
    dest.assignedRunway = src->assignedRunway;
    dest.direction = src->direction;
    dest.speed = src->speed;
    dest.hasFault = src->hasFault;
    dest.isEmergency = src->isEmergency;
    dest.fuelLevel = src->fuelLevel;
    dest.isDeparture = src->isDeparture;
    dest.avnStatus = src->avnStatus;
    dest.lastUpdated = src->lastUpdated;
    dest.avnCount = src->avnCount;
    dest.priority = src->priority;
    dest.scheduledTime = src->scheduledTime;
    dest.estimatedWait = src->estimatedWait;
    dest.altitude = src->altitude;
    dest.position = src->position;
    dest.x = src->x;
    dest.y = src->y;
    dest.targetX = src->targetX;
    dest.targetY = src->targetY;
    dest.velocityX = src->velocityX;
    dest.velocityY = src->velocityY;
    dest.isVIP = src->isVIP;
    dest.lastReportedViolation = src->lastReportedViolation;
    return dest;
}

typedef struct {
    char name[30];
    FlightType type;
    int totalAircrafts;
    int activeFlights;
    int violations;
} Airline;

typedef struct {
    Flight flight;
} AVNData;

AVNData avn;

typedef struct {
    Flight* flights;
    int flightCount;
} ThreadData;

Flight* runwayAQueue[MAX_FLIGHTS];
Flight* runwayBQueue[MAX_FLIGHTS];
Flight* runwayCQueue[MAX_FLIGHTS];
int runwayACount = 0, runwayBCount = 0, runwayCCount = 0;

sfTexture* commercialTexture;
sfTexture* cargoTexture;
sfTexture* emergencyTexture;
sfTexture* airTexture;

void loadTextures() {
    commercialTexture = sfTexture_createFromFile("comm.png", NULL);
    cargoTexture = sfTexture_createFromFile("cargo.png", NULL);
    emergencyTexture = sfTexture_createFromFile("emergency.png", NULL);
    airTexture = sfTexture_createFromFile("air.png", NULL);
    if (!commercialTexture || !cargoTexture || !emergencyTexture || !airTexture) {
        printf("Error loading airplane textures!\n");
        exit(1);
    }
}

int Flights_Comparison(const void* a, const void* b) {
    Flight* f1 = *(Flight**)a;
    Flight* f2 = *(Flight**)b;
    if (f1->isEmergency != f2->isEmergency) return f2->isEmergency - f1->isEmergency;
    if (f1->priority != f2->priority) return f2->priority - f1->priority;
    if (f1->scheduledTime != f2->scheduledTime) return f1->scheduledTime - f2->scheduledTime;
    return strcmp(f1->id, f2->id);
}

int EmergencyPriorityComparison(const void* a, const void* b) {
    Flight* f1 = *(Flight**)a;
    Flight* f2 = *(Flight**)b;
    if (f1->isEmergency != f2->isEmergency) return f2->isEmergency - f1->isEmergency;
    return f2->priority - f1->priority;
}

void QueuesReordering() {
    qsort(runwayAQueue, runwayACount, sizeof(Flight*), EmergencyPriorityComparison);
    qsort(runwayBQueue, runwayBCount, sizeof(Flight*), EmergencyPriorityComparison);
    qsort(runwayCQueue, runwayCCount, sizeof(Flight*), EmergencyPriorityComparison);
}

void sortQueue(Flight** queue, int count) {
    qsort(queue, count, sizeof(Flight*), Flights_Comparison);
}

const char* getDirectionString(Direction d) {
    if (d == NORTH) return "NORTH";
    else if (d == SOUTH) return "SOUTH";
    else if (d == EAST) return "EAST";
    else if (d == WEST) return "WEST";
    else return "UNDEFINED";
}

const char* getPhaseString(FlightPhase p) {
    if (p == HOLDING) return "HOLDING";
    else if (p == APPROACH) return "APPROACH";
    else if (p == LANDING) return "LANDING";
    else if (p == TAXI) return "TAXI";
    else if (p == AT_GATE) return "AT_GATE";
    else if (p == TAKEOFF_ROLL) return "TAKEOFF_ROLL";
    else if (p == CLIMB) return "CLIMB";
    else if (p == CRUISE) return "CRUISE";
    else return "UNKNOWN";
}

const char* getFlightTypeString(FlightType t) {
    if (t == COMMERCIAL) return "COMMERCIAL";
    else if (t == CARGO) return "CARGO";
    else if (t == EMERGENCY) return "EMERGENCY";
    else return "UNKNOWN";
}

const char* getRunwayString(Runway r) {
    if (r == RWY_A) return "RWY-A";
    else if (r == RWY_B) return "RWY-B";
    else if (r == RWY_C) return "RWY-C";
    else return "NO_RUNWAY";
}

const char* getAirlineName(int airlineId) {
    if (airlineId == 0) return "PIA";
    else if (airlineId == 1) return "AirBlue";
    else if (airlineId == 2) return "FedEx";
    else if (airlineId == 3) return "Pakistan Airforce";
    else if (airlineId == 4) return "Blue Dart";
    else if (airlineId == 5) return "AghaKhan Air";
    else return "Unknown Airline";
}

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

int getSafeAltitudeForPhase(FlightPhase phase) {
    switch (phase) {
        case HOLDING: return 9000;
        case APPROACH: return 3000;
        case LANDING: return 1000;
        case TAXI: return 0;
        case AT_GATE: return 0;
        case TAKEOFF_ROLL: return 0;
        case CLIMB: return 15000;
        case CRUISE: return 35000;
        default: return 0;
    }
}

typedef struct {
    int min;
    int max;
} PositionRange;

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

static inline bool check_speedViolation(Flight* f) {
    if (f->phase == HOLDING) return f->speed > 600;
    else if (f->phase == APPROACH) return f->speed < 240 || f->speed > 290;
    else if (f->phase == LANDING) return f->speed > 240 || f->speed < 30;
    else if (f->phase == TAXI) return f->speed < 15 || f->speed > 30;
    else if (f->phase == AT_GATE) return f->speed != 0;
    else if (f->phase == TAKEOFF_ROLL) return f->speed < 0 || f->speed > 290;
    else if (f->phase == CLIMB) return f->speed < 250 || f->speed > 463;
    else if (f->phase == CRUISE) return f->speed < 800 || f->speed > 900;
    else return false;
}

static inline bool check_altitudeViolation(Flight* f) {
    if (f->phase == HOLDING) return f->altitude > 15000 || f->altitude < 10000;
    else if (f->phase == APPROACH) return f->altitude > 10000 || f->altitude < 3000;
    else if (f->phase == LANDING) return f->altitude > 3000 || f->altitude < 0;
    else if (f->phase == TAXI || f->phase == AT_GATE) return f->altitude != 0;
    else if (f->phase == TAKEOFF_ROLL) return f->altitude > 100 || f->altitude < 0;
    else if (f->phase == CLIMB) return f->altitude < 1000 || f->altitude > 30000;
    else if (f->phase == CRUISE) return f->altitude < 30000 || f->altitude > 166670;
    else return false;
}

static inline bool check_positionViolation(Flight* f) {
    PositionRange range = getSafePositionRangeForPhase(f->phase);
    return f->position < range.min || f->position > range.max;
}

static inline bool check_RunwayDirection(Flight* f) {
    if (f->direction == NORTH || f->direction == SOUTH) {
        return f->assignedRunway == RWY_A || f->assignedRunway == RWY_C;
    }
    else if (f->direction == EAST || f->direction == WEST) {
        return f->assignedRunway == RWY_B || f->assignedRunway == RWY_C;
    }
    else return false;
}

static inline bool isCargoRunwayValid(Flight* f) {
    return (f->type != CARGO || f->assignedRunway == RWY_C || f->isEmergency);
}

static inline bool isRunwayViolation(Flight* f) {
    return !check_RunwayDirection(f) || !isCargoRunwayValid(f);
}

void checkForFaults(Flight* f) {
    if (f->phase == TAXI || f->phase == AT_GATE) {
        if (rand() % 100 < 5) {
            f->hasFault = true;
            printf("FAULT DETECTED! Flight %s has ground fault\n", f->id);
        }
    }
}

Runway assignRunway(Flight* f) {
    if (f->isEmergency || f->fuelLevel < FUEL_THRESHOLD) 
    {   
        f->targetX = 650;
        f->x=650;
        return RWY_C;
    }
    if (f->type == CARGO) 
    {
        f->targetX = 650;
        f->x=650;
        return RWY_C;
    }
    if (f->isDeparture) {
        if (runwayBCount < MAX_FLIGHTS && (f->direction == EAST || f->direction == WEST)) 
        {
            if(f->direction == WEST)
            {
                f->targetX = 360;
                f->x=360;
            }
            else
            {
                f->targetX = 450;
                f->x=450;
            }
            return RWY_B;
        }
        f->targetX = 650;
        f->x=650;
        return RWY_C;
    }
    if (runwayACount < MAX_FLIGHTS && (f->direction == NORTH || f->direction == SOUTH)) 
    {
        if(f->direction == SOUTH)
        {
        f->targetX = 40;
        f->x=40;
        }
        else
        {
        f->targetX = 200;
        f->x=200;
        }
        return RWY_A;
    }
    f->targetX = 650;
    f->x=650;
    return RWY_C;
}

void printFlightStatus(Flight flights[], int flightCount) {
    printf("\n===== Current Flight Status =====\n");
    printf("Time Remaining: %d seconds\n", getCurrentSimulationTime());
    printf("--------------------------------\n");
    for (int i = 0; i < flightCount; i++) {
        printf("Flight %s | %s | %s | %s | Runway: %s | Wait: %ds\n",
               flights[i].id, getAirlineName(flights[i].airlineId),
               getFlightTypeString(flights[i].type), getPhaseString(flights[i].phase),
               getRunwayString(flights[i].assignedRunway), flights[i].estimatedWait);
    }
}

Flight generateFlight(Airline airlines[], int airlineId, int timeCounter, const char* flightId, bool isDeparture) {
    Flight f = { 0 };
    
    strncpy(f.id, flightId, sizeof(f.id) - 1);
    f.airlineId = airlineId;
    f.isDeparture = isDeparture;
    f.lastUpdated = time(NULL);
    f.assignedRunway = NO_RUNWAY;
    f.lastReportedViolation = 0;
    f.sprite = sfSprite_create();
    sfSprite_setTexture(f.sprite, commercialTexture, sfTrue);
    if (f.type == CARGO) {
        sfSprite_setTexture(f.sprite, cargoTexture, sfTrue);
    } else if (f.isEmergency || airlineId == 5) {
        sfSprite_setTexture(f.sprite, emergencyTexture, sfTrue);
    } else if (airlineId == 3) {
        sfSprite_setTexture(f.sprite, airTexture, sfTrue);
    }
    Airline airline = airlines[airlineId];
    f.type = airline.type;
    strncpy(f.name, airline.name, sizeof(f.name) - 1);
    f.isEmergency = false;
    f.isVIP = false;
    if (isDeparture) {
        f.fuelLevel = 100;
        f.position = 0;
        f.altitude = 0;
        f.phase = AT_GATE;
    } else {
        f.fuelLevel = rand() % 101;
        f.position = rand() % 401 + 300;
        f.altitude = 9000;
        f.phase = HOLDING;
    }
    if (f.fuelLevel < FUEL_THRESHOLD || airlineId == 3 || airlineId == 5) {
        f.isEmergency = true;
        f.priority = 3;
    }
    if (isDeparture) {
        f.direction = (rand() % 2) ? EAST : WEST;
    } else {
        f.direction = (rand() % 2) ? NORTH : SOUTH;
    }
    f.hasFault = false;
    f.avnStatus = INACTIVE;
    f.avnCount = 0;
    if(f.direction == NORTH || f.direction == EAST)
    {
        f.y = 250;
        sfSprite_setRotation(f.sprite, 180);

    }
    else
    { 
        if(!f.isDeparture)
        f.y = 380;
        else
        f.y = 300;
    }
    sfSprite_setPosition(f.sprite, (sfVector2f){f.x, f.y});
    sfSprite_setScale(f.sprite, (sfVector2f){0.5f, 0.5f});

    
    return f;
}

void setTaxiSpeed(Flight* f) { f->speed = rand() % 16 + 15; }
void setHoldingSpeed(Flight* f) { f->speed = rand() % 201 + 400; }
void setApproachSpeed(Flight* f) { f->speed = rand() % 51 + 240; }
void setLandingSpeed(Flight* f) { f->speed = rand() % 211 + 30; }
void setClimbSpeed(Flight* f) { f->speed = rand() % 214 + 250; }
void setTakeoffRollSpeed(Flight* f) { f->speed = rand() % 291; }
void setInitialSpeedForGate(Flight* f) { f->speed = 0; }
void transitionToTakeoffRoll(Flight* f) { f->speed = 0; }
void transitionToCruise(Flight* f) {
    f->speed = f->speed < 800 ? 800 : (f->speed > 900 ? 900 : f->speed);
}

void activateAVN(Flight* f) {
    if (f->avnStatus == INACTIVE) {
        f->avnStatus = ACTIVE;
        avnTriggered = true;
        printf("\n!!! AVN ACTIVATED for Flight %s !!!\n", f->id);
    }
}

void handleAVNspeed(Flight* f) {
    char arr[20] = "ATCtoAVN";
    if (mkfifo(arr, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo failed");
        return;
    }
    if (check_speedViolation(f)) {
        printf("\033[1;31m!!!! Speed Violation has Occurred !!!!\033[0m\n");
        int fd = open(arr, O_WRONLY);
        activateAVN(f);
        f->avnCount++;
        int minSpeed = getMinAllowedSpeed(f->phase);
        int newSpeed;
        if (f->speed < minSpeed) {
            newSpeed = minSpeed + (minSpeed - f->speed)/2;
        } else {
            newSpeed = minSpeed + (f->speed - minSpeed)/2;
        }
        avn.flight = assignFlight(f);
        write(fd, &avn, sizeof(AVNData));
        f->speed = newSpeed;
        close(fd);
    }
}

void handleAVNposition(Flight* f) {
    PositionRange safeRange = getSafePositionRangeForPhase(f->phase);
    char arr[20] = "ATCtoAVN";
    if (mkfifo(arr, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo failed");
        return;
    }
    if (f->position < safeRange.min || f->position > safeRange.max) {
        printf("\033[1;31m!!!! Position Violation has Occurred !!!!\033[0m\n");
        int fd = open(arr, O_WRONLY);
        activateAVN(f);
        f->avnCount++;
        int newPosition;
        avn.flight = assignFlight(f);
        write(fd, &avn, sizeof(AVNData));
        close(fd);
        if (f->position < safeRange.min) {
            newPosition = safeRange.min + (safeRange.max - safeRange.min)/4;
        } else {
            newPosition = safeRange.max - (safeRange.max - safeRange.min)/4;
        }
        f->position = newPosition;
    }
}

void handleAVNaltitude(Flight* f) {
    int safeAltitude = getSafeAltitudeForPhase(f->phase);
    int tolerance = 500;
    char arr[20] = "ATCtoAVN";
    if (mkfifo(arr, 0666) < 0 && errno != EEXIST) {
        perror("mkfifo failed");
        return;
    }
    if (abs(f->altitude - safeAltitude) > tolerance) {
        printf("\033[1;31m!!!! Altitude Violation has Occurred !!!!\033[0m\n");
        int fd = open(arr, O_WRONLY);
        activateAVN(f);
        f->avnCount++;
        int newAltitude;
        if (f->altitude < safeAltitude) {
            newAltitude = safeAltitude - tolerance/2;
        } else {
            newAltitude = safeAltitude + tolerance/2;
        }
        avn.flight = assignFlight(f);
        write(fd, &avn, sizeof(AVNData));
        close(fd);
        f->altitude = newAltitude;
    }
}

bool checkForViolations(Flight* f, char* violation_msg, size_t msg_size) {
    bool violationDetected = false;
    time_t now = time(NULL);
    violation_msg[0] = '\0';
    char temp_msg[256];
    if (check_speedViolation(f)) {
        handleAVNspeed(f);
        int minSpeed = getMinAllowedSpeed(f->phase);
        snprintf(temp_msg, sizeof(temp_msg), "Speed Violation: %d km/h (Safe: %d+)", f->speed, minSpeed);
        strncat(violation_msg, temp_msg, msg_size - strlen(violation_msg) - 1);
        violationDetected = true;
    }
    if (check_positionViolation(f)) {
        handleAVNposition(f);
        PositionRange safeRange = getSafePositionRangeForPhase(f->phase);
        snprintf(temp_msg, sizeof(temp_msg), "%sPosition Violation: %d (Safe: %d-%d)",
                 violation_msg[0] ? "; " : "", f->position, safeRange.min, safeRange.max);
        strncat(violation_msg, temp_msg, msg_size - strlen(violation_msg) - 1);
        violationDetected = true;
    }
    if (check_altitudeViolation(f)) {
        handleAVNaltitude(f);
        int safeAltitude = getSafeAltitudeForPhase(f->phase);
        snprintf(temp_msg, sizeof(temp_msg), "%sAltitude Violation: %d ft (Safe: %d ft)",
                 violation_msg[0] ? "; " : "", f->altitude, safeAltitude);
        strncat(violation_msg, temp_msg, msg_size - strlen(violation_msg) - 1);
        violationDetected = true;
    }
    if (isRunwayViolation(f)) {
        snprintf(temp_msg, sizeof(temp_msg), "%sRunway Violation: %s (Direction: %s)",
                 violation_msg[0] ? "; " : "", getRunwayString(f->assignedRunway),
                 getDirectionString(f->direction));
        strncat(violation_msg, temp_msg, msg_size - strlen(violation_msg) - 1);
        violationDetected = true;
    }
    if (violationDetected) {
        f->lastUpdated = now;
        f->lastReportedViolation = now;
    }
    return violationDetected;
}

void cleanup_mutex(void* arg) {
    pthread_mutex_t* mutex = (pthread_mutex_t*)arg;
    pthread_mutex_unlock(mutex);
}

void initializeFlightPosition(Flight* f) {
    if(f->direction == SOUTH || f->direction == WEST)
    {
          switch (f->phase) {
              case HOLDING: f->targetY = 351; break;
              case APPROACH: f->targetY = 201; break;
              case LANDING: f->targetY = 101; break;
              case TAXI: 
              {
                  if(!f->isDeparture)
                  f->targetY = 51;
                  else
                  f->targetY = 450;
                  break;
              }
              case AT_GATE: 
              {
                  if(!f->isDeparture)
                  f->targetY = 20;
                  else
                  f->targetY = 550;
                  break;
              }
              case TAKEOFF_ROLL: f->targetY = 250; break;
              case CLIMB: f->targetY = 150; break;
              case CRUISE: f->targetY = 50; break;
              default: f->targetY = 500; break;
          }
    }
    else
    {
          switch (f->phase) {
              case HOLDING: f->targetY = 20; break;
              case APPROACH: f->targetY = 250; break;
              case LANDING: f->targetY = 400; break;
             case TAXI: 
              {
                  if(!f->isDeparture)
                  f->targetY = 501;
                  else
                  f->targetY = 150;
                  break;
              }
              case AT_GATE: 
              {
                  if(!f->isDeparture)
                  f->targetY = 600;
                  else
                  f->targetY = 20;
                  break;
              }
              case TAKEOFF_ROLL: f->targetY = 250; break;
              case CLIMB: f->targetY = 400; break;
              case CRUISE: f->targetY = 550; break;
              default: f->targetY = 500; break;
          }
    }
    
}

float lerp(float start, float end, float t) {
    return start + (end - start) * t;
}

void updateFlightPosition(Flight* f, float deltaTime) {
    // Interpolate towards target position
    float lerpSpeed = 0.3;
    if(lerpSpeed==0)
    lerpSpeed=0.2f;
    //f->x = lerp(f->x, f->targetX, lerpSpeed * deltaTime);
    f->y = lerp(f->y, f->targetY, lerpSpeed * deltaTime);
    // Update sprite position
    sfSprite_setPosition(f->sprite, (sfVector2f){f->x, f->y});
}

void RealTimeSimulation(Flight* f) {
    printf("\n--- Simulating Flight %s ---\n", f->id);
    time_t startTime = time(NULL);
    printf("Simulation Start Time: %s", ctime(&startTime));
    printf("Airline: %s | Type: %s | Direction: %s | Fuel: %d%%\n",
           getAirlineName(f->airlineId), getFlightTypeString(f->type),
           getDirectionString(f->direction), f->fuelLevel);
    if (f->fuelLevel < FUEL_THRESHOLD && !f->isEmergency) {
        f->isEmergency = true;
        printf("LOW FUEL EMERGENCY! Flight %s\n", f->id);
    }
    f->assignedRunway = assignRunway(f);
    printf("Assigned Runway: %s\n", getRunwayString(f->assignedRunway));
    pthread_mutex_t* selectedLock = NULL;
    switch (f->assignedRunway) {
        case RWY_A: selectedLock = &runwayLocks[0]; break;
        case RWY_B: selectedLock = &runwayLocks[1]; break;
        case RWY_C: selectedLock = &runwayLocks[2]; break;
        default: break;
    }
    if (selectedLock != NULL) {
        pthread_mutex_lock(selectedLock);
        printf("[LOCKED] %s runway in use by flight %s\n", getRunwayString(f->assignedRunway), f->id);
    }
    checkForFaults(f);
    char violation_msg[MAX_VIOLATION_MSG];
    bool isArrival = f->direction == NORTH || f->direction == SOUTH;
    sfClock* clock = sfClock_create();
    float phaseDuration = 2.0f; 
    float elapsedTime = 0.0f;

    if (isArrival) {
        f->phase = HOLDING;
        printf("Flight %s entering HOLDING phase...\n", f->id);
        setHoldingSpeed(f);
        f->altitude = rand() % 9001 + 8000;
        f->position = rand() % 601 + 200;
        initializeFlightPosition(f);
        const char* dir = getDirectionString(f->direction);
        if(strcmp(dir, "NORTH") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.48f, 0.48f});
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667); // ~60 FPS
        }

        f->phase = APPROACH;
        printf("Flight %s moving to APPROACH phase...\n", f->id);
        setApproachSpeed(f);
        f->altitude = rand() % 10000 + 1500;
        f->position = rand() % 901;
        initializeFlightPosition(f);
        if(strcmp(dir, "NORTH") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x += 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.47f, 0.47f});
        
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = LANDING;
        printf("Flight %s starting LANDING phase...\n", f->id);
        setLandingSpeed(f);
        f->altitude = rand() % 4500;
        f->position = rand() % 300;
        initializeFlightPosition(f);
        if(strcmp(dir, "NORTH") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x += 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.43f, 0.43f});
        
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = TAXI;
        printf("Flight %s taxiing to gate...\n", f->id);
        setTaxiSpeed(f);
        f->altitude = rand() % 2;
        f->position = rand() % 70;
        initializeFlightPosition(f);
        if(strcmp(dir, "NORTH") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x += 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.39f, 0.39f});
        
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = AT_GATE;
        printf("Flight %s parked at gate.\n", f->id);
        setInitialSpeedForGate(f);
        f->altitude = rand() % 2;
        f->position = rand() % 70;
        initializeFlightPosition(f);
        if(strcmp(dir, "NORTH") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x += 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.36f, 0.36f});
        
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }
    } else {
        f->phase = AT_GATE;
        printf("Flight %s starting at gate (preparing for departure).\n", f->id);
        setInitialSpeedForGate(f);
        f->altitude = 0;
        f->position = rand() % 70;
        initializeFlightPosition(f);
        const char* dir = getDirectionString(f->direction);
        if(strcmp(dir, "EAST") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x -= 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.36f, 0.36f});
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = TAXI;
        printf("Flight %s taxiing to runway...\n", f->id);
        setTaxiSpeed(f);
        f->altitude = 0;
        f->position = rand() % 70;
        initializeFlightPosition(f);
        if(strcmp(dir, "EAST") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x -= 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.39f, 0.39f});
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = TAKEOFF_ROLL;
        printf("Flight %s starting takeoff roll...\n", f->id);
        transitionToTakeoffRoll(f);
        f->altitude = rand() % 150;
        f->position = rand() % 250;
        initializeFlightPosition(f);
        if(strcmp(dir, "EAST") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x -= 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.43f, 0.43f});
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = CLIMB;
        printf("Flight %s climbing after takeoff...\n", f->id);
        setClimbSpeed(f);
        f->altitude = rand() % 30101 + 900;
        f->position = rand() % 851 + 50;
        initializeFlightPosition(f);
        if(strcmp(dir, "EAST") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x -= 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.47f, 0.47f});
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }

        f->phase = CRUISE;
        printf("Flight %s cruising at safe altitude...\n", f->id);
        transitionToCruise(f);
        f->altitude = rand() % 20001 + 25000;
        f->position = rand() % 1101 + 50;
        initializeFlightPosition(f);
        if(strcmp(dir, "EAST") == 0)
        {
              sfSprite_setRotation(f->sprite, 180.0f);
              f->x += 10;
        }
        else
        {
              sfSprite_setRotation(f->sprite, 0);
              f->x -= 10;
        }
        sfSprite_setScale(f->sprite, (sfVector2f){0.49f, 0.49f});
        elapsedTime = 0.0f;
        while (elapsedTime < phaseDuration && simulationRunning) {
            float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
            elapsedTime += deltaTime;
            pthread_mutex_lock(&flightDataMutex);
            updateFlightPosition(f, deltaTime);
            checkForViolations(f, violation_msg, sizeof(violation_msg));
            pthread_mutex_unlock(&flightDataMutex);
            usleep(16667);
        }
    }

    if (selectedLock != NULL) {
        pthread_mutex_unlock(selectedLock);
        printf("[RELEASED] %s runway is now available\n", getRunwayString(f->assignedRunway));
        sfSprite_destroy(f->sprite);
        f->sprite = NULL;
    }
    sfClock_destroy(clock);
    printf("Flight %s completed simulation at %s", f->id, ctime(&startTime));
}

void displayFlightState(Flight* f) {
    printf("Flight %s | Phase: %s | ", f->id, getPhaseString(f->phase));
    switch (f->phase) {
        case HOLDING: printf("[~==~] Holding"); break;
        case APPROACH: printf("[->-] Approaching"); break;
        case LANDING: printf("[>-<] Landing"); break;
        case TAXI: printf("[==>] Moving on ground"); break;
        case AT_GATE: printf("[||] Parked"); break;
        case TAKEOFF_ROLL: printf("[=>] Accelerating"); break;
        case CLIMB: printf("[/^\\] Ascending"); break;
        case CRUISE: printf("[~~~] Cruising"); break;
        default: printf("[?] Unknown"); break;
    }
    printf("\n");
}

void displayActiveViolations(Flight* flights, int flightCount) {
    printf("\033[92m");
    int activeViolations = 0;
    printf("\n===== AirControlX Dashboard =====\n");
    printf("Time: %d seconds\n", getCurrentSimulationTime());
    printf("--------------------------------\n");
    for (int i = 0; i < flightCount; i++) {
        if (flights[i].avnStatus == ACTIVE || flights[i].avnCount > 0) {
            activeViolations++;
        }
    }
    printf("Number of Active Violations: %d\n", activeViolations);
    printf("--------------------------------\n");
    printf("Aircraft with Active Violations:\n");
    for (int i = 0; i < flightCount; i++) {
        if (flights[i].avnStatus == ACTIVE || flights[i].avnCount > 0) {
            printf("Flight %s | Airline: %s | Violations: %d | Status: %s\n",
                   flights[i].id, getAirlineName(flights[i].airlineId),
                   flights[i].avnCount, flights[i].avnStatus == ACTIVE ? "ACTIVE" : "INACTIVE");
            if (check_speedViolation(&flights[i])) {
                printf("  - Speed Violation: %d km/h (Safe: %d+)\n",
                       flights[i].speed, getMinAllowedSpeed(flights[i].phase));
            }
            if (check_altitudeViolation(&flights[i])) {
                printf("  - Altitude Violation: %d ft (Safe: %d)\n",
                       flights[i].altitude, getSafeAltitudeForPhase(flights[i].phase));
            }
            if (check_positionViolation(&flights[i])) {
                PositionRange range = getSafePositionRangeForPhase(flights[i].phase);
                printf("  - Position Violation: %d (Safe: %d-%d)\n",
                       flights[i].position, range.min, range.max);
            }
            if (isRunwayViolation(&flights[i])) {
                printf("  - Runway Violation: %s (Direction: %s)\n",
                       getRunwayString(flights[i].assignedRunway), getDirectionString(flights[i].direction));
            }
        }
    }
    printf("--------------------------------\n");
    printf("Flight States Visualization:\n");
    for (int i = 0; i < flightCount; i++) {
        displayFlightState(&flights[i]);
    }
    printf("================================\n");
    printf("\033[0m");
}

void logS(Flight* flights, int flightCount) {
    FILE* logFile = fopen("simulation_log.txt", "a");
    if (!logFile) {
        perror("Failed to open log file");
        return;
    }
    fprintf(logFile, "\n--- Simulation Log ---\n");
    fprintf(logFile, "Total Flights: %d\n", flightCount);
    int totalAVNTriggers = 0, totalViolations = 0, emergencyLandings = 0;
    for (int i = 0; i < flightCount; i++) {
        int runwayViolation = isRunwayViolation(&flights[i]) ? 1 : 0;
        fprintf(logFile,
                "Flight %s | Airline: %s | Type: %s | AVN: %d | Violations: %d | Fuel: %d%%\n",
                flights[i].id, getAirlineName(flights[i].airlineId),
                getFlightTypeString(flights[i].type), flights[i].avnCount,
                runwayViolation, flights[i].fuelLevel);
        totalAVNTriggers += flights[i].avnCount;
        totalViolations += runwayViolation;
        if (flights[i].isEmergency) emergencyLandings++;
    }
    fprintf(logFile, "Total AVN Triggers: %d\n", totalAVNTriggers);
    fprintf(logFile, "Total Violations: %d\n", totalViolations);
    fprintf(logFile, "Emergency Landings: %d\n", emergencyLandings);
    fclose(logFile);
}

void* Flight_ThreadScheduling(void* arg) {
    Flight* f = (Flight*)arg;
    if (f->priority == 0) {
        if (f->isEmergency) f->priority = 2;
        else if (f->isVIP || f->fuelLevel < FUEL_THRESHOLD + 10) f->priority = 1;
    }
    printf("\n[Thread] Flight %s scheduled to start in %d seconds (Priority: %d)\n",
           f->id, f->scheduledTime, f->priority);
    sleep(f->scheduledTime);
    RealTimeSimulation(f);
    pthread_exit(NULL);
}

void FindWaitTime() {
    for (int i = 0; i < runwayACount; i++) runwayAQueue[i]->estimatedWait = i * 30;
    for (int i = 0; i < runwayBCount; i++) runwayBQueue[i]->estimatedWait = i * 30;
    for (int i = 0; i < runwayCCount; i++) runwayCQueue[i]->estimatedWait = i * 30;
}

sfRenderWindow* window = NULL;
bool sfmlRunning = false;

void* sfmlThread(void* arg) {
    ThreadData* data = (ThreadData*)arg;
    sfVideoMode mode = {800, 600, 32};
    window = sfRenderWindow_create(mode, "ATCS CONTROLLER SYSTEM", sfResize | sfClose, NULL);
    if (!window) {
        printf("Failed to create SFML window.\n");
        return NULL;
    }
    sfTexture* backgroundTexture = sfTexture_createFromFile("bg.png", NULL);
    if (!backgroundTexture) {
        printf("Error loading background texture!\n");
        sfRenderWindow_destroy(window);
        return NULL;
    }
    sfSprite* backgroundSprite = sfSprite_create();
    sfSprite_setTexture(backgroundSprite, backgroundTexture, sfTrue);
    sfClock* clock = sfClock_create();
    sfmlRunning = true;
    while (sfmlRunning) {
        sfEvent event;
        while (sfRenderWindow_pollEvent(window, &event)) {
            if (event.type == sfEvtClosed) {
                sfmlRunning = false;
            }
        }
        float deltaTime = sfTime_asSeconds(sfClock_restart(clock));
        sfRenderWindow_clear(window, sfBlack);
        sfRenderWindow_drawSprite(window, backgroundSprite, NULL);
        pthread_mutex_lock(&flightDataMutex);
        for (int i = 0; i < data->flightCount; i++) {
            Flight* f = &data->flights[i];
            if (f->sprite) {
                updateFlightPosition(f, deltaTime);
                sfRenderWindow_drawSprite(window, f->sprite, NULL);
            }
        }
        pthread_mutex_unlock(&flightDataMutex);
        sfRenderWindow_display(window);
    }
    sfSprite_destroy(backgroundSprite);
    sfTexture_destroy(backgroundTexture);
    sfClock_destroy(clock);
    sfRenderWindow_destroy(window);
    return NULL;
}

int main() {
    pid_t reader_pid = 0;
    srand(time(NULL));
    simulationStartTime = time(NULL);
    pthread_mutex_init(&flightDataMutex, NULL);
    loadTextures();
    flightDataReady = true;
    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT);
    for (int i = 0; i < MAX_RUNWAYS; i++) {
        pthread_mutex_init(&runwayLocks[i], &attr);
    }
    pthread_mutexattr_destroy(&attr);
    Airline airlines[MAX_AIRLINES] = {
        {"PIA", COMMERCIAL, 6, 4, 0},
        {"AirBlue", COMMERCIAL, 4, 4, 0},
        {"FedEx", CARGO, 3, 2, 0},
        {"Pakistan Airforce", EMERGENCY, 2, 1, 0},
        {"Blue Dart", CARGO, 2, 2, 0},
        {"AghaKhan Air", EMERGENCY, 2, 1, 0}
    };
    Flight flights[MAX_FLIGHTS];
    int flightCount = 0;
    char flightIdBuffer[20];
    pthread_t sfmlThreadId;
    ThreadData threadData = {flights, flightCount};
    pthread_create(&sfmlThreadId, NULL, sfmlThread, &threadData);

    while (1) {
        printf("\n=========== Airline Flight Simulator ===========\n");
        printf("Available Airlines:\n");
        printf("Index\tAirline\t\t\tType\t\tTotal\tActive\tAvailable\n");
        printf("---------------------------------------------------------------\n");
        for (int i = 0; i < MAX_AIRLINES; i++) {
            int available = airlines[i].activeFlights;
            printf("%d\t%-20s\t%-10s\t%d\t%d\t%d\n",
                   i, airlines[i].name, getFlightTypeString(airlines[i].type),
                   airlines[i].totalAircrafts, airlines[i].activeFlights, available);
        }
        printf("\nOptions:\n");
        printf("1. Add Departing Flight\n");
        printf("2. Add Arriving Flight\n");
        printf("3. View Flight Status\n");
        printf("4. Run Simulation\n");
        printf("5. Exit\n");
        printf("6. Pay AVNs\n");
        printf("Enter your choice: ");
        int choice;
        if (scanf("%d", &choice) != 1 || choice < 1 || choice > 6) {
            printf("Invalid choice! Please enter a number between 1 and 6.\n");
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');
        switch (choice) {
        case 1:
        case 2: {
            if (flightCount >= MAX_FLIGHTS) {
                printf("Maximum flight limit reached!\n");
                break;
            }
            printf("Enter airline index (0-%d): ", MAX_AIRLINES - 1);
            int airlineChoice;
            if (scanf("%d", &airlineChoice) != 1 || airlineChoice < 0 || airlineChoice >= MAX_AIRLINES) {
                printf("Invalid airline index!\n");
                while (getchar() != '\n');
                break;
            }
            while (getchar() != '\n');
            if (airlines[airlineChoice].activeFlights<=0 ) {
                printf("No available aircraft for this airline!\n");
                break;
            }
            airlines[airlineChoice].activeFlights--;
            bool isDeparture = (choice == 1);
            snprintf(flightIdBuffer, sizeof(flightIdBuffer), "%s%03d", isDeparture ? "DEP" : "ARR", flightCount + 1);
            pthread_mutex_lock(&flightDataMutex);
            flights[flightCount] = generateFlight(airlines, airlineChoice, getCurrentSimulationTime(), flightIdBuffer, isDeparture);
            flights[flightCount].airlineId = airlineChoice;
            flights[flightCount].assignedRunway = assignRunway(&flights[flightCount]);
            initializeFlightPosition(&flights[flightCount]);
            if (flights[flightCount].type == COMMERCIAL && rand() % 100 < 50) {
                flights[flightCount].isVIP = true;
                printf("Flight %s is VIP\n", flights[flightCount].id);
            }
            if (flights[flightCount].type != EMERGENCY && !flights[flightCount].isVIP) {
                printf("Enter priority (0 = Low, 1 = Medium, 2 = High): ");
                int priority;
                if (scanf("%d", &priority) != 1) {
                    printf("Invalid priority! Setting to 0.\n");
                    priority = 0;
                }
                flights[flightCount].priority = (priority < 0 || priority > 2) ? 0 : priority;
                while (getchar() != '\n');
            } else {
                flights[flightCount].priority = 3;
            }
            printf("Enter scheduled time in seconds: ");
            int scheduledTime;
            if (scanf("%d", &scheduledTime) != 1 || scheduledTime < 0) {
                printf("Invalid time! Setting to 0.\n");
                scheduledTime = 0;
            }
            flights[flightCount].scheduledTime = scheduledTime;
            while (getchar() != '\n');
            if (flights[flightCount].assignedRunway == RWY_A)
                runwayAQueue[runwayACount++] = &flights[flightCount];
            else if (flights[flightCount].assignedRunway == RWY_B)
                runwayBQueue[runwayBCount++] = &flights[flightCount];
            else
                runwayCQueue[runwayCCount++] = &flights[flightCount];
            if (flights[flightCount].isEmergency) {
                QueuesReordering();
            }
            //printf("X: %f, Y: %f\n",flights[flightCount].x, flights[flightCount].y); 
            flightCount++;
            threadData.flightCount = flightCount;
            pthread_mutex_unlock(&flightDataMutex);
            printf("%s Flight %s added to %s\n",
                   isDeparture ? "Departing" : "Arriving",
                   flightIdBuffer, airlines[airlineChoice].name);
            break;
        }
        case 3: {
            if (flightCount == 0) {
                printf("No flights available!\n");
                break;
            }
            printf("\nCurrent Flights:\n");
            for (int i = 0; i < flightCount; i++) {
                printf("%d. %s (%s) - %s | %s | Fuel: %d%% | Runway: %s\n",
                       i + 1, flights[i].id, getAirlineName(flights[i].airlineId),
                       getFlightTypeString(flights[i].type),
                       flights[i].isDeparture ? "DEPARTURE" : "ARRIVAL",
                       flights[i].fuelLevel, getRunwayString(flights[i].assignedRunway));
            }
            printFlightStatus(flights, flightCount);
            break;
        }
        case 4: {
            if (flightCount == 0) {
                printf("No flights to simulate!\n");
                break;
            }
            sortQueue(runwayAQueue, runwayACount);
            sortQueue(runwayBQueue, runwayBCount);
            sortQueue(runwayCQueue, runwayCCount);
            QueuesReordering();
            FindWaitTime();
            simulationRunning = true;
            pthread_t threads[MAX_FLIGHTS];
            int threadIndex = 0;
            for (int i = 0; i < runwayACount; i++) {
                if (pthread_create(&threads[threadIndex++], NULL, Flight_ThreadScheduling, runwayAQueue[i]) != 0) {
                    fprintf(stderr, "Failed to create thread for flight %s\n", runwayAQueue[i]->id);
                }
            }
            for (int i = 0; i < runwayBCount; i++) {
                if (pthread_create(&threads[threadIndex++], NULL, Flight_ThreadScheduling, runwayBQueue[i]) != 0) {
                    fprintf(stderr, "Failed to create thread for flight %s\n", runwayBQueue[i]->id);
                }
            }
            for (int i = 0; i < runwayCCount; i++) {
                if (pthread_create(&threads[threadIndex++], NULL, Flight_ThreadScheduling, runwayCQueue[i]) != 0) {
                    fprintf(stderr, "Failed to create thread for flight %s\n", runwayCQueue[i]->id);
                }
            }
            for (int i = 0; i < threadIndex; i++) {
                pthread_join(threads[i], NULL);
            }
            simulationRunning = false;
            displayActiveViolations(flights, flightCount);
            logS(flights, flightCount);
            printf("Simulation summary logged to 'simulation_log.txt'\n");
            pthread_mutex_lock(&flightDataMutex);
            for (int i = 0; i < flightCount; i++) {
                if (flights[i].sprite) {
                    sfSprite_destroy(flights[i].sprite);
                    flights[i].sprite = NULL;
                }
            }
            memset(flights, 0, sizeof(Flight) * MAX_FLIGHTS);
            flightCount = 0;
            runwayACount = 0;
            runwayBCount = 0;
            runwayCCount = 0;
            threadData.flightCount = flightCount;
            pthread_mutex_unlock(&flightDataMutex);
            break;
        }
        case 5: {
            printf("Exiting simulator...\n");
            sfmlRunning = false;
            pthread_join(sfmlThreadId, NULL);
            if (reader_pid > 0) {
                kill(reader_pid, SIGTERM);
                waitpid(reader_pid, NULL, 0);
            }
            for (int i = 0; i < MAX_RUNWAYS; i++) {
                pthread_mutex_destroy(&runwayLocks[i]);
            }
            pthread_mutex_destroy(&flightDataMutex);
            return 0;
        }
        case 6: {
            char airlineName[30];
            char choice;
            printf("Enter Admin to pay tickets\n");
            printf("Select Airline:\n");
            printf("a. PIA\n");
            printf("b. AirBlue\n");
            printf("c. FedEx\n");
            printf("d. Pakistan Airforce\n");
            printf("e. Blue Dart\n");
            printf("f. AghaKhan Air\n");
            printf("g. All Tickets\n");
            printf("Enter your choice (a-g): ");
            scanf(" %c", &choice);
            switch (choice) {
                case 'a': strcpy(airlineName, "PIA"); break;
                case 'b': strcpy(airlineName, "AirBlue"); break;
                case 'c': strcpy(airlineName, "FedEx"); break;
                case 'd': strcpy(airlineName, "Pakistan Airforce"); break;
                case 'e': strcpy(airlineName, "Blue Dart"); break;
                case 'f': strcpy(airlineName, "AghaKhan Air"); break;
                case 'g' : ;break;
                default: printf("Invalid choice.\n"); break;
            }
            if (choice >= 'a' && choice <= 'f') {
                printf("Processing payment for airline: %s\n", airlineName);
                char fifo_path2[20] = "ATCtoSP";
                if (mkfifo(fifo_path2, 0666) < 0 && errno != EEXIST) {
                    perror("mkfifo2 failed");
                    exit(EXIT_FAILURE);
                }
                int fd2 = open(fifo_path2, O_WRONLY);
                if (fd2 < 0) {
                    perror("open failed");
                    exit(EXIT_FAILURE);
                }
                
                write(fd2, airlineName, sizeof(airlineName));
                close(fd2);
                printf("\033[0;32mDo you want to pay the ticket? (Y/N): \033[0m");
                char choice1;
                scanf(" %c", &choice1);
                if (choice1 == 'Y' || choice1 == 'y') {
                    const char* fifo_path3 = "SPtoATC";
                    if (mkfifo(fifo_path3, 0666) < 0 && errno != EEXIST) {
                        perror("mkfifo failed");
                        exit(EXIT_FAILURE);
                    }
                    int fd3 = open(fifo_path3, O_RDONLY);
                    if (fd3 < 0) {
                        perror("open failed");
                        exit(EXIT_FAILURE);
                    }
                    int amount = 0;
                    read(fd3, &amount, sizeof(int));
                    if (amount > 0) {
                        printf("\033[91mPayed with Amount: %d\033[0m\n", amount);
                    } else {
                        printf("No Ticket Generated.\n");
                    }
                    close(fd3);
                } else {
                    printf("No payment initiated.\n");
                }
            }
            else if (choice == 'g')
            {
                  FILE *file;
                  char filename[] = "avn_report.log";  
                  char ch;

                  file = fopen(filename, "r"); 
                  if (file == NULL) {
                      perror("Error opening file");
                      return 1;
                  }
                  
                  printf("\n\033[92m---------------All Data--------------\n\033[0m");
                  printf("\033[96m");
                  while ((ch = fgetc(file)) != EOF) 
                  {
                      putchar(ch);
                  }
                  printf("\033[0m");
                  fclose(file); 
            }
            break;
        }
        }
    }
    sfmlRunning = false;
    pthread_join(sfmlThreadId, NULL);
    for (int i = 0; i < MAX_RUNWAYS; i++) {
        pthread_mutex_destroy(&runwayLocks[i]);
    }
    pthread_mutex_destroy(&flightDataMutex);
    return 0;
}
