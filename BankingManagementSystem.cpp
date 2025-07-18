#include <iostream>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <cstdlib>
#include <ctime>
#include <string>

using namespace std;

// Task 1: Resource Management (Define banking resources)
#define MAX_ATMS 2
#define MAX_TELLERS 3
#define MAX_CUSTOMERS 5
#define INITIAL_FUNDS 50000
#define MAX_TRANSACTIONS 20
#define MAX_SECURITY_LOG 30
#define MAX_TOKENS 5
#define BUSINESS_HOURS 10 // Task 10: Simulate 10-second business hours

int available_funds = INITIAL_FUNDS;
int atm_status[MAX_ATMS] = {1, 1}; // 1 = available, 0 = busy
int teller_status[MAX_TELLERS] = {1, 1, 1};
int token_status[MAX_TOKENS] = {1, 1, 1, 1, 1}; // Task 1: Security tokens
int atm_lru[MAX_ATMS] = {0}; // Task 10: LRU for ATMs
int teller_lru[MAX_TELLERS] = {0}; // Task 10: LRU for tellers
int token_lru[MAX_TOKENS] = {0}; // Task 10: LRU for tokens
int transaction_capacity = 10; // Task 1: Dynamic transaction capacity
int customer_transaction_limits[MAX_CUSTOMERS] = {5, 5, 5, 5, 5}; // Task 8: Per-customer limits
bool is_business_open = true; // Task 10: Business hours flag
time_t simulation_start_time;

// Task 4: Memory Management (Global security log)
string security_log[MAX_SECURITY_LOG];
int security_log_count = 0;

// Task 4: Memory Management (Account structure)
struct Account 
{
  int account_id;
  int balance;
  bool is_vip; // Task 5.3: Priority for VIPs
  bool requires_clearance; // Task 2 & 9: Security clearance
  string transaction_history[50];
  int history_count;
};

// Task 4: Global arrays for accounts
Account accounts[MAX_CUSTOMERS];

// Task 5: Transaction structure
struct Transaction 
{
  int tid;
  int account_id;
  int amount;
  int priority; // Task 5.1: Priority levels (0=critical, 1=high, 2=medium, 3=low)
  bool is_processed;
  string type; // e.g., "withdraw", "deposit", "inquiry"
  time_t submission_time; // Task 5.4: Tiebreaker
};

// Task 5.5: Separate transaction queues for each priority level
Transaction transaction_queues[4][MAX_TRANSACTIONS];
int queue_sizes[4] = {0, 0, 0, 0};

// Task 3: Synchronization - Mutexes and semaphore
pthread_mutex_t account_mutex[MAX_CUSTOMERS];
pthread_mutex_t queue_mutex;
pthread_mutex_t resource_mutex;
pthread_mutex_t security_log_mutex;
sem_t token_sem; // Task 3: Semaphore for tokens

// Task 7: Error Handling - Error codes
enum ErrorCode { SUCCESS, INSUFFICIENT_FUNDS, ACCOUNT_LOCKED, SYSTEM_OVERLOAD, FRAUD_SUSPECTED, NETWORK_FAILURE, OUTSIDE_BUSINESS_HOURS, TRANSACTION_LIMIT_EXCEEDED };

// Task 2: Process and Thread Management - Thread control flag
bool keep_running = true;

// Task 4: Memory Management - Log security events
void log_security(const char* event) 
{
  pthread_mutex_lock(&security_log_mutex);
  
  if (security_log_count < MAX_SECURITY_LOG) 
  {
    security_log[security_log_count] = "Time:" + to_string(time(NULL)) + " " + event;
    security_log_count++;
  }
  
  pthread_mutex_unlock(&security_log_mutex);
}

// Task 2: Process and Thread Management (Process a transaction)
void* process_transaction(void* arg) 
{
  Transaction* t = (Transaction*)arg;
  cout << "Processing TID: " << t->tid << ", Account: " << t->account_id << ", Type: " << t->type << ", Priority: " << t->priority << endl;

  // Task 3: Synchronization - Lock account
  pthread_mutex_lock(&account_mutex[t->account_id]);
  
  Account* acc = &accounts[t->account_id];
  ErrorCode status = SUCCESS; // error handling

  // Task 10: Realistic System Behavior - Check business hours
  if (!is_business_open) 
  {
    status = OUTSIDE_BUSINESS_HOURS;
    acc->transaction_history[acc->history_count] = "Transaction ID:" + to_string(t->tid) + " " + t->type + " " + to_string(t->amount) + " Status:Outside Business Hours";
    acc->history_count++;
    cout << "Transaction ID: " << t->tid << " failed: Outside business hours" << endl;
    
    pthread_mutex_unlock(&account_mutex[t->account_id]);
    
    if (t->priority <= 1) 
    {
      sem_post(&token_sem); // Task 3: Release token
    }
    
    pthread_exit(NULL);
  }

  // Task 2 & 9: Security clearance check
  if (acc->requires_clearance && t->priority > 1) 
  {
    t->priority = 1; // Task 5.7: Escalate to high priority
    log_security("Clearance required");
    cout << "Transaction ID: " << t->tid << " escalated for clearance" << endl;
  }

  // Task 7: Error Handling - Validate transaction
  if (acc->balance < 0) 
  {
    status = ACCOUNT_LOCKED;
  } 
  
  else if (t->type == "withdraw" && acc->balance < t->amount) 
  {
    status = INSUFFICIENT_FUNDS;
  } 
  
  else if (transaction_capacity <= 0) 
  {
    status = SYSTEM_OVERLOAD;
  } 
  
  else if (t->amount > 500) 
  {
    status = FRAUD_SUSPECTED;
    t->priority = 0; // Task 5.7: Escalate to critical
    log_security("Fraud suspected");
    cout << "Transaction ID: " << t->tid << " flagged for fraud" << endl;
  } 
  
  else if (rand() % 100 == 1) 
  { 
    // Task 7: 1% chance of network failure
    status = NETWORK_FAILURE;
    log_security("Network failure");
    cout << "Transaction ID: " << t->tid << " failed: Network issue" << endl;
  }

  if (status == SUCCESS) 
  {
    // Task 5.9: System Capacity - Update funds
    pthread_mutex_lock(&resource_mutex);
    
    if (t->type == "withdraw") 
    {
      acc->balance -= t->amount;
      available_funds -= t->amount;
    } 
    
    else if (t->type == "deposit") 
    {
      acc->balance += t->amount;
      available_funds += t->amount;
    }
    
    customer_transaction_limits[t->account_id]--; // Task 8: Update transaction limit
    transaction_capacity--;
    
    pthread_mutex_unlock(&resource_mutex);

    // Task 4: Memory Management - Log transaction
    acc->transaction_history[acc->history_count] = "Transaction ID:" + to_string(t->tid) + " " + t->type + " " + to_string(t->amount) + " Status:Success";
    acc->history_count++;

    // Task 10: Realistic System Behavior - Processing delay
    usleep(50000); 
    cout << "Transaction ID: " << t->tid << " completed. Account " << t->account_id << " balance: " << acc->balance << endl;
  } 
  
  else 
  {
    // Task 4: Memory Management - Log failed transaction
    acc->transaction_history[acc->history_count] = "Transaction ID:" + to_string(t->tid) + " " + t->type + " " + to_string(t->amount) + " Status:" + to_string(status);
    acc->history_count++;
    
    cout << "Transaction ID: " << t->tid << " failed. Error: " << status << endl;
  }

  pthread_mutex_unlock(&account_mutex[t->account_id]);
  
  if (t->priority <= 1) 
  {
    sem_post(&token_sem); // Task 3: Release token
  }
  pthread_exit(NULL);
}

// Task 5.6: Priority Scheduling - Scheduler thread
void* scheduler(void* arg) 
{
  while (keep_running) 
  {
    pthread_mutex_lock(&queue_mutex);
    bool processed = false;
    
    // Task 5.1 & 5.6: Process highest priority queue first
    for (int priority = 0; priority < 4; priority++) 
    {
      if (queue_sizes[priority] > 0) 
      {
        // Task 5.4: Tiebreaker - Earliest submission time
        int earliest_index = 0;
        time_t earliest_time = transaction_queues[priority][0].submission_time;
        
        for (int i = 1; i < queue_sizes[priority]; i++) 
        {
          if (transaction_queues[priority][i].submission_time < earliest_time && !transaction_queues[priority][i].is_processed) 
          {
            earliest_index = i;
            earliest_time = transaction_queues[priority][i].submission_time;
          }
        }
        
        // Task 5.6: Selecting Transactions:
        Transaction* t = &transaction_queues[priority][earliest_index];
        if (!t->is_processed) 
        {
          t->is_processed = true;
          pthread_t thread;
          pthread_create(&thread, NULL, process_transaction, (void*)t);
          pthread_join(thread, NULL); // Task 2: Join instead of detach
          processed = true;
        }
        
        // Remove transaction from queue
        for (int i = earliest_index; i < queue_sizes[priority] - 1; i++) 
        {
          transaction_queues[priority][i] = transaction_queues[priority][i + 1];
        }
        
        queue_sizes[priority]--;
        break;
      }
    }
    
    pthread_mutex_unlock(&queue_mutex);
    if (!processed) 
    {
      usleep(50000); 
    }
  }
  
  return NULL;
}

// Task 8: Dynamic Resource Generation (Replenish resources)
void* resource_replenishment(void* arg) 
{
  while (keep_running) 
  {
    pthread_mutex_lock(&resource_mutex);
    
    // Task 1: Simulate fund transfer
    int transfer_amount = 1000 + (rand() % 4000);
    available_funds = available_funds + transfer_amount;
    transaction_capacity = 10;
    
    for (int i = 0; i < MAX_ATMS; i++) 
    {
      atm_status[i] = 1;
    }
    
    for (int i = 0; i < MAX_TELLERS; i++) 
    {
      teller_status[i] = 1;
    }
    
    for (int i = 0; i < MAX_TOKENS; i++) 
    {
      token_status[i] = 1; // Task 1: Reset tokens
    }
    
    for (int i = 0; i < MAX_CUSTOMERS; i++) 
    {
      customer_transaction_limits[i] = 5 + (rand() % 3); // Task 8: Adaptive limits
    }
    
    log_security(("Fund transfer of " + to_string(transfer_amount) + " received").c_str());
    cout << "Resources replenished. Funds: " << available_funds << ", Capacity: " << transaction_capacity << endl;
    
    pthread_mutex_unlock(&resource_mutex);
    
    sleep(4); // Task 1: Time-based cycle
  }
  return NULL;
}

// Task 6 & 5.8: I/O Management - Simulate external events
void* io_simulator(void* arg) 
{
  while (keep_running) 
  {
    // Task 6: Simulate network issues (5% chance)
    if (rand() % 100 < 5) 
    {
      log_security("Network issue");
      cout << "Network issue detected! Delaying..." << endl;
      
      sleep(1);
      
      // Task 5.7 & 5.11: Escalate withdrawals to critical
      pthread_mutex_lock(&queue_mutex);
      
      for (int i = 0; i < queue_sizes[2]; i++) 
      {
        if (transaction_queues[2][i].type == "withdraw") 
        {
          transaction_queues[2][i].priority = 0;
          transaction_queues[0][queue_sizes[0]] = transaction_queues[2][i];
          queue_sizes[0]++;
          
          for (int j = i; j < queue_sizes[2] - 1; j++) 
          {
            transaction_queues[2][j] = transaction_queues[2][j + 1];
          }
          
          queue_sizes[2]--;
          i--;
        }
      }
      
      pthread_mutex_unlock(&queue_mutex);
    }
    
    // Task 6 & 10: Simulate maintenance
    if (time(NULL) % 8 == 0) 
    {
      log_security("Maintenance window");
      cout << "Maintenance in progress..." << endl;
      
      sleep(1);
    }
    
    // Task 6: Simulate regulatory update
    if (rand() % 100 < 3) 
    {
      log_security("Regulatory update");
      cout << "Regulatory update: Requiring additional verification for large transactions" << endl;
      
      pthread_mutex_lock(&queue_mutex);
      for (int i = 0; i < queue_sizes[1]; i++) 
      {
        if (transaction_queues[1][i].amount > 300) 
        {
          transaction_queues[1][i].priority = 0; // Escalate to critical
          transaction_queues[0][queue_sizes[0]] = transaction_queues[1][i];
          queue_sizes[0]++;
          
          for (int j = i; j < queue_sizes[1] - 1; j++) 
          {
            transaction_queues[1][j] = transaction_queues[1][j + 1];
          }
          
          queue_sizes[1]--;
          i--;
        }
      }
      
      pthread_mutex_unlock(&queue_mutex);
    }
    
    sleep(3);
  }
  
  return NULL;
}

// Task 9 & 5.10: Dynamic Task Assignment - Assign transaction to resources
void assign_transaction(Transaction t) 
{
  pthread_mutex_lock(&resource_mutex);
  
  if (!is_business_open) 
  {
    cout << "Transaction ID: " << t.tid << " rejected: Outside business hours" << endl;
    log_security("Transaction rejected: Outside business hours");
    
    pthread_mutex_unlock(&resource_mutex);
    return;
  }
  
  // Task 8: Check customer transaction limit
  if (customer_transaction_limits[t.account_id] <= 0) 
  {
    cout << "Transaction ID: " << t.tid << " rejected: Transaction limit exceeded" << endl;
    log_security("Customer transaction limit reached");
    
    pthread_mutex_unlock(&resource_mutex);
    return;
  }
  
  bool assigned = false;
  int resource_index = -1;
  int token_index = -1;

  // Task 5.3: Assign priority based on customer and transaction
  if (accounts[t.account_id].is_vip) 
  {
    t.priority = min(t.priority, 1); // Task 5.2: High priority for VIPs
  }
  
  if (t.amount > 300) 
  {
    t.priority = min(t.priority, 1); // Task 5.2: High priority for large amounts
  }
  
  if (t.type == "inquiry") 
  {
    t.priority = 3; // Task 5.2: Low priority for inquiries
  }

  // Task 1 & 3: Allocate token for secure transactions
  if (t.priority <= 1) 
  {
    if (sem_trywait(&token_sem) == 0) 
    {
      int min_lru_token = token_lru[0], min_token_index = 0;
      for (int i = 0; i < MAX_TOKENS; i++) 
      {
        if (token_status[i] && token_lru[i] < min_lru_token) 
        {
          min_lru_token = token_lru[i];
          min_token_index = i;
        }
      }
      
      token_status[min_token_index] = 0;
      token_lru[min_token_index] = time(NULL); // Task 10: Update LRU
      
      token_index = min_token_index;
    } 
    
    else 
    {
      log_security("No tokens available");
      cout << "Transaction ID: " << t.tid << " rejected: No tokens" << endl;
      pthread_mutex_unlock(&resource_mutex);
      return;
    }
  }

  // Task 10: LRU - Allocate ATM or teller
  if (t.priority <= 1) 
  {
    int min_lru = teller_lru[0], min_index = 0;
    for (int i = 0; i < MAX_TELLERS; i++) 
    {
      if (teller_status[i] && teller_lru[i] < min_lru) 
      {
        min_lru = teller_lru[i];
        min_index = i;
      }
    }
    
    if (teller_status[min_index]) 
    {
      teller_status[min_index] = 0;
      teller_lru[min_index] = time(NULL); // Task 10: Update LRU
      assigned = true;
      resource_index = min_index;
    }
  } 
  
  else 
  {
    int min_lru = atm_lru[0], min_index = 0;
    for (int i = 0; i < MAX_ATMS; i++) 
    {
      if (atm_status[i] && atm_lru[i] < min_lru) 
      {
        min_lru = atm_lru[i];
        min_index = i;
      }
    }
    if (atm_status[min_index]) 
    {
      atm_status[min_index] = 0;
      atm_lru[min_index] = time(NULL); // Task 10: Update LRU
      assigned = true;
      resource_index = min_index;
    }
  }

  if (assigned) 
  {
    pthread_mutex_lock(&queue_mutex);
    int q = t.priority;
    transaction_queues[q][queue_sizes[q]] = t;
    queue_sizes[q]++;
    pthread_mutex_unlock(&queue_mutex);
    cout << "Transaction ID: " << t.tid << " assigned to resource " << resource_index << " with token " << (token_index != -1 ? to_string(token_index) : "none") << endl;
  } 
  
  else 
  {
    log_security("No resources available");
    cout << "Transaction ID: " << t.tid << " rejected: No resources" << endl;
    if (token_index != -1) 
    {
      sem_post(&token_sem); // Task 3: Release token
    }
  }
  
  pthread_mutex_unlock(&resource_mutex);
}

int main() 
{
  // Task 10: Realistic System Behavior:
  srand(time(NULL));
  simulation_start_time = time(NULL);

  // Task 4: Memory Management - Initialize accounts
  for (int i = 0; i < MAX_CUSTOMERS; i++) 
  {
    accounts[i].account_id = i;
    accounts[i].balance = 500 + (rand() % 2000);
    accounts[i].is_vip = (rand() % 3 == 0); // Task 5.3: Some VIPs
    accounts[i].requires_clearance = (rand() % 2 == 0); // Task 2 & 9
    accounts[i].history_count = 0;
  }

  // Task 3: Synchronization - Initialize mutexes and semaphore
  for (int i = 0; i < MAX_CUSTOMERS; i++) 
  {
    pthread_mutex_init(&account_mutex[i], NULL);
  }
  
  pthread_mutex_init(&queue_mutex, NULL);
  pthread_mutex_init(&resource_mutex, NULL);
  pthread_mutex_init(&security_log_mutex, NULL);
  sem_init(&token_sem, 0, MAX_TOKENS); // Task 3: Initialize semaphore

  // Task 2: Process and Thread Management - Start background threads
  pthread_t scheduler_thread, replenisher_thread, io_thread;
  pthread_create(&scheduler_thread, NULL, scheduler, NULL);
  pthread_create(&replenisher_thread, NULL, resource_replenishment, NULL);
  pthread_create(&io_thread, NULL, io_simulator, NULL);

  // Task 5.2 & 10: Simulate transactions with peak times
  for (int i = 0; i < 10; i++) 
  {
    Transaction t;
    t.tid = i;
    t.account_id = rand() % MAX_CUSTOMERS;
    t.amount = 50 + (rand() % 500);
    t.priority = rand() % 4; // Task 5.1: Random initial priority
    t.is_processed = false;
    int type = rand() % 3;
    t.type = (type == 0 ? "withdraw" : type == 1 ? "deposit" : "inquiry");
    t.submission_time = time(NULL); // Task 5.4: Submission time
    assign_transaction(t);
    
    if (i % 4 == 0 && i >= 4) 
    {
      // Task 10: Simulate peak time
      cout << "Peak time: High volume!" << endl;
      usleep(50000);
    } 
    
    else 
    {
      usleep(100000);
    }
  }

  // Task 10: Simulate business hours
  while (time(NULL) - simulation_start_time < BUSINESS_HOURS) 
  {
    usleep(100000);
  }
  
  is_business_open = false; // Task 10: Close business

  // Task 2: Process and Thread Management - Signal threads to exit
  keep_running = false;
  pthread_join(scheduler_thread, NULL);
  pthread_join(replenisher_thread, NULL);
  pthread_join(io_thread, NULL);

  // Task 4: Memory Management - Print transaction histories and logs
  for (int i = 0; i < MAX_CUSTOMERS; i++) 
  {
    if (accounts[i].history_count > 0) 
    {
      cout << "Account " << i << " History:" << endl;
      for (int j = 0; j < accounts[i].history_count; j++) 
      {
        cout << accounts[i].transaction_history[j] << endl;
      }
    }
  }
  
  cout << "Security Log:" << endl;
  
  for (int i = 0; i < security_log_count; i++) 
  {
    cout << security_log[i] << endl;
  }

  // Task 3: Synchronization - Clean up mutexes and semaphore
  for (int i = 0; i < MAX_CUSTOMERS; i++) 
  {
    pthread_mutex_destroy(&account_mutex[i]);
  }
  
  pthread_mutex_destroy(&queue_mutex);
  pthread_mutex_destroy(&resource_mutex);
  pthread_mutex_destroy(&security_log_mutex);
  sem_destroy(&token_sem);

  cout << "Simulation ended." << endl;
  return 0;
}
