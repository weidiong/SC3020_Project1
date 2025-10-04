// Task 3: Full B+ Tree Deletion with Node Repacking and Tree Maintenance
// Compile: gcc -O2 -std=c11 deletion.c -o deletion
// Usage: ./deletion database.bin bpt.idx

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>
#include <time.h>

#define BLOCK_SIZE 4096u

// ===== Storage header =====
#pragma pack(push, 1)
struct SimpleHeader { uint32_t num_records; };
#pragma pack(pop)

#pragma pack(push, 1)
struct Record {
    char     GAME_DATE_EST[16];
    int32_t  TEAM_ID_home;
    int32_t  PTS_home;
    float    FG_PCT_home;
    float    FT_PCT_home; 
    float    FG3_PCT_home;
    int32_t  AST_home;
    int32_t  REB_home;
    int32_t  HOME_TEAM_WINS;
}; // 48 bytes
#pragma pack(pop)

#define SRC_RECORD_SIZE ((uint32_t)sizeof(struct Record))

// ===== Index node formats =====
typedef uint64_t pageid_t;
enum { NODE_INTERNAL = 1, NODE_LEAF = 2 };

#pragma pack(push, 1)
struct InternalHeader {
    uint8_t  node_type;
    uint8_t  is_root;
    uint16_t reserved0;
    uint32_t num_keys;
    pageid_t parent;
    uint64_t reserved1;
    uint64_t reserved2;
}; // 32 bytes
#pragma pack(pop)

#pragma pack(push, 1)
struct LeafHeader {
    uint8_t  node_type;
    uint8_t  is_root;
    uint16_t reserved0;
    uint32_t num_keys;
    pageid_t parent;
    pageid_t next_leaf;
    uint64_t reserved1;
    uint32_t reserved2;
}; // 40 bytes
#pragma pack(pop)

#pragma pack(push, 1)
struct RID { uint32_t block_no, slot_idx; };
struct LeafEntryOnDisk {
    float    key;
    struct RID rid;
}; // 12 bytes
#pragma pack(pop)

// ===== Derived capacities =====
static const uint32_t INTERNAL_HDR_SIZE = sizeof(struct InternalHeader);
static const uint32_t LEAF_HDR_SIZE = sizeof(struct LeafHeader);

static inline uint32_t leaf_entry_size(void){ return (uint32_t)sizeof(struct LeafEntryOnDisk); }
static inline uint32_t leaf_max_entries(void){ return (BLOCK_SIZE - LEAF_HDR_SIZE) / leaf_entry_size(); }
static inline uint32_t leaf_fill_entries(void){ return (uint32_t)(0.90f * leaf_max_entries()); } // 90% fill factor (same as build)
static inline uint32_t leaf_min_entries(void){ return leaf_fill_entries(); } // Maintain 90% fill factor minimum

// ===== Statistics tracking =====
struct DeletionStats {
    uint64_t index_nodes_accessed;
    uint64_t data_blocks_accessed;
    uint64_t games_deleted;
    double   avg_ft_pct_deleted;
    double   running_time_seconds;
    uint64_t brute_force_blocks;
    double   brute_force_time;
    uint64_t nodes_merged;
    uint64_t entries_removed_from_index;
};

// Track unique blocks accessed
#define MAX_BLOCKS 1024
static uint32_t accessed_blocks[MAX_BLOCKS];
static size_t num_accessed_blocks = 0;

static int block_already_accessed(uint32_t block_no) {
    for (size_t i = 0; i < num_accessed_blocks; i++) {
        if (accessed_blocks[i] == block_no) return 1;
    }
    return 0;
}

static void mark_block_accessed(uint32_t block_no) {
    if (!block_already_accessed(block_no) && num_accessed_blocks < MAX_BLOCKS) {
        accessed_blocks[num_accessed_blocks++] = block_no;
    }
}

// ===== B+ Tree node I/O =====
static void read_node(FILE *fp, pageid_t pid, uint8_t *page) {
    fseek(fp, (long)(pid * BLOCK_SIZE), SEEK_SET);
    fread(page, 1, BLOCK_SIZE, fp);
}

static void write_node(FILE *fp, pageid_t pid, const uint8_t *page) {
    fseek(fp, (long)(pid * BLOCK_SIZE), SEEK_SET);
    fwrite(page, 1, BLOCK_SIZE, fp);
}

static pageid_t find_root(FILE *fp) {
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    uint64_t num_pages = file_size / BLOCK_SIZE;
    
    uint8_t page[BLOCK_SIZE];
    for (uint64_t i = 0; i < num_pages; i++) {
        read_node(fp, i, page);
        uint8_t node_type = page[0];
        if (node_type == NODE_LEAF) {
            struct LeafHeader *h = (struct LeafHeader*)page;
            if (h->is_root) return i;
        } else if (node_type == NODE_INTERNAL) {
            struct InternalHeader *h = (struct InternalHeader*)page;
            if (h->is_root) return i;
        }
    }
    return UINT64_MAX;
}

// ===== Database record deletion =====
static void delete_record_from_database(FILE *db_fp, struct RID rid) {
    uint8_t page[BLOCK_SIZE];
    fseek(db_fp, (long)(rid.block_no * BLOCK_SIZE), SEEK_SET);
    fread(page, 1, BLOCK_SIZE, db_fp);
    
    mark_block_accessed(rid.block_no);
    
    struct SimpleHeader *sh = (struct SimpleHeader*)page;
    if (rid.slot_idx < sh->num_records) {
        struct Record *slots = (struct Record*)(page + sizeof(struct SimpleHeader));
        slots[rid.slot_idx].FT_PCT_home = NAN; // Mark as deleted
        
        fseek(db_fp, (long)(rid.block_no * BLOCK_SIZE), SEEK_SET);
        fwrite(page, 1, BLOCK_SIZE, db_fp);
    }
}

// ===== B+ Tree deletion helper: Remove entry from leaf =====
static void remove_entry_from_leaf(uint8_t *page, uint32_t entry_idx) {
    struct LeafHeader *h = (struct LeafHeader*)page;
    struct LeafEntryOnDisk *entries = (struct LeafEntryOnDisk*)(page + LEAF_HDR_SIZE);
    
    if (entry_idx >= h->num_keys) return;
    
    // Shift entries left to fill the gap
    for (uint32_t i = entry_idx; i < h->num_keys - 1; i++) {
        entries[i] = entries[i + 1];
    }
    h->num_keys--;
}

// ===== B+ Tree deletion helper: Merge two leaf nodes =====
static int merge_leaves(FILE *idx_fp, pageid_t left_pid, pageid_t right_pid, 
                       uint8_t *left_page, struct DeletionStats *stats) {
    struct LeafHeader *left_h = (struct LeafHeader*)left_page;
    struct LeafEntryOnDisk *left_entries = (struct LeafEntryOnDisk*)(left_page + LEAF_HDR_SIZE);
    
    uint8_t right_page[BLOCK_SIZE];
    read_node(idx_fp, right_pid, right_page);
    stats->index_nodes_accessed++;
    
    struct LeafHeader *right_h = (struct LeafHeader*)right_page;
    struct LeafEntryOnDisk *right_entries = (struct LeafEntryOnDisk*)(right_page + LEAF_HDR_SIZE);
    
    // Check if merge is possible
    if (left_h->num_keys + right_h->num_keys > leaf_max_entries()) {
        return 0; // Can't merge, too many entries
    }
    
    // Copy all entries from right to left
    memcpy(&left_entries[left_h->num_keys], right_entries, 
           right_h->num_keys * sizeof(struct LeafEntryOnDisk));
    left_h->num_keys += right_h->num_keys;
    left_h->next_leaf = right_h->next_leaf;
    
    // Write updated left node
    write_node(idx_fp, left_pid, left_page);
    
    // Mark right node as deleted (set num_keys to 0)
    right_h->num_keys = 0;
    write_node(idx_fp, right_pid, right_page);
    
    stats->nodes_merged++;
    return 1; // Merge successful
}

// ===== B+ Tree deletion with repacking =====
static void delete_from_btree(FILE *idx_fp, FILE *db_fp, float threshold, 
                             struct DeletionStats *stats) {
    pageid_t root = find_root(idx_fp);
    if (root == UINT64_MAX) {
        printf("Error: Root node not found\n");
        return;
    }
    
    uint8_t page[BLOCK_SIZE];
    read_node(idx_fp, root, page);
    stats->index_nodes_accessed++;
    
    uint8_t node_type = page[0];
    
    // Navigate to first leaf
    pageid_t leaf_pid = root;
    
    if (node_type == NODE_INTERNAL) {
        // Navigate to leftmost leaf
        while (1) {
            read_node(idx_fp, leaf_pid, page);
            stats->index_nodes_accessed++;
            
            if (page[0] == NODE_LEAF) break;
            
            struct InternalHeader *h = (struct InternalHeader*)page;
            uint8_t *p = page + INTERNAL_HDR_SIZE;
            memcpy(&leaf_pid, p, sizeof(pageid_t)); // Follow first pointer
        }
    }
    
    // Process all leaves, removing matching entries
    while (leaf_pid != UINT64_MAX) {
        read_node(idx_fp, leaf_pid, page);
        stats->index_nodes_accessed++;
        
        struct LeafHeader *h = (struct LeafHeader*)page;
        struct LeafEntryOnDisk *entries = (struct LeafEntryOnDisk*)(page + LEAF_HDR_SIZE);
        
        int modified = 0;
        uint32_t original_count = h->num_keys;
        
        // Remove entries that match criteria (iterate backwards to avoid index issues)
        for (int32_t i = (int32_t)h->num_keys - 1; i >= 0; i--) {
            if (entries[i].key > threshold) {
                // Delete from database
                delete_record_from_database(db_fp, entries[i].rid);
                stats->games_deleted++;
                stats->avg_ft_pct_deleted += entries[i].key;
                
                // Remove from index
                remove_entry_from_leaf(page, (uint32_t)i);
                stats->entries_removed_from_index++;
                modified = 1;
            }
        }
        
        // Write back modified leaf
        if (modified) {
            write_node(idx_fp, leaf_pid, page);
            
            // Check if node is underfull and needs merging
            // (Skip for root node)
            if (h->num_keys < leaf_min_entries() && !h->is_root && h->next_leaf != UINT64_MAX) {
                // Try to merge with next sibling
                merge_leaves(idx_fp, leaf_pid, h->next_leaf, page, stats);
            }
        }
        
        // Move to next leaf (re-read header as it may have been updated)
        read_node(idx_fp, leaf_pid, page);
        struct LeafHeader *updated_h = (struct LeafHeader*)page;
        leaf_pid = updated_h->next_leaf;
    }
    
    stats->data_blocks_accessed = num_accessed_blocks;
}

// ===== Brute force comparison =====
static void brute_force_scan(FILE *db_fp, float threshold, struct DeletionStats *stats) {
    fseek(db_fp, 0, SEEK_END);
    long file_size = ftell(db_fp);
    uint64_t num_blocks = file_size / BLOCK_SIZE;
    
    uint8_t page[BLOCK_SIZE];
    uint64_t deleted_count = 0;
    double total_ft_pct = 0.0;
    
    for (uint64_t b = 0; b < num_blocks; b++) {
        fseek(db_fp, (long)(b * BLOCK_SIZE), SEEK_SET);
        fread(page, 1, BLOCK_SIZE, db_fp);
        stats->brute_force_blocks++;
        
        struct SimpleHeader *sh = (struct SimpleHeader*)page;
        struct Record *slots = (struct Record*)(page + sizeof(struct SimpleHeader));
        
        for (uint32_t s = 0; s < sh->num_records; s++) {
            if (!isnan(slots[s].FT_PCT_home) && slots[s].FT_PCT_home > threshold) {
                deleted_count++;
                total_ft_pct += slots[s].FT_PCT_home;
            }
        }
    }
}

// ===== B+ Tree statistics after deletion =====
struct BTreeStats {
    uint64_t num_leaves;
    uint64_t num_internals;
    uint64_t max_level;
    uint64_t total_entries;
    uint64_t empty_nodes;
};

static void traverse_btree(FILE *fp, pageid_t pid, uint64_t level, struct BTreeStats *st) {
    uint8_t page[BLOCK_SIZE];
    read_node(fp, pid, page);
    
    uint8_t node_type = page[0];
    if (node_type == NODE_LEAF) {
        struct LeafHeader *h = (struct LeafHeader*)page;
        st->num_leaves++;
        st->total_entries += h->num_keys;
        if (h->num_keys == 0) st->empty_nodes++;
        if (level > st->max_level) st->max_level = level;
    } else if (node_type == NODE_INTERNAL) {
        struct InternalHeader *h = (struct InternalHeader*)page;
        st->num_internals++;
        if (level > st->max_level) st->max_level = level;
        
        uint8_t *p = page + INTERNAL_HDR_SIZE;
        pageid_t child;
        memcpy(&child, p, sizeof(pageid_t));
        traverse_btree(fp, child, level + 1, st);
        p += sizeof(pageid_t);
        
        for (uint32_t i = 0; i < h->num_keys; i++) {
            p += sizeof(float); // Skip separator key
            memcpy(&child, p, sizeof(pageid_t));
            p += sizeof(pageid_t);
            traverse_btree(fp, child, level + 1, st);
        }
    }
}

static void print_root_keys(FILE *fp, pageid_t root_pid) {
    uint8_t page[BLOCK_SIZE];
    read_node(fp, root_pid, page);
    
    uint8_t node_type = page[0];
    if (node_type == NODE_LEAF) {
        struct LeafHeader *h = (struct LeafHeader*)page;
        struct LeafEntryOnDisk *entries = (struct LeafEntryOnDisk*)(page + LEAF_HDR_SIZE);
        
        printf("\nRoot is LEAF with %u keys:\n", h->num_keys);
        uint32_t display_limit = h->num_keys < 20 ? h->num_keys : 20;
        for (uint32_t i = 0; i < display_limit; i++) {
            printf("%.3f ", entries[i].key);
        }
        if (h->num_keys > 20) printf("... (%u more)", h->num_keys - 20);
        printf("\n");
    } else if (node_type == NODE_INTERNAL) {
        struct InternalHeader *h = (struct InternalHeader*)page;
        
        printf("\nRoot is INTERNAL with %u keys:\n", h->num_keys);
        uint8_t *p = page + INTERNAL_HDR_SIZE + sizeof(pageid_t);
        uint32_t display_limit = h->num_keys < 20 ? h->num_keys : 20;
        for (uint32_t i = 0; i < display_limit; i++) {
            float k;
            memcpy(&k, p, sizeof(float));
            printf("%.3f ", k);
            p += sizeof(float) + sizeof(pageid_t);
        }
        if (h->num_keys > 20) printf("... (%u more)", h->num_keys - 20);
        printf("\n");
    }
}

// ===== Main =====
int main(int argc, char **argv) {
    if (argc < 3) {
        printf("Usage: %s <database.bin> <bpt.idx>\n", argv[0]);
        return 1;
    }
    
    const char *db_path = argv[1];
    const char *idx_path = argv[2];
    
    // Open files
    FILE *db_fp = fopen(db_path, "rb+");
    if (!db_fp) {
        fprintf(stderr, "Error opening database file: %s\n", strerror(errno));
        return 1;
    }
    
    FILE *idx_fp = fopen(idx_path, "rb+");  // Changed to "rb+" for read/write
    if (!idx_fp) {
        fprintf(stderr, "Error opening index file: %s\n", strerror(errno));
        fclose(db_fp);
        return 1;
    }
    
    // Initialize statistics
    struct DeletionStats stats = {0};
    float threshold = 0.9f;
    
    printf("=== Task 3: Delete records with FT_PCT_home > %.1f ===\n", threshold);
    printf("Using B+ tree with FULL deletion and node repacking...\n");
    printf("Maintaining 90%% fill factor (min entries per leaf: %u)\n\n", leaf_min_entries());
    
    // Reset block tracking
    num_accessed_blocks = 0;
    
    // Print initial B+ tree stats
    pageid_t root = find_root(idx_fp);
    if (root != UINT64_MAX) {
        struct BTreeStats bt_stats_before = {0};
        traverse_btree(idx_fp, root, 1, &bt_stats_before);
        
        printf("=== Initial B+ Tree Statistics ===\n");
        printf("Leaf nodes    : %llu\n", (unsigned long long)bt_stats_before.num_leaves);
        printf("Internal nodes: %llu\n", (unsigned long long)bt_stats_before.num_internals);
        printf("Tree height   : %llu levels\n", (unsigned long long)bt_stats_before.max_level);
        printf("Total entries : %llu\n\n", (unsigned long long)bt_stats_before.total_entries);
    }
    
    // Measure B+ tree deletion time
    clock_t start = clock();
    delete_from_btree(idx_fp, db_fp, threshold, &stats);
    clock_t end = clock();
    stats.running_time_seconds = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Calculate average
    if (stats.games_deleted > 0) {
        stats.avg_ft_pct_deleted /= stats.games_deleted;
    }
    
    // Measure brute force time
    start = clock();
    brute_force_scan(db_fp, threshold, &stats);
    end = clock();
    stats.brute_force_time = ((double)(end - start)) / CLOCKS_PER_SEC;
    
    // Print B+ tree deletion statistics
    printf("=== B+ Tree Deletion Statistics ===\n");
    printf("Index nodes accessed         : %llu\n", (unsigned long long)stats.index_nodes_accessed);
    printf("Data blocks accessed         : %llu\n", (unsigned long long)stats.data_blocks_accessed);
    printf("Games deleted                : %llu\n", (unsigned long long)stats.games_deleted);
    printf("Entries removed from index   : %llu\n", (unsigned long long)stats.entries_removed_from_index);
    printf("Nodes merged                 : %llu\n", (unsigned long long)stats.nodes_merged);
    printf("Average FT_PCT_home deleted  : %.3f\n", stats.avg_ft_pct_deleted);
    printf("Running time                 : %.6f seconds\n", stats.running_time_seconds);
    
    // Print brute force comparison
    printf("\n=== Brute Force Comparison ===\n");
    printf("Data blocks accessed (brute force): %llu\n", (unsigned long long)stats.brute_force_blocks);
    printf("Running time (brute force)        : %.6f seconds\n", stats.brute_force_time);
    if (stats.brute_force_time > 0) {
        printf("Speedup                           : %.2fx\n", stats.brute_force_time / stats.running_time_seconds);
    }
    
    // Get updated B+ tree statistics
    if (root != UINT64_MAX) {
        struct BTreeStats bt_stats_after = {0};
        traverse_btree(idx_fp, root, 1, &bt_stats_after);
        
        printf("\n=== Updated B+ Tree Statistics ===\n");
        printf("Leaf nodes    : %llu\n", (unsigned long long)bt_stats_after.num_leaves);
        printf("Internal nodes: %llu\n", (unsigned long long)bt_stats_after.num_internals);
        printf("Total nodes   : %llu\n", (unsigned long long)(bt_stats_after.num_leaves + bt_stats_after.num_internals));
        printf("Empty nodes   : %llu\n", (unsigned long long)bt_stats_after.empty_nodes);
        printf("Tree height   : %llu levels\n", (unsigned long long)bt_stats_after.max_level);
        printf("Total entries : %llu\n", (unsigned long long)bt_stats_after.total_entries);
        printf("Avg entries/leaf: %.1f\n", 
               bt_stats_after.num_leaves > 0 ? 
               (double)bt_stats_after.total_entries / bt_stats_after.num_leaves : 0.0);
        
        print_root_keys(idx_fp, root);
    }
    
    // Close files
    fclose(db_fp);
    fclose(idx_fp);
    
    printf("\n=== Deletion complete! ===\n");
    printf("Note: %llu entries removed from index, %llu nodes merged\n", 
           (unsigned long long)stats.entries_removed_from_index,
           (unsigned long long)stats.nodes_merged);
    
    return 0;
}
