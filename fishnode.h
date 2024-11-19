#ifndef FISHNODE_H
#define FISHNODE_H

#include "fish.h"
#include <signal.h>
#include <stdint.h>
#include <time.h>

// Macro Definitions
#define DEBUG
#define L3_IMPL

// Function Prototypes
void sigint_handler(int sig);

#ifdef L2_IMPL
int my_fish_l2_send(void *l3frame, fnaddr_t next_hop, int len, uint8_t l2_proto);
int my_fishnode_l2_receive(void *l2frame);
void my_arp_received(void *l2frame);
void my_send_arp_request(fnaddr_t l3addr);
void my_add_arp_entry(fn_l2addr_t l2addr, fnaddr_t addr, int timeout);
void my_resolve_fnaddr(fnaddr_t addr, arp_resolution_cb cb, void *param);
#endif

#ifdef L3_IMPL

struct L3_hdr {
    uint8_t ttl;               // Time-to-Live (offset 0)
    uint8_t protocol;          // Protocol (offset 1)
    uint32_t packet_id;        // Packet ID (offset 2-3)
    fnaddr_t src;              // Source L3 Address (offset 4-5, 6-7 for continuation)
    fnaddr_t dst;              // Destination L3 Address (offset 8-11)
}__attribute__((packed));

// Hash Table Definitions for src, packet_id, and timestamp
#define HASH_TABLE_SIZE 1024  // You can adjust the size as needed

typedef struct HashEntry {
    fnaddr_t src;             // Source address
    uint16_t packet_id;       // Packet ID
    time_t timestamp;         // Timestamp of when entry was added
    struct HashEntry *next;   // Pointer to the next entry in case of collision
} HashEntry;

extern HashEntry* hash_table[HASH_TABLE_SIZE];  // Declare the hash table

// Function Prototypes for Hash Table Operations
unsigned int hash_function(fnaddr_t src, uint32_t packet_id);
void insert_entry(fnaddr_t src, uint16_t packet_id);
time_t get_timestamp(fnaddr_t src, uint16_t packet_id);

int my_fishnode_l3_receive(void *l3frame, int len, uint8_t protocol);
int my_fish_l3_send(void *l4frame, int len, fnaddr_t dst_addr, uint8_t proto, uint8_t ttl);
int my_fish_l3_forward(void *l3frame, int len);
// void my_timed_event(void *);
// void* my_add_fwtable_entry(fnaddr_t dst, int prefix_length, fnaddr_t next_hop, int metric, char type, void *user_data);
// void* my_remove_fwtable_entry(void *route_key);
// int my_update_fwtable_metric(void *route_key, int new_metric);
// fnaddr_t my_longest_prefix_match(fnaddr_t addr);
#endif

#endif // FISHNODE_H
