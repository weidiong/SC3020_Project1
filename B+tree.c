// Build command (Windows):
//   gcc -O2 -std=c11 "B+tree.c" -o build_bpt.exe
//   .\build_bpt.exe database.bin bpt.idx

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <math.h>
#include <errno.h>

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
    float    FT_PCT_home; // index key
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
static const uint32_t LEAF_HDR_SIZE     = sizeof(struct LeafHeader);

static inline uint32_t leaf_entry_size(void){ return (uint32_t)sizeof(struct LeafEntryOnDisk); }
static inline uint32_t leaf_max_entries(void){ return (BLOCK_SIZE - LEAF_HDR_SIZE) / leaf_entry_size(); }
static inline uint32_t leaf_fill_entries(void){ return (uint32_t)floor(0.90f * leaf_max_entries()); }
static inline uint32_t internal_keys_max(void){
    return (BLOCK_SIZE - INTERNAL_HDR_SIZE - (uint32_t)sizeof(pageid_t))        // 32 + n*KEY_SIZE + (n+1)*PTR_SIZE <= 4096
           / (sizeof(((struct LeafEntryOnDisk *)0)->key)+ (uint32_t)sizeof(pageid_t));}       // 338 keys
static inline uint32_t internal_max_pointers(void){ return internal_keys_max()+1; }
static inline uint32_t internal_fill_pointers(void){ return (uint32_t)floor(0.90f * internal_max_pointers()); }

// ===== In-memory structs =====
struct KeyRID { float key; struct RID rid; };
struct ChildForParent { pageid_t child_pid; float first_key; };

// ===== Utilities =====
static int cmp_keyrid(const void *a, const void *b){
    const struct KeyRID *x = (const struct KeyRID*)a, *y = (const struct KeyRID*)b;
    if (x->key < y->key) return -1;
    if (x->key > y->key) return 1;
    if (x->rid.block_no < y->rid.block_no) return -1;
    if (x->rid.block_no > y->rid.block_no) return 1;
    if (x->rid.slot_idx < y->rid.slot_idx) return -1;
    if (x->rid.slot_idx > y->rid.slot_idx) return 1;
    return 0;
}

static inline int is_null_key(float v){ return isnan(v); }
static inline float normalize_key(float v){ return v; }

// ===== Page writer =====
struct PageWriter { FILE *fp; pageid_t next_pid; };
static void pw_init(struct PageWriter *pw, FILE *fp){ pw->fp = fp; pw->next_pid = 0; }
static pageid_t pw_write_page(struct PageWriter *pw, const void *page){
    fseek(pw->fp, (long)(pw->next_pid * BLOCK_SIZE), SEEK_SET);
    fwrite(page, 1, BLOCK_SIZE, pw->fp);
    return pw->next_pid++;
}

// ===== Page builders =====
static pageid_t write_leaf(struct PageWriter *pw,
                           const struct KeyRID *items, uint32_t n,
                           pageid_t parent, pageid_t next_leaf,
                           int is_root, float *out_first_key)
{
    uint8_t page[BLOCK_SIZE]; memset(page, 0, sizeof(page));
    struct LeafHeader *hdr = (struct LeafHeader*)page;
    hdr->node_type = NODE_LEAF; hdr->is_root = is_root; hdr->num_keys = n;
    hdr->parent = parent; hdr->next_leaf = next_leaf;

    struct LeafEntryOnDisk *dst = (struct LeafEntryOnDisk*)(page + LEAF_HDR_SIZE);
    for (uint32_t i = 0; i < n; ++i) dst[i] = (struct LeafEntryOnDisk){ items[i].key, items[i].rid };

    *out_first_key = (n ? items[0].key : -INFINITY);
    return pw_write_page(pw, page);
}

static pageid_t write_internal(struct PageWriter *pw,
                               const struct ChildForParent *children, uint32_t m,
                               pageid_t parent, int is_root, float *out_first_key)
{
    uint8_t page[BLOCK_SIZE]; memset(page, 0, sizeof(page));
    struct InternalHeader *hdr = (struct InternalHeader*)page;
    hdr->node_type = NODE_INTERNAL; hdr->is_root = is_root; hdr->num_keys = (m ? m - 1 : 0); hdr->parent = parent;

    uint8_t *p = page + INTERNAL_HDR_SIZE;
    memcpy(p, &children[0].child_pid, sizeof(pageid_t)); p += sizeof(pageid_t);

    for (uint32_t i = 1; i < m; ++i){
        float sep = children[i].first_key;
        memcpy(p, &sep, sizeof(float)); p += sizeof(float);
        memcpy(p, &children[i].child_pid, sizeof(pageid_t)); p += sizeof(pageid_t);
    }

    *out_first_key = (m ? children[0].first_key : -INFINITY);
    return pw_write_page(pw, page);
}

// ===== Bulk-load =====
static pageid_t bulk_load(struct KeyRID *items, size_t N, FILE *fout_idx){
    struct PageWriter pw; pw_init(&pw, fout_idx);

    const uint32_t LCAP = leaf_fill_entries();
    size_t pos = 0, cap = (N / LCAP) + 4, level_sz = 0;
    struct ChildForParent *level = malloc(cap * sizeof(*level));
    pageid_t prev_leaf = UINT64_MAX;

    // Build leaves
    while (pos < N){
        uint32_t take = (uint32_t)((N - pos) < LCAP ? (N - pos) : LCAP);
        float fk;
        pageid_t pid = write_leaf(&pw, &items[pos], take, UINT64_MAX, UINT64_MAX, 0, &fk);

        if (prev_leaf != UINT64_MAX){
            fseek(fout_idx, (long)(prev_leaf * BLOCK_SIZE), SEEK_SET);
            uint8_t buf[BLOCK_SIZE]; fread(buf,1,BLOCK_SIZE,fout_idx);
            ((struct LeafHeader*)buf)->next_leaf = pid;
            fseek(fout_idx, (long)(prev_leaf * BLOCK_SIZE), SEEK_SET);
            fwrite(buf,1,BLOCK_SIZE,fout_idx);
        }
        prev_leaf = pid;

        level[level_sz++] = (struct ChildForParent){ pid, fk };
        pos += take;
    }

    if (N == 0){
        float fk; write_leaf(&pw, NULL, 0, UINT64_MAX, UINT64_MAX, 1, &fk);
        free(level);
        return 0;
    }

    // Build internal levels
    const uint32_t IFAN = internal_fill_pointers();
    while (level_sz > 1){
        size_t in_pos = 0, next_cap = (level_sz / IFAN) + 4, next_sz = 0;
        struct ChildForParent *next = malloc(next_cap * sizeof(*next));

        while (in_pos < level_sz){
            uint32_t take = (uint32_t)((level_sz - in_pos) < IFAN ? (level_sz - in_pos) : IFAN);
            float fk;
            pageid_t pid = write_internal(&pw, &level[in_pos], take, UINT64_MAX, 0, &fk);
            next[next_sz++] = (struct ChildForParent){ pid, fk };
            in_pos += take;
        }

        free(level); level = next; level_sz = next_sz;
    }

    // Mark root
    pageid_t root = level[0].child_pid;
    free(level);

    fseek(fout_idx, (long)(root * BLOCK_SIZE), SEEK_SET);
    uint8_t buf[BLOCK_SIZE]; fread(buf,1,BLOCK_SIZE,fout_idx);
    if (buf[0] == NODE_LEAF) ((struct LeafHeader*)buf)->is_root = 1;
    else ((struct InternalHeader*)buf)->is_root = 1;
    fseek(fout_idx, (long)(root * BLOCK_SIZE), SEEK_SET);
    fwrite(buf,1,BLOCK_SIZE,fout_idx);

    return root;   // return PID of root
}


// ===== Read database.bin =====
static void collect_pairs(FILE *fin, struct KeyRID **out_arr, size_t *out_N, size_t *out_skipped){
    fseek(fin, 0L, SEEK_END);
    long fsz = ftell(fin);
    fseek(fin, 0L, SEEK_SET);
    uint64_t num_blocks = (uint64_t)fsz / BLOCK_SIZE;

    uint8_t page[BLOCK_SIZE];
    uint64_t total = 0;
    for (uint64_t b = 0; b < num_blocks; ++b){
        fread(page,1,BLOCK_SIZE,fin);
        const struct SimpleHeader *sh = (const struct SimpleHeader*)page;
        total += sh->num_records;
    }

    struct KeyRID *arr = malloc(sizeof(struct KeyRID) * total);
    fseek(fin, 0L, SEEK_SET);
    size_t idx = 0, skipped = 0;
    for (uint64_t b = 0; b < num_blocks; ++b){
        fread(page,1,BLOCK_SIZE,fin);
        const struct SimpleHeader *sh = (const struct SimpleHeader*)page;
        struct Record *slots = (struct Record*)(page + sizeof(struct SimpleHeader));
        for (uint32_t s = 0; s < sh->num_records; ++s){
            float key = slots[s].FT_PCT_home;
            if (is_null_key(key)){ skipped++; continue; }
            arr[idx].key = normalize_key(key);
            arr[idx].rid.block_no = (uint32_t)b;
            arr[idx].rid.slot_idx = (uint32_t)s;
            idx++;
        }
    }
    *out_arr = arr; *out_N = idx; if (out_skipped) *out_skipped = skipped;
}

// ===== Stats =====
struct Stats {
    uint64_t num_leaves;
    uint64_t num_internals;
    uint64_t max_level;
};

// Recursive traversal (counts only)
static void traverse(FILE *fp, pageid_t pid, uint64_t level, struct Stats *st) {
    uint8_t page[BLOCK_SIZE];
    fseek(fp, (long)(pid * BLOCK_SIZE), SEEK_SET);
    fread(page, 1, BLOCK_SIZE, fp);

    uint8_t node_type = page[0];
    if (node_type == NODE_LEAF) {
        st->num_leaves++;
        if (level > st->max_level) st->max_level = level;
    } else if (node_type == NODE_INTERNAL) {
        struct InternalHeader *h = (struct InternalHeader*)page;
        st->num_internals++;
        if (level > st->max_level) st->max_level = level;

        uint8_t *p = page + INTERNAL_HDR_SIZE;
        pageid_t child;
        memcpy(&child, p, sizeof(pageid_t));
        traverse(fp, child, level + 1, st);
        p += sizeof(pageid_t);

        for (uint32_t i = 0; i < h->num_keys; i++) {
            float sep; memcpy(&sep, p, sizeof(float)); p += sizeof(float);
            memcpy(&child, p, sizeof(pageid_t)); p += sizeof(pageid_t);
            traverse(fp, child, level + 1, st);
        }
    }
}

// ===== Print root info with child PIDs =====
static void print_root_info(const char *idx_path) {
    FILE *fp = fopen(idx_path, "rb");
    if (!fp) { perror("fopen"); return; }

    uint8_t page[BLOCK_SIZE];
    fseek(fp, 0, SEEK_SET);
    fread(page, 1, BLOCK_SIZE, fp);
    fclose(fp);

    uint8_t node_type = page[0];
    if (node_type == NODE_LEAF) {
        struct LeafHeader *h = (struct LeafHeader*)page;
        printf("\nRoot is LEAF\n");
        printf("Root keys  : %u\n", h->num_keys);
        printf("Root ptrs  : %u (RIDs)\n", h->num_keys);
    } else if (node_type == NODE_INTERNAL) {
        struct InternalHeader *h = (struct InternalHeader*)page;
        printf("\nRoot is INTERNAL\n");
        printf("Root keys  : %u\n", h->num_keys);
        printf("Root ptrs  : %u\n", h->num_keys + 1);

        uint8_t *p = page + INTERNAL_HDR_SIZE;
        pageid_t child;
        memcpy(&child, p, sizeof(pageid_t));
        p += sizeof(pageid_t);

        printf("Layout: [P0=%llu]", (unsigned long long)child);
        for (uint32_t i = 0; i < h->num_keys; i++) {
            float k; memcpy(&k, p, sizeof(float)); p += sizeof(float);
            memcpy(&child, p, sizeof(pageid_t)); p += sizeof(pageid_t);
            printf(" --(%.3f)--> [P%u=%llu]", k, i+1, (unsigned long long)child);
        }
        printf("\n");
    }
}

// ===== Print only the keys in the root node =====
static void print_root_keys(const char *idx_path, pageid_t root_pid) {
    FILE *fp = fopen(idx_path, "rb");
    if (!fp) { perror("fopen"); return; }

    uint8_t page[BLOCK_SIZE];
    fseek(fp, (long)(root_pid * BLOCK_SIZE), SEEK_SET);
    fread(page, 1, BLOCK_SIZE, fp);
    fclose(fp);

    uint8_t node_type = page[0];
    if (node_type == NODE_LEAF) {
        struct LeafHeader *h = (struct LeafHeader*)page;
        struct LeafEntryOnDisk *entries = (struct LeafEntryOnDisk*)(page + LEAF_HDR_SIZE);

        printf("\nRoot is LEAF with %u keys:\n", h->num_keys);
        for (uint32_t i = 0; i < h->num_keys; i++) {
            printf("%.3f ", entries[i].key);
        }
        printf("\n");

    } else if (node_type == NODE_INTERNAL) {
        struct InternalHeader *h = (struct InternalHeader*)page;

        printf("\nRoot is INTERNAL with %u keys:\n", h->num_keys);
        uint8_t *p = page + INTERNAL_HDR_SIZE + sizeof(pageid_t); // skip P0
        for (uint32_t i = 0; i < h->num_keys; i++) {
            float k; memcpy(&k, p, sizeof(float));
            printf("%.3f ", k);
            p += sizeof(float) + sizeof(pageid_t); // skip key and pointer
        }
        printf("\n");
    } else {
        printf("Root node has invalid type: %u\n", node_type);
    }
}

// ===== Count and print the number of keys in each leaf =====
static void print_leaf_node_counts(const char *idx_path, pageid_t root_pid) {
    FILE *fp = fopen(idx_path, "rb");
    if (!fp) { perror("fopen"); return; }

    uint8_t page[BLOCK_SIZE];
    fseek(fp, (long)(root_pid * BLOCK_SIZE), SEEK_SET);
    fread(page, 1, BLOCK_SIZE, fp);

    uint8_t node_type = page[0];

    // If root itself is a leaf, just print it
    if (node_type == NODE_LEAF) {
        struct LeafHeader *h = (struct LeafHeader*)page;
        printf("Leaf PID=%llu has %u keys\n",
               (unsigned long long)root_pid, h->num_keys);
        fclose(fp);
        return;
    }

    // Otherwise, descend to the leftmost leaf
    pageid_t child;
    uint8_t *p = page + INTERNAL_HDR_SIZE;
    memcpy(&child, p, sizeof(pageid_t));

    // Follow down until we hit a leaf
    while (1) {
        fseek(fp, (long)(child * BLOCK_SIZE), SEEK_SET);
        fread(page, 1, BLOCK_SIZE, fp);
        if (page[0] == NODE_LEAF) break;

        struct InternalHeader *h = (struct InternalHeader*)page;
        p = page + INTERNAL_HDR_SIZE;
        memcpy(&child, p, sizeof(pageid_t)); // always follow P0
    }

    // Now child is the first leaf
    pageid_t leaf_pid = child;
    while (leaf_pid != UINT64_MAX) {
        fseek(fp, (long)(leaf_pid * BLOCK_SIZE), SEEK_SET);
        fread(page, 1, BLOCK_SIZE, fp);

        struct LeafHeader *h = (struct LeafHeader*)page;
        printf("Leaf PID=%llu has %u keys\n",
               (unsigned long long)leaf_pid, h->num_keys);

        leaf_pid = h->next_leaf;
    }

    fclose(fp);
}



int main(int argc, char **argv){
    const char *in_path  = (argc >= 2) ? argv[1] : "database.bin";
    const char *out_path = (argc >= 3) ? argv[2] : "bpt.idx";

    FILE *fin = fopen(in_path, "rb");
    if (!fin){ fprintf(stderr, "open %s: %s\n", in_path, strerror(errno)); return 1; }

    struct KeyRID *arr = NULL; size_t N = 0, skipped = 0;
    collect_pairs(fin, &arr, &N, &skipped);
    fclose(fin);

    qsort(arr, N, sizeof(struct KeyRID), cmp_keyrid);

    FILE *fout = fopen(out_path, "wb+");
    pageid_t root_pid = bulk_load(arr, N, fout);   // <-- get actual root PID
    fflush(fout); fclose(fout); free(arr);

    printf("B+ tree built.\n");
    printf("Filtered out null keys: %zu\n", skipped);
    printf("Input records used: %zu\n", N);

    // Traverse using the real root
    FILE *fp = fopen(out_path, "rb");
    struct Stats st = {0,0,0};
    traverse(fp, root_pid, 1, &st);
    fclose(fp);

    printf("\nActual B+ Tree stats:\n");
    printf("Leaf nodes    : %llu\n", (unsigned long long)st.num_leaves);
    printf("Internal nodes: %llu\n", (unsigned long long)st.num_internals);
    printf("Tree height   : %llu levels\n", (unsigned long long)st.max_level);

    print_root_keys(out_path, root_pid);
    print_leaf_node_counts(out_path, root_pid);



    return 0;
}







