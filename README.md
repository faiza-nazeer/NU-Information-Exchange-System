# NU-Information-Exchange-System-FAST-NUCES-Multi-Campus-Network-

The system follows a **client-server architecture** and supports communication between campuses and departments
using **TCP (reliable)** and **UDP (connectionless)** protocols.

The system has three main modules:

1. **Central Server ( Islamabad Campus)**
   Acts as the main hub for all campus clients.
   Handles authentication, message routing, and administrative commands.
   Receives heartbeat messages from campus clients via UDP.
   Broadcasts announcements to all campuses.

2. **Campus Clients (Lahore, Karachi, Peshawar, CFD, Multan)**
   Each campus has a client application that simulates department users (Admissions, Academics, IT, Sports).
   Maintains a TCP connection to the server for direct messaging.
   Sends periodic UDP "heartbeat" messages to notify the server that it is online.
   Receives system-wide UDP broadcasts.

4. **Admin Console (part of Server Module)**
   Allows an admin to monitor connected campuses and their last-seen UDP status.
   Can broadcast announcements to all campuses using UDP.


### Application Features

### Hybrid Protocol Usage
1. **TCP (Transmission Control Protocol)**  
  Used for all **critical one-to-one communication**, including:
 Campus client authentication
 Sending messages between campuses
 Receiving administrative commands

2. **UDP (User Datagram Protocol)**  
  Used for **non-critical, broadcast, or status-update messages**,
  including:
   Periodic heartbeat messages from campus clients
   Admin system-wide announcements

### Concurrency Handling

 **TCP Client Connections:**  
  - Each campus client connecting to the server is assigned a separate thread (clientHandler) to handle messaging and communication independently.
  
 **UDP Heartbeat Listener:**  
  - The server has a separate thread (udpListener) listening for all UDP heartbeats concurrently.
  
 **Admin Console:**  
  - A separate thread (adminConsole) handles admin commands without interrupting client-server communication.

**Benefit:** This design ensures that **multiple campuses can send messages, receive broadcasts, and update status
at the same time** without blocking each other.

### Message Routing
 Messages follow the format: TargetCampus,TargetDept,Message
 The server identifies the destination campus and department:
   1. If the exact department is connected, the message is routed there.
   2. If the department is not connected, the message is sent to any available client in that campus.
 Received messages are stored in **message history** on the client for review.

### Heartbeat and Status Monitoring
 Each campus client sends a heartbeat every 10 seconds using UDP with the format Campus|Department.
 The server stores the last seen timestamp and UDP address for each campus.
 Admins can view real-time status of all connected campuses.
