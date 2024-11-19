#include "fish.h"
#include "fishnode.h"
#include <assert.h>
#include <signal.h>
#include <string.h>

#define DEBUG
#define ENTRY_TTL_SECONDS 120 // 2 minutes

static int noprompt = 0;

void sigint_handler(int sig)
{
   if (SIGINT == sig)
	   fish_main_exit();
}

static int print_route(void *callback_data __attribute__((unused)),
      fnaddr_t dest, int prefix_len __attribute((unused)),
      fnaddr_t net_hop __attribute((unused)),
      int metric __attribute((unused)),
      void *entry_data __attribute__((unused))) {
   printf("%s\n", fn_ntoa(dest));
   return 0;
}

static void keyboard_callback(char *line)
{
   if (0 == strcasecmp("show neighbors", line))
      fish_print_neighbor_table();
   else if (0 == strcasecmp("show arp", line))
      fish_print_arp_table();
   else if (0 == strcasecmp("show route", line)) {
      fish_print_forwarding_table();
      fish_fwd.iterate_entries(&print_route, NULL, FISH_FWD_TYPE_BROADCAST);
   }
   else if (0 == strcasecmp("show dv", line))
      fish_print_dv_state();
   else if (0 == strcasecmp("quit", line) || 0 == strcasecmp("exit", line))
      fish_main_exit();
   else if (0 == strcasecmp("show topo", line))
      fish_print_lsa_topo();
   else if (0 == strcasecmp("help", line) || 0 == strcasecmp("?", line)) {
      printf("Available commands are:\n"
             "    exit                         Quit the fishnode\n"
             "    help                         Display this message\n"
             "    quit                         Quit the fishnode\n"
             "    show arp                     Display the ARP table\n"
             "    show dv                      Display the dv routing state\n"
             "    show neighbors               Display the neighbor table\n"
             "    show route                   Display the forwarding table\n"
             "    show topo                    Display the link-state routing\n"
             "                                 algorithm's view of the network\n"
             "                                 topology\n"
             "    ?                            Display this message\n"
            );
   }
   else if (line[0] != 0)
      printf("Type 'help' or '?' for a list of available commands.  "
             "Unknown command: %s\n", line);

   if (!noprompt)
      printf("> ");

   fflush(stdout);
}

// Prototypes for program 2.  Taken directly from fish.h header file
#ifdef L2_IMPL
int my_fish_l2_send(void *l3frame, fnaddr_t next_hop, int len, uint8_t l2_proto)
{
   return 0;
}

int my_fishnode_l2_receive(void *l2frame)
{
   return 0;
}

void my_arp_received(void *l2frame)
{
}

void my_send_arp_request(fnaddr_t l3addr)
{
}

void my_add_arp_entry(fn_l2addr_t l2addr, fnaddr_t addr, int timeout)
{
}

void my_resolve_fnaddr(fnaddr_t addr, arp_resolution_cb cb, void *param)
{
}
#endif

#ifdef L3_IMPL

// Initialize the hash table
HashEntry* hash_table[HASH_TABLE_SIZE];

// Hash function to combine src and packet_id
unsigned int hash_function(fnaddr_t src, uint32_t packet_id) {
   return (src ^ packet_id) % HASH_TABLE_SIZE;
}

// Function to insert a new entry into the hash table 
void insert_entry(fnaddr_t src, uint16_t packet_id) {
   unsigned int index = hash_function(src, packet_id);

   // Allocate memory for the new entry
   HashEntry *new_entry = (HashEntry *)malloc(sizeof(HashEntry));

   new_entry->src = src;
   new_entry->packet_id = packet_id;
   new_entry->timestamp = time(NULL);
   new_entry->next = NULL;

   // Insert into the hash table (handle collisions with chaining)
   if (hash_table[index] == NULL) {
      hash_table[index] = new_entry; // No collision
   } else {
      // Check for existing entry with the same src and packet_id
      HashEntry *current = hash_table[index];
      HashEntry *previous = NULL;

      while (current != NULL) {
         if (current->src == src && current->packet_id == packet_id) {
               // Replace existing entry
               new_entry->next = current->next;
               if (previous) {
                  previous->next = new_entry;
               } else {
                  hash_table[index] = new_entry;
               }
               free(current);
               return;
         }
         previous = current;
         current = current->next;
      }

      // Append to the beginning of the chain
      new_entry->next = hash_table[index];
      hash_table[index] = new_entry;
   }
}

// Function to retrieve the timestamp for a given src and packet_id
time_t get_timestamp(fnaddr_t src, uint16_t packet_id) {
   unsigned int index = hash_function(src, packet_id);

   HashEntry *current = hash_table[index];
   while (current != NULL) {
      if (current->src == src && current->packet_id == packet_id) {
         time_t current_time = time(NULL);
         if (current_time - current->timestamp > ENTRY_TTL_SECONDS) {
               // Entry has expired
               return -1;
         }
         return current->timestamp;
      }
      current = current->next;
   }

   return -1; // Entry not found
}


int my_fishnode_l3_receive(void *l3frame, int len, uint8_t protocol) {
   struct L3_hdr *header = (struct L3_hdr *)l3frame;
   fnaddr_t dest_addr = header->dst;
   fnaddr_t node_addr = fish_getaddress();

   if (dest_addr == node_addr) {
      void *l4frame = (uint8_t *)l3frame + sizeof(struct L3_hdr);
      int l4len = len - sizeof(struct L3_hdr);
      fish_l4.fish_l4_receive(l4frame, l4len, header->protocol, header->src);
   } 
   else if (dest_addr == ALL_NEIGHBORS) {
      time_t timestamp = get_timestamp(header->src, header->packet_id);

      if (timestamp != -1) {
         insert_entry(header->src, header->packet_id);
         if (header->ttl > 1) {
            header->ttl--;
            void *l4frame = (uint8_t *)l3frame + sizeof(struct L3_hdr);
            int l4len = len - sizeof(struct L3_hdr);
            fish_l4.fish_l4_receive(l4frame, l4len, header->protocol, header->src);
            fish_l3.fish_l3_forward(l3frame, len);
         }
      }
   }
   else {
      if (header->ttl > 1) {
            header->ttl--;
            fish_l3.fish_l3_forward(l3frame, len);
      }
   }
   return 0;
}

int my_fish_l3_send(void *l4frame, int len, fnaddr_t dst_addr, uint8_t proto, uint8_t ttl)
{
   // Set TTL to maximum if the provided TTL is 0 or greater than the maximum
   if (ttl == 0 || ttl > MAX_TTL) {
      ttl = MAX_TTL;
   }

   // Allocate memory for the L3 frame, which includes the header and the L4 frame
   int l3_header_len = sizeof(struct L3_hdr); // Assuming a defined L3 header struct
   void *l3frame = malloc(l3_header_len + len);

   // Initialize the L3 header (Assuming a struct l3_header is defined in fish.h)
   struct L3_hdr *header = (struct L3_hdr *)l3frame;
   header->dst = dst_addr;    // Destination L3 address
   header->src = fish_getaddress();
   header->protocol = proto;     // Protocol type for the L4 frame
   header->ttl = ttl;         // Set TTL

   // Copy the L4 frame data into the L3 frame right after the header
   memcpy((uint8_t *)l3frame + l3_header_len, l4frame, len);

   // Forward the L3 frame using the fish_l3_forward function
   fish_l3.fish_l3_forward(l3frame, l3_header_len + len);

   // Free the allocated L3 frame memory after forwarding
   free(l3frame);

   return 0; // Return 0 as the return value is ignored
}

int my_fish_l3_forward(void *l3frame, int len)
{
   struct L3_hdr *header = (struct L3_hdr *)l3frame;
   fnaddr_t dest_addr = header->dst;
   fnaddr_t my_addr = fish_getaddress();

   if (header->ttl == 0 && dest_addr != my_addr) {
      fish_fcmp.send_fcmp_response(l3frame, len, 1);
      return 0;
   }

   fnaddr_t next_hop = fish_fwd.longest_prefix_match(dest_addr);

   if(dest_addr == ALL_NEIGHBORS) {
      next_hop = ALL_NEIGHBORS;
   }

   if(dest_addr == my_addr) {
      next_hop = my_addr;
   }

   if (next_hop == 0) {
      fish_fcmp.send_fcmp_response(l3frame, len, 2);
      return 0;
   }

   fish_l2.fish_l2_send(l3frame, next_hop, len, 1);

   return 0;
}

// Callback to broadcast DV advertisement
// void my_timed_event(void*)
// {
// }

// Full functionality
void* my_add_fwtable_entry(fnaddr_t dst, int prefix_length, fnaddr_t next_hop,
                   int metric, char type, void *user_data)
{
   return NULL;
}

void* my_remove_fwtable_entry(void *route_key)
{
   return NULL;
}

int my_update_fwtable_metric(void *route_key, int new_metric)
{
   return 0;
}

fnaddr_t my_longest_prefix_match(fnaddr_t addr)
{
   return 0;
}
#endif

int main(int argc, char **argv)
{
	struct sigaction sa;
   int arg_offset = 1;
   fish_enable_dvrouting_builtin(1);

   /* Verify and parse the command line parameters */
	if (argc != 2 && argc != 3 && argc != 4)
	{
		printf("Usage: %s [-noprompt] <fishhead address> [<fn address>]\n", argv[0]);
		return 1;
	}

   if (0 == strcasecmp(argv[arg_offset], "-noprompt")) {
      noprompt = 1;
      arg_offset++;
   }

   /* Install the signal handler */
	sa.sa_handler = sigint_handler;
	sigfillset(&sa.sa_mask);
	sa.sa_flags = 0;
	if (-1 == sigaction(SIGINT, &sa, NULL))
	{
		perror("Couldn't set signal handler for SIGINT");
		return 2;
	}

   /* Set up debugging output */
#ifdef DEBUG
	fish_setdebuglevel(FISH_DEBUG_INTERNAL);
	// fish_setdebuglevel(FISH_DEBUG_ALL);
#else
	fish_setdebuglevel(FISH_DEBUG_NONE);
#endif
	fish_setdebugfile(stdout);

   /* Join the fishnet */
	if (argc-arg_offset == 1)
		fish_joinnetwork(argv[arg_offset]);
	else
		fish_joinnetwork_addr(argv[arg_offset], fn_aton(argv[arg_offset+1]));

   /* Install the command line parsing callback */
   fish_keybhook(keyboard_callback);
   if (!noprompt)
      printf("> ");
   fflush(stdout);

#ifdef L2_IMPL
   // Examples of overriding function pointers for program 2 base functionality
   fish_l2.fishnode_l2_receive = &my_fishnode_l2_receive;
   fish_l2.fish_l2_send = &my_fish_l2_send;
   fish_arp.arp_received = &my_arp_received;
   fish_arp.send_arp_request = &my_send_arp_request;
   // Full functionality functions
   fish_arp.add_arp_entry = &my_add_arp_entry;
   fish_arp.resolve_fnaddr = &my_resolve_fnaddr;
#endif

#ifdef L3_IMPL
   fish_l3.fishnode_l3_receive = &my_fishnode_l3_receive;
   fish_l3.fish_l3_send = &my_fish_l3_send;
   fish_l3.fish_l3_forward = &my_fish_l3_forward;
   // Set up a callback to broadcast DV advertisement
   // fish_scheduleevent(0, &my_timed_event, NULL);
   // Full functionality
   // fish_fwd.add_fwtable_entry = &my_add_fwtable_entry;
   // fish_fwd.remove_fwtable_entry = &my_remove_fwtable_entry;
   // fish_fwd.update_fwtable_metric = &my_update_fwtable_metric;
   // fish_fwd.longest_prefix_match = &my_longest_prefix_match;
#endif

#if 1
   /* Enable the built-in neighbor protocol implementation.  This will discover
    * one-hop routes in your fishnet.  The link-state routing protocol requires
    * the neighbor protocol to be working, whereas it is redundant with DV.
    * Running them both doesn't break the fishnode, but will cause extra routing
    * overhead */
   fish_enable_neighbor_builtin( 0
         | NEIGHBOR_USE_LIBFISH_NEIGHBOR_DOWN
      );
#endif

   /* Enable the link-state routing protocol.  This requires the neighbor
    * protocol to be enabled. */
   // fish_enable_lsarouting_builtin(0);

#if 0
   /* Full-featured DV routing.  I suggest NOT using this until you have some
    * reasonable expectation that your code works.  This generates a lot of
    * routing traffic in fishnet */

   fish_enable_dvrouting_builtin( 0
         | DVROUTING_WITHDRAW_ROUTES
         | DVROUTING_TRIGGERED_UPDATES
         | RVROUTING_USE_LIBFISH_NEIGHBOR_DOWN
         | DVROUTING_SPLIT_HOR_POISON_REV
         | DVROUTING_KEEP_ROUTE_HISTORY
    );
#endif

   /* Execute the libfish event loop */
	fish_main();

   /* Clean up and exit */
   if (!noprompt)
      printf("\n");
   fish_keybhook(NULL);

	printf("Fishnode exiting cleanly.\n");

   fishnet_cleanup();

   // Cleanup your data structures here

	return 0;
}
