# Airline-control-system
The Airline Control System is a Linux-based operating system simulation project built to demonstrate real-time scheduling and synchronization of airline flights using low-level system programming concepts. Developed as part of an academic exploration, this project simulates how flights are prioritized, scheduled, and managed concurrently, while ensuring synchronization across critical resources like runways and flight control logs.

Project Overview
This system simulates multiple flights, each represented by a thread. Every flight has a specific priority, schedule, and status. The primary objective of the system is to ensure that all flights are managed without conflict using thread synchronization tools such as mutexes and locks.

Runways, for instance, are treated as shared resources. Only one flight can use a runway at a time. This is where mutexes are heavily used to prevent race conditions and ensure mutual exclusion. Priority-based scheduling is simulated manually through control logic, giving emergency or high-priority flights access to runways first.

Key Features
Thread-based Flight Simulation: Each flight operates in its own thread, simulating a real-time environment where flights arrive, wait, or depart based on their status and the availability of resources.

Synchronization Using Mutexes: Critical sections, like access to the runway or logging systems, are protected using mutexes to avoid simultaneous access issues.

Named Pipes (mkfifo): Inter-process communication is implemented using Linux’s mkfifo to pass messages between modules or log status updates in real time. This simulates real-time status broadcasting or logging without using standard console output.

Flight Priority Handling: Flights are assigned priorities such as emergency, VIP, commercial, or cargo. Higher-priority flights are granted runway access before others.

Runway Management: A shared resource simulation where flights must request and be granted runway access based on availability and priority. Mutex locks and conditional variables ensure safe access.

Status Logging: Real-time status updates, flight progress, and any violations (e.g., missed schedules or unpermitted access) are logged for monitoring. Logs can be output via pipes or stored in a file.

Technologies Used
C Programming Language: Core logic is implemented in C for low-level control.

POSIX Threads (pthreads): Used to handle concurrency and simulate parallel execution of flights.

Mutexes & Condition Variables: To manage shared resources and coordinate thread execution.

Named Pipes (mkfifo): For inter-process communication and real-time status updates.

Ubuntu/Linux: Developed and tested on Ubuntu, leveraging Linux system calls and process management features.

Purpose
This project is primarily educational and serves as a practical application of operating system concepts such as concurrency, synchronization, scheduling, and inter-process communication. It was developed to deepen understanding of how complex systems like air traffic control can be abstracted and controlled using core OS principles.
