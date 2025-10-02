#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include <string.h>


#define MAX_CUSTOMERS 200000       
#define NUM_NODES 3
#define MAX_QUEUE_SIZE 100         
#define PRINT_STATE_DISTRIBUTION 1


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

typedef struct {
    Customer* current_customer;
    double next_completion_time;
    bool busy;
} Server;

typedef struct {
    Customer* customers[MAX_QUEUE_SIZE];
    int front, rear, size;
    int max_capacity;
    int num_servers;        
    Server servers[2];
    double service_min, service_max;
    double routing_probs[4];
    char name[64];
} QueueNode;

typedef struct Event {
    double time;
    int type;            
    int customer_id;
    int server_id;
    struct Event* next;
} Event;

typedef struct {
    Event* head;
} EventList;

Customer customers[MAX_CUSTOMERS];
QueueNode nodes[NUM_NODES];
EventList event_list;

double simulation_time = 0.0;
int customer_count = 0;
double external_arrival_min = 2.0;
double external_arrival_max = 4.0;

int total_customers_served = 0;
double total_system_time = 0.0;
int customers_processed[NUM_NODES] = {0,0,0};
double total_waiting_time[NUM_NODES] = {0.0,0.0,0.0};
int lost_customers[NUM_NODES] = {0,0,0};

static unsigned long RNG_COUNT = 0;
static unsigned long RNG_LIMIT = 100000UL;  
static double FIRST_ARRIVAL_TIME = 2.0;     
static bool SEED_WAS_SET = false;
static bool CONFIG_FROM_YAML = false;

double state_time[NUM_NODES][MAX_QUEUE_SIZE + 1] = {{0}};

static inline double urand01(void);
double generate_uniform(double min, double max);
void schedule_event(double time, int type, int customer_id, int server_id);
Event* get_next_event(void);
void initialize_simulation(void);
void process_external_arrival(void);
void assign_customer_to_server(QueueNode* node, int node_id, Customer* customer);
void process_service_completion(int node_id, int server_id);
void route_customer(Customer* customer, int from_node);
void run_simulation(void);
void print_report_for_pdf(void);
static void trim(char* s);


static inline double urand01(void) {
    RNG_COUNT++;
    return (double)rand() / (double)RAND_MAX;
}

double generate_uniform(double min, double max) {
    return min + urand01() * (max - min);
}

void enqueue_customer(QueueNode* node, Customer* customer) {
    if (node->size < node->max_capacity && node->size < MAX_QUEUE_SIZE) {
        node->customers[node->rear] = customer;
        node->rear = (node->rear + 1) % MAX_QUEUE_SIZE;
        node->size++;
    } else {
        int idx = (int)(node - nodes);
        if (0 <= idx && idx < NUM_NODES) lost_customers[idx]++;
    }
}

Customer* dequeue_customer(QueueNode* node) {
    if (node->size > 0) {
        Customer* c = node->customers[node->front];
        node->front = (node->front + 1) % MAX_QUEUE_SIZE;
        node->size--;
        return c;
    }
    return NULL;
}


void schedule_event(double time, int type, int customer_id, int server_id) {
    Event* e = (Event*)malloc(sizeof(Event));
    e->time = time; e->type = type; e->customer_id = customer_id; e->server_id = server_id; e->next = NULL;

    if (!event_list.head || time < event_list.head->time) {
        e->next = event_list.head;
        event_list.head = e;
    } else {
        Event* cur = event_list.head;
        while (cur->next && cur->next->time <= time) cur = cur->next;
        e->next = cur->next; cur->next = e;
    }
}

Event* get_next_event(void) {
    if (!event_list.head) return NULL;
    Event* e = event_list.head;
    event_list.head = event_list.head->next;
    return e;
}

void assign_customer_to_server(QueueNode* node, int node_id, Customer* customer) {
    int avail = -1;
    for (int i = 0; i < node->num_servers; i++) if (!node->servers[i].busy) { avail = i; break; }

    if (avail != -1) {
        node->servers[avail].busy = true;
        node->servers[avail].current_customer = customer;
        customer->current_node = node_id;
        customer->visits[node_id]++;

        double svc = generate_uniform(node->service_min, node->service_max);
        customer->service_times[node_id] += svc;
        double done = simulation_time + svc;
        node->servers[avail].next_completion_time = done;
        schedule_event(done, node_id + 1, customer->id, avail);
    } else {
        enqueue_customer(node, customer);
        customer->current_node = node_id;
    }
}

void process_external_arrival(void) {
    if (customer_count >= MAX_CUSTOMERS) return;

    customers[customer_count].id = customer_count;
    customers[customer_count].arrival_time = simulation_time;
    customers[customer_count].current_time = simulation_time;
    customers[customer_count].current_node = 0;
    customers[customer_count].total_system_time = 0.0;
    for (int i=0;i<NUM_NODES;i++) {
        customers[customer_count].waiting_times[i]=0.0;
        customers[customer_count].service_times[i]=0.0;
        customers[customer_count].visits[i]=0;
    }

    assign_customer_to_server(&nodes[0], 0, &customers[customer_count]);

    customer_count++;
    if (customer_count < MAX_CUSTOMERS) {
        double next_t = simulation_time + generate_uniform(external_arrival_min, external_arrival_max);
        schedule_event(next_t, 0, customer_count, -1);
    }
}

void process_service_completion(int node_id, int server_id) {
    QueueNode* node = &nodes[node_id];
    Customer* c = node->servers[server_id].current_customer;

    node->servers[server_id].busy = false;
    node->servers[server_id].current_customer = NULL;
    node->servers[server_id].next_completion_time = INFINITY;

    if (c) route_customer(c, node_id);

    if (node->size > 0) {
        Customer* nxt = dequeue_customer(node);
        double wait = simulation_time - nxt->current_time;
        nxt->waiting_times[node_id] += wait;
        total_waiting_time[node_id] += wait;

        node->servers[server_id].busy = true;
        node->servers[server_id].current_customer = nxt;
        nxt->visits[node_id]++;

        double svc = generate_uniform(node->service_min, node->service_max);
        nxt->service_times[node_id] += svc;
        double done = simulation_time + svc;
        node->servers[server_id].next_completion_time = done;
        schedule_event(done, node_id + 1, nxt->id, server_id);
    }
}

void route_customer(Customer* customer, int from_node) {
    double r[NUM_NODES][4] = {
        {0.0, 0.8, 0.2, 0.0},
        {0.3, 0.0, 0.5, 0.2},
        {0.0, 0.7, 0.0, 0.3} 
    };

    double sum = r[from_node][0] + r[from_node][1] + r[from_node][2] + r[from_node][3];
    int destination = 3;
    if (sum > 0) {
        double u = urand01() * sum, cum = 0.0;
        for (int i=0;i<4;i++){ cum += r[from_node][i]; if (u <= cum) { destination = i; break; } }
    }

    customer->current_time = simulation_time;
    customers_processed[from_node]++;

    if (destination == 3) {
        customer->total_system_time = simulation_time - customer->arrival_time;
        total_system_time += customer->total_system_time;
        total_customers_served++;
    } else {
        assign_customer_to_server(&nodes[destination], destination, customer);
    }
}

void print_report_for_pdf(void) {
    printf("===== Relatorio para o pdf=====\n");
    printf("Resultado da Fila 1: G/G/1, chegadas entre 2..4, atendimento entre 1..2:\n");
    printf("  Clientes processados: %d\n", customers_processed[0]);
    printf("  Tempo medio de espera: %.6f\n",
           customers_processed[0] ? total_waiting_time[0] / customers_processed[0] : 0.0);
    printf("  Perdas: %d\n\n", lost_customers[0]);

    printf("Resultado da Fila 2: G/G/2/5, atendimento entre 4..6:\n");
    printf("  Clientes processados: %d\n", customers_processed[1]);
    printf("  Tempo medio de espera: %.6f\n",
           customers_processed[1] ? total_waiting_time[1] / customers_processed[1] : 0.0);
    printf("  Perdas: %d\n\n", lost_customers[1]);

    printf("Resultado da Fila 3: G/G/2/10, atendimento entre 5..15:\n");
    printf("  Clientes processados: %d\n", customers_processed[2]);
    printf("  Tempo medio de espera: %.6f\n",
           customers_processed[2] ? total_waiting_time[2] / customers_processed[2] : 0.0);
    printf("  Perdas: %d\n\n", lost_customers[2]);

    printf("Tempo total de simulacao: %.6f\n", simulation_time);

#if PRINT_STATE_DISTRIBUTION
    for (int i=0;i<NUM_NODES;i++) {
        double tot=0.0; int cap=nodes[i].max_capacity;
        if (cap>MAX_QUEUE_SIZE) cap=MAX_QUEUE_SIZE;
        for (int s=0;s<=cap;s++) tot += state_time[i][s];
        printf("\nDistribuicao de estados - Fila %d (%s):\n", i+1, nodes[i].name);
        printf("Estado;TempoAcumulado;Probabilidade\n");
        for (int s=0;s<=cap;s++){
            double p = (tot>0.0)? state_time[i][s]/tot : 0.0;
            printf("%d;%.6f;%.6f\n", s, state_time[i][s], p);
        }
    }
#endif
}

void initialize_simulation(void) {
    if (!SEED_WAS_SET) srand((unsigned)time(NULL));

    /* Node 1 (G/G/1) */
    strncpy(nodes[0].name, "Node 1 (G/G/1)", sizeof(nodes[0].name)-1);
    nodes[0].front=nodes[0].rear=0; nodes[0].size=0;
    nodes[0].max_capacity = MAX_QUEUE_SIZE;
    nodes[0].num_servers = 1;
    nodes[0].service_min = 1.0; nodes[0].service_max = 2.0;

    /* Node 2 (G/G/2/5) */
    strncpy(nodes[1].name, "Node 2 (G/G/2/5)", sizeof(nodes[1].name)-1);
    nodes[1].front=nodes[1].rear=0; nodes[1].size=0;
    nodes[1].max_capacity = 5;
    nodes[1].num_servers = 2;
    nodes[1].service_min = 4.0; nodes[1].service_max = 6.0;

    /* Node 3 (G/G/2/10) */
    strncpy(nodes[2].name, "Node 3 (G/G/2/10)", sizeof(nodes[2].name)-1);
    nodes[2].front=nodes[2].rear=0; nodes[2].size=0;
    nodes[2].max_capacity = 10;
    nodes[2].num_servers = 2;
    nodes[2].service_min = 5.0; nodes[2].service_max = 15.0;

    for (int i=0;i<NUM_NODES;i++){
        for (int s=0;s<nodes[i].num_servers;s++){
            nodes[i].servers[s].busy=false;
            nodes[i].servers[s].current_customer=NULL;
            nodes[i].servers[s].next_completion_time=INFINITY;
        }
    }

    event_list.head = NULL;
    schedule_event(FIRST_ARRIVAL_TIME, 0, customer_count, -1);
}

void run_simulation(void) {
    initialize_simulation();

    while (RNG_COUNT < RNG_LIMIT) {
        Event* next_event = get_next_event();
        if (!next_event) break;

        double next_t = next_event->time;
        double dt = next_t - simulation_time;
        if (dt > 0) {
            for (int i=0;i<NUM_NODES;i++){
                int q = nodes[i].size;
                if (q < 0) q = 0;
                if (q > nodes[i].max_capacity) q = nodes[i].max_capacity;
                if (q > MAX_QUEUE_SIZE) q = MAX_QUEUE_SIZE;
                state_time[i][q] += dt;
            }
        }

        simulation_time = next_t;

        switch (next_event->type) {
            case 0:  process_external_arrival();                 break;
            case 1:  process_service_completion(0, next_event->server_id); break;
            case 2:  process_service_completion(1, next_event->server_id); break;
            case 3:  process_service_completion(2, next_event->server_id); break;
            default: break;
        }
        free(next_event);
    }

    print_report_for_pdf();
}

static void trim(char* s) {
    size_t n = strlen(s);
    while (n && (s[n-1]=='\n' || s[n-1]=='\r' || s[n-1]==' ' || s[n-1]=='\t')) s[--n]=0;
}

int main(int argc, char** argv) {
    if (argc >= 2) {
        FILE* f = fopen(argv[1], "r");
        if (f) {
            CONFIG_FROM_YAML = true;
            char line[512];
            int node_ix = -1;

            while (fgets(line, sizeof(line), f)) {
                trim(line);
                if (!line[0] || line[0]=='#') continue;

                if (strstr(line, "seed:")) {
                    unsigned s; if (sscanf(line, " seed: %u", &s)==1) { srand(s); SEED_WAS_SET = true; }
                } else if (strstr(line, "rng_limit:")) {
                    unsigned long lim; if (sscanf(line, " rng_limit: %lu", &lim)==1) RNG_LIMIT = lim;
                } else if (strstr(line, "first_arrival_time:")) {
                    double fa; if (sscanf(line, " first_arrival_time: %lf", &fa)==1) FIRST_ARRIVAL_TIME = fa;
                } else if (strstr(line, "external_arrival:")) {
                    double a,b; if (sscanf(line, " external_arrival: [ %lf , %lf ]", &a,&b)==2) {
                        external_arrival_min = a; external_arrival_max = b;
                    }
                }

                else if (strstr(line, "- name:")) {
                    node_ix++;
                    if (node_ix < NUM_NODES) {
                        char nm[63]={0};
                        if (sscanf(line, " - name: \"%62[^\"]\"", nm)==1 ||
                            sscanf(line, " - name: '%62[^']'", nm)==1 ||
                            sscanf(line, " - name: %62[^\n]", nm)==1) {
                            strncpy(nodes[node_ix].name, nm, sizeof(nodes[node_ix].name)-1);
                        }
                    }
                } else if (node_ix >= 0 && node_ix < NUM_NODES) {
                    if (strstr(line, " servers:")) {
                        int s; if (sscanf(line, " servers: %d", &s)==1) {
                            if (s<1) s=1; if (s>2) s=2; nodes[node_ix].num_servers=s;
                        }
                    } else if (strstr(line, " capacity:")) {
                        int c; if (sscanf(line, " capacity: %d", &c)==1) {
                            if (c<0) c=0; if (c>MAX_QUEUE_SIZE) c=MAX_QUEUE_SIZE; nodes[node_ix].max_capacity=c;
                        }
                    } else if (strstr(line, " service:")) {
                        double a,b; if (sscanf(line, " service: [ %lf , %lf ]", &a,&b)==2) {
                            nodes[node_ix].service_min=a; nodes[node_ix].service_max=b;
                        }
                    }
                }
            }
            fclose(f);
        }
    }

    run_simulation();
    return 0;
}
