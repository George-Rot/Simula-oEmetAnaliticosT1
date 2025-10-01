#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>

#define MAX_CUSTOMERS 1000
#define NUM_NODES 3
#define MAX_QUEUE_SIZE 100

// Customer structure
typedef struct {
    int id;
    double arrival_time;
    double current_time;
    int current_node;
    double total_system_time;
    double waiting_times[NUM_NODES];
    double service_times[NUM_NODES];
    int visits[NUM_NODES];
} Customer;

// Server structure
typedef struct {
    Customer* current_customer;
    double next_completion_time;
    bool busy;
} Server;

// Queue/Node structure
typedef struct {
    Customer* customers[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int size;
    int max_capacity;
    int num_servers;
    Server servers[2];  // Max 2 servers per node
    double service_min;
    double service_max;
    double routing_probs[4];  // [to_node1, to_node2, to_node3, exit]
    char name[20];
} QueueNode;

// Event structure
typedef struct Event {
    double time;
    int type;  // 0: external_arrival, 1: service_completion_node1, 2: service_completion_node2, 3: service_completion_node3
    int customer_id;
    int server_id;
    struct Event* next;
} Event;

// Event list
typedef struct {
    Event* head;
} EventList;

// Global variables
Customer customers[MAX_CUSTOMERS];
QueueNode nodes[NUM_NODES];
EventList event_list;
double simulation_time = 0.0;
double total_simulation_time = 100.0;
int customer_count = 0;
double external_arrival_min = 2.0;
double external_arrival_max = 4.0;

// Statistics
int total_customers_served = 0;
double total_system_time = 0.0;
int customers_processed[NUM_NODES] = {0, 0, 0};
double total_waiting_time[NUM_NODES] = {0.0, 0.0, 0.0};

// Function prototypes
void initialize_simulation();
double generate_uniform(double min, double max);
void enqueue_customer(QueueNode* node, Customer* customer);
Customer* dequeue_customer(QueueNode* node);
bool is_queue_empty(QueueNode* node);
void schedule_event(double time, int type, int customer_id, int server_id);
Event* get_next_event();
void process_external_arrival();
void process_service_completion(int node_id, int server_id);
void assign_customer_to_server(QueueNode* node, int node_id);
void route_customer(Customer* customer, int from_node);
void print_statistics();
void run_simulation();

// Initialize the simulation
void initialize_simulation() {
    srand(time(NULL));
    
    // Initialize Node 1 (G/G/1 with 1-2 min service)
    strcpy(nodes[0].name, "Node 1 (G/G/1)");
    nodes[0].front = 0;
    nodes[0].rear = 0;
    nodes[0].size = 0;
    nodes[0].max_capacity = MAX_QUEUE_SIZE;
    nodes[0].num_servers = 1;
    nodes[0].service_min = 1.0;
    nodes[0].service_max = 2.0;
    nodes[0].routing_probs[0] = 0.0;  // to node 1
    nodes[0].routing_probs[1] = 0.8;  // to node 2 (top)
    nodes[0].routing_probs[2] = 0.2;  // to node 3 (bottom)
    nodes[0].routing_probs[3] = 0.0;  // exit
    for (int i = 0; i < nodes[0].num_servers; i++) {
        nodes[0].servers[i].busy = false;
        nodes[0].servers[i].current_customer = NULL;
        nodes[0].servers[i].next_completion_time = INFINITY;
    }
    
    // Initialize Node 2 (G/G/2/5 with 4-6 min service)
    strcpy(nodes[1].name, "Node 2 (G/G/2/5)");
    nodes[1].front = 0;
    nodes[1].rear = 0;
    nodes[1].size = 0;
    nodes[1].max_capacity = 5;
    nodes[1].num_servers = 2;
    nodes[1].service_min = 4.0;
    nodes[1].service_max = 6.0;
    nodes[1].routing_probs[0] = 0.3;  // back to node 1
    nodes[1].routing_probs[1] = 0.0;  // to node 2
    nodes[1].routing_probs[2] = 0.5;  // to node 3
    nodes[1].routing_probs[3] = 0.2;  // exit
    for (int i = 0; i < nodes[1].num_servers; i++) {
        nodes[1].servers[i].busy = false;
        nodes[1].servers[i].current_customer = NULL;
        nodes[1].servers[i].next_completion_time = INFINITY;
    }
    
    // Initialize Node 3 (G/G/2/10 with 5-15 min service)
    strcpy(nodes[2].name, "Node 3 (G/G/2/10)");
    nodes[2].front = 0;
    nodes[2].rear = 0;
    nodes[2].size = 0;
    nodes[2].max_capacity = 10;
    nodes[2].num_servers = 2;
    nodes[2].service_min = 5.0;
    nodes[2].service_max = 15.0;
    nodes[2].routing_probs[0] = 0.0;  // to node 1
    nodes[2].routing_probs[1] = 0.7;  // to node 2
    nodes[2].routing_probs[2] = 0.0;  // to node 3
    nodes[2].routing_probs[3] = 0.3;  // exit
    for (int i = 0; i < nodes[2].num_servers; i++) {
        nodes[2].servers[i].busy = false;
        nodes[2].servers[i].current_customer = NULL;
        nodes[2].servers[i].next_completion_time = INFINITY;
    }
    
    // Initialize event list
    event_list.head = NULL;
    
    // Schedule first external arrival
    double first_arrival_time = generate_uniform(external_arrival_min, external_arrival_max);
    schedule_event(first_arrival_time, 0, customer_count, -1);
}

// Generate uniform random variable
double generate_uniform(double min, double max) {
    double u = (double)rand() / RAND_MAX;
    return min + u * (max - min);
}

// Enqueue customer
void enqueue_customer(QueueNode* node, Customer* customer) {
    if (node->size < node->max_capacity) {
        node->customers[node->rear] = customer;
        node->rear = (node->rear + 1) % MAX_QUEUE_SIZE;
        node->size++;
    } else {
        printf("Time %.2f: Customer %d blocked at %s (capacity exceeded)\n", 
               simulation_time, customer->id, node->name);
    }
}

// Dequeue customer
Customer* dequeue_customer(QueueNode* node) {
    if (node->size > 0) {
        Customer* customer = node->customers[node->front];
        node->front = (node->front + 1) % MAX_QUEUE_SIZE;
        node->size--;
        return customer;
    }
    return NULL;
}

// Check if queue is empty
bool is_queue_empty(QueueNode* node) {
    return node->size == 0;
}

// Schedule an event
void schedule_event(double time, int type, int customer_id, int server_id) {
    Event* new_event = (Event*)malloc(sizeof(Event));
    new_event->time = time;
    new_event->type = type;
    new_event->customer_id = customer_id;
    new_event->server_id = server_id;
    new_event->next = NULL;
    
    // Insert in chronological order
    if (event_list.head == NULL || time < event_list.head->time) {
        new_event->next = event_list.head;
        event_list.head = new_event;
    } else {
        Event* current = event_list.head;
        while (current->next != NULL && current->next->time <= time) {
            current = current->next;
        }
        new_event->next = current->next;
        current->next = new_event;
    }
}

// Get next event from event list
Event* get_next_event() {
    if (event_list.head == NULL) return NULL;
    
    Event* next_event = event_list.head;
    event_list.head = event_list.head->next;
    return next_event;
}

// Process external arrival
void process_external_arrival() {
    // Create new customer
    customers[customer_count].id = customer_count;
    customers[customer_count].arrival_time = simulation_time;
    customers[customer_count].current_time = simulation_time;
    customers[customer_count].current_node = 0;
    customers[customer_count].total_system_time = 0.0;
    
    for (int i = 0; i < NUM_NODES; i++) {
        customers[customer_count].waiting_times[i] = 0.0;
        customers[customer_count].service_times[i] = 0.0;
        customers[customer_count].visits[i] = 0;
    }
    
    printf("Time %.2f: Customer %d arrives at system\n", simulation_time, customer_count);
    
    // Send to Node 1
    assign_customer_to_server(&nodes[0], 0);
    
    // Schedule next external arrival
    customer_count++;
    if (customer_count < MAX_CUSTOMERS) {
        double next_arrival_time = simulation_time + generate_uniform(external_arrival_min, external_arrival_max);
        schedule_event(next_arrival_time, 0, customer_count, -1);
    }
}

// Assign customer to server if available, otherwise enqueue
void assign_customer_to_server(QueueNode* node, int node_id) {
    Customer* customer = &customers[customer_count];
    
    // Find available server
    int available_server = -1;
    for (int i = 0; i < node->num_servers; i++) {
        if (!node->servers[i].busy) {
            available_server = i;
            break;
        }
    }
    
    if (available_server != -1) {
        // Server available - start service
        node->servers[available_server].busy = true;
        node->servers[available_server].current_customer = customer;
        customer->current_node = node_id;
        customer->visits[node_id]++;
        
        double service_time = generate_uniform(node->service_min, node->service_max);
        customer->service_times[node_id] += service_time;
        double completion_time = simulation_time + service_time;
        node->servers[available_server].next_completion_time = completion_time;
        
        schedule_event(completion_time, node_id + 1, customer->id, available_server);
        
        printf("Time %.2f: Customer %d starts service at %s (Server %d, service time %.2f)\n", 
               simulation_time, customer->id, node->name, available_server, service_time);
    } else {
        // All servers busy - add to queue
        double wait_start = simulation_time;
        enqueue_customer(node, customer);
        customer->current_node = node_id;
        printf("Time %.2f: Customer %d joins queue at %s (queue size: %d)\n", 
               simulation_time, customer->id, node->name, node->size);
    }
}

// Process service completion
void process_service_completion(int node_id, int server_id) {
    QueueNode* node = &nodes[node_id];
    Customer* departing_customer = node->servers[server_id].current_customer;
    
    printf("Time %.2f: Customer %d completes service at %s (Server %d)\n", 
           simulation_time, departing_customer->id, node->name, server_id);
    
    // Free the server
    node->servers[server_id].busy = false;
    node->servers[server_id].current_customer = NULL;
    node->servers[server_id].next_completion_time = INFINITY;
    
    // Route the customer
    route_customer(departing_customer, node_id);
    
    // Assign next customer from queue to this server
    if (!is_queue_empty(node)) {
        Customer* next_customer = dequeue_customer(node);
        
        // Calculate waiting time
        double waiting_time = simulation_time - next_customer->current_time;
        next_customer->waiting_times[node_id] += waiting_time;
        total_waiting_time[node_id] += waiting_time;
        
        // Start service
        node->servers[server_id].busy = true;
        node->servers[server_id].current_customer = next_customer;
        next_customer->visits[node_id]++;
        
        double service_time = generate_uniform(node->service_min, node->service_max);
        next_customer->service_times[node_id] += service_time;
        double completion_time = simulation_time + service_time;
        node->servers[server_id].next_completion_time = completion_time;
        
        schedule_event(completion_time, node_id + 1, next_customer->id, server_id);
        
        printf("Time %.2f: Customer %d starts service at %s (Server %d, waited %.2f)\n", 
               simulation_time, next_customer->id, node->name, server_id, waiting_time);
    }
}

// Route customer based on probabilities
void route_customer(Customer* customer, int from_node) {
    double rand_val = (double)rand() / RAND_MAX;
    double cumulative_prob = 0.0;
    int destination = -1;
    
    for (int i = 0; i < 4; i++) {
        cumulative_prob += nodes[from_node].routing_probs[i];
        if (rand_val <= cumulative_prob) {
            destination = i;
            break;
        }
    }
    
    customer->current_time = simulation_time;
    customers_processed[from_node]++;
    
    if (destination == 3) {
        // Exit system
        customer->total_system_time = simulation_time - customer->arrival_time;
        total_system_time += customer->total_system_time;
        total_customers_served++;
        
        printf("Time %.2f: Customer %d exits system (total time: %.2f)\n", 
               simulation_time, customer->id, customer->total_system_time);
        
        // Print customer summary
        printf("  Customer %d summary: Node1 visits=%d, Node2 visits=%d, Node3 visits=%d\n",
               customer->id, customer->visits[0], customer->visits[1], customer->visits[2]);
    } else {
        // Route to another node
        printf("Time %.2f: Customer %d routed from %s to %s\n", 
               simulation_time, customer->id, nodes[from_node].name, nodes[destination].name);
        
        assign_customer_to_server(&nodes[destination], destination);
    }
}

// Print simulation statistics
void print_statistics() {
    printf("\n==================================================\n");
    printf("COMPLEX QUEUEING NETWORK SIMULATION RESULTS\n");
    printf("==================================================\n");
    printf("Simulation Time: %.2f time units\n", total_simulation_time);
    printf("External Arrival Rate: %.2f-%.2f min intervals\n", external_arrival_min, external_arrival_max);
    printf("\n");
    
    printf("NETWORK CONFIGURATION:\n");
    printf("------------------------------\n");
    printf("Node 1: G/G/1, Service time: %.1f-%.1f min\n", nodes[0].service_min, nodes[0].service_max);
    printf("  Routing: %.1f to Node2, %.1f to Node3\n", nodes[0].routing_probs[1], nodes[0].routing_probs[2]);
    printf("Node 2: G/G/2/5, Service time: %.1f-%.1f min\n", nodes[1].service_min, nodes[1].service_max);
    printf("  Routing: %.1f to Node1, %.1f to Node3, %.1f exit\n", 
           nodes[1].routing_probs[0], nodes[1].routing_probs[2], nodes[1].routing_probs[3]);
    printf("Node 3: G/G/2/10, Service time: %.1f-%.1f min\n", nodes[2].service_min, nodes[2].service_max);
    printf("  Routing: %.1f to Node2, %.1f exit\n", nodes[2].routing_probs[1], nodes[2].routing_probs[3]);
    printf("\n");
    
    printf("PERFORMANCE METRICS:\n");
    printf("------------------------------\n");
    printf("Total Customers Served: %d\n", total_customers_served);
    
    if (total_customers_served > 0) {
        printf("Average System Time: %.3f time units\n", total_system_time / total_customers_served);
    }
    
    for (int i = 0; i < NUM_NODES; i++) {
        printf("Node %d (%s):\n", i+1, nodes[i].name);
        printf("  Customers processed: %d\n", customers_processed[i]);
        if (customers_processed[i] > 0) {
            printf("  Average waiting time: %.3f time units\n", total_waiting_time[i] / customers_processed[i]);
        }
        printf("  Current queue size: %d\n", nodes[i].size);
        printf("  Server utilization: ");
        for (int j = 0; j < nodes[i].num_servers; j++) {
            printf("Server%d=%s ", j, nodes[i].servers[j].busy ? "Busy" : "Free");
        }
        printf("\n");
    }
}

// Run the simulation
void run_simulation() {
    initialize_simulation();
    
    printf("Starting Complex Queueing Network Simulation...\n");
    printf("Network: Node1(G/G/1) -> Node2(G/G/2/5) <-> Node3(G/G/2/10)\n");
    printf("Simulation will run for %.2f time units\n\n", total_simulation_time);
    
    while (simulation_time < total_simulation_time) {
        Event* next_event = get_next_event();
        if (next_event == NULL) break;
        
        simulation_time = next_event->time;
        
        if (simulation_time > total_simulation_time) {
            free(next_event);
            break;
        }
        
        switch (next_event->type) {
            case 0:  // External arrival
                process_external_arrival();
                break;
            case 1:  // Service completion at Node 1
                process_service_completion(0, next_event->server_id);
                break;
            case 2:  // Service completion at Node 2
                process_service_completion(1, next_event->server_id);
                break;
            case 3:  // Service completion at Node 3
                process_service_completion(2, next_event->server_id);
                break;
        }
        
        free(next_event);
    }
    
    print_statistics();
}

int main() {
    printf("Complex Queueing Network Simulator\n");
    printf("==================================\n\n");
    
    // Allow user to modify parameters
    printf("Enter simulation parameters (or press Enter for defaults):\n");
    
    printf("Total simulation time (default %.1f): ", total_simulation_time);
    char input[100];
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
        total_simulation_time = atof(input);
    }
    
    printf("External arrival interval min (default %.1f): ", external_arrival_min);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
        external_arrival_min = atof(input);
    }
    
    printf("External arrival interval max (default %.1f): ", external_arrival_max);
    if (fgets(input, sizeof(input), stdin) && input[0] != '\n') {
        external_arrival_max = atof(input);
    }
    
    printf("\n");
    
    run_simulation();
    
    return 0;
}