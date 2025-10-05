#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>


#define BLOCK_SIZE 4096 // 4KB is the typical size of a block
#define RECORD_SIZE 48  // size of each record, justified the bytes for each data type in the report doc, mainly 4 for each int/float and 16 for char[16]
#define MAX_RECORDS_PER_BLOCK ((BLOCK_SIZE - sizeof(uint32_t)) / RECORD_SIZE)  // 85
#define BUFFER_POOL_SIZE 10 


#pragma pack(push, 1)
typedef struct {
   char GAME_DATE_EST[16];
   int32_t TEAM_ID_home;
   int32_t PTS_home;
   float FG_PCT_home;
   float FT_PCT_home;
   float FG3_PCT_home;
   int32_t AST_home;
   int32_t REB_home;
   int32_t HOME_TEAM_WINS;
} Record; // 48 bytes
#pragma pack(pop) 



typedef struct {
   const char *field_names[9];
   const char *types[9];
   int sizes[9];
} Schema; // 9 fields in total with 9 data types 

//create the schema for the nba game records


Schema get_schema() {
   Schema s;
   s.field_names[0] = "GAME_DATE_EST"; s.types[0] = "char[16]"; s.sizes[0] = 16;
   s.field_names[1] = "TEAM_ID_home"; s.types[1] = "int"; s.sizes[1] = 4;
   s.field_names[2] = "PTS_home"; s.types[2] = "int"; s.sizes[2] = 4;
   s.field_names[3] = "FG_PCT_home"; s.types[3] = "float"; s.sizes[3] = 4;
   s.field_names[4] = "FT_PCT_home"; s.types[4] = "float"; s.sizes[4] = 4;
   s.field_names[5] = "FG3_PCT_home"; s.types[5] = "float"; s.sizes[5] = 4;
   s.field_names[6] = "AST_home"; s.types[6] = "int"; s.sizes[6] = 4;
   s.field_names[7] = "REB_home"; s.types[7] = "int"; s.sizes[7] = 4;
   s.field_names[8] = "HOME_TEAM_WINS"; s.types[8] = "int"; s.sizes[8] = 4;


   return s;


}

typedef struct {

   FILE *file;
   long offset;
} StorageBlockPtr; // quick access, points to where the block is 

typedef struct {
   StorageBlockPtr block;
   int offset_in_block;
   int size;

} DataSegmentPtr; //points to a segment in the block

typedef struct BufferFrame {

   char data[BLOCK_SIZE];
   int block_id;
   int dirty;
   int pin_count;

   time_t last_access;
   struct BufferFrame *prev;
   struct BufferFrame *next;
} BufferFrame; // each frame in the buffer pool holds the block data

typedef struct {
   BufferFrame frames[BUFFER_POOL_SIZE];
   BufferFrame *head;
   BufferFrame *tail;
   FILE *db_file;

} BufferPool;  //structure for the buffer pool 

void init_buffer_pool(BufferPool *pool, const char *filename) {

   pool->db_file = fopen(filename, "rb+");
   if (!pool->db_file) pool->db_file = fopen(filename, "wb+");
   if (!pool->db_file) {
       perror("Failed to open database file");
       exit(1);
       
   }
   for (int i = 0; i < BUFFER_POOL_SIZE; i++) {

       pool->frames[i].block_id = -1;
       pool->frames[i].dirty = 0;
       pool->frames[i].pin_count = 0;
       pool->frames[i].prev = NULL;
       pool->frames[i].next = NULL;
   }
   pool->head = NULL;
   pool->tail = NULL;

} //open file for read/write, create it if it doesnt exist. 

void move_to_tail(BufferPool *pool, BufferFrame *frame) {
   if (frame == pool->tail) return;
   if (frame->prev) frame->prev->next = frame->next;
   if (frame->next) frame->next->prev = frame->prev;
   if (frame == pool->head) pool->head = frame->next;
   frame->prev = pool->tail;
   frame->next = NULL;
   if (pool->tail) pool->tail->next = frame;
   pool->tail = frame;
   if (!pool->head) pool->head = frame;

} // move frame to end of LRU list, 


BufferFrame *find_frame(BufferPool *pool, int block_id) {
   for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
       if (pool->frames[i].block_id == block_id) {
           pool->frames[i].last_access = time(NULL);
           move_to_tail(pool, &pool->frames[i]);
           return &pool->frames[i];
       }
   }

   return NULL;
}  //given blockid, find frame in the pool.

BufferFrame *get_free_frame(BufferPool *pool) {

   for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
       if (pool->frames[i].block_id == -1) return &pool->frames[i];
   }

   return NULL;
} 

void evict_lru(BufferPool *pool) {

   BufferFrame *evict = pool->head;
   while (evict && evict->pin_count > 0) evict = evict->next;
   if (!evict) {
       fprintf(stderr, "No unpinned frame to evict\n");
       exit(1);
   }
   if (evict->dirty) {
       fseek(pool->db_file, (long)evict->block_id * BLOCK_SIZE, SEEK_SET);
       fwrite(evict->data, BLOCK_SIZE, 1, pool->db_file);
       evict->dirty = 0;
   }
   evict->block_id = -1;
   evict->pin_count = 0;
   if (evict->prev) evict->prev->next = evict->next;
   if (evict->next) evict->next->prev = evict->prev;
   if (evict == pool->head) pool->head = evict->next;
   if (evict == pool->tail) pool->tail = evict->prev;
} // using the LRU policy, we evict the least recently used frame. 

BufferFrame *load_block(BufferPool *pool, int block_id) {
   // load block from disk to frame 
   BufferFrame *frame = find_frame(pool, block_id);
   if (frame) return frame;
   frame = get_free_frame(pool);
   if (!frame) evict_lru(pool);
   frame = get_free_frame(pool);
   fseek(pool->db_file, (long)block_id * BLOCK_SIZE, SEEK_SET);
   size_t read = fread(frame->data, 1, BLOCK_SIZE, pool->db_file);
   if (read < BLOCK_SIZE) {
       memset(frame->data + read, 0, BLOCK_SIZE - read); // zero out unused space
       *(uint32_t *)frame->data = 0; // initialize num_records to 0 for new blocks
   }
   frame->block_id = block_id;
   frame->dirty = 0;
   frame->pin_count = 0;
   frame->last_access = time(NULL);
   frame->prev = pool->tail;
   frame->next = NULL;
   if (pool->tail) pool->tail->next = frame;
   pool->tail = frame;
   if (!pool->head) pool->head = frame;
   return frame;
}

void write_block(BufferPool *pool, BufferFrame *frame, int block_id) { //writing frame back to disk
   fseek(pool->db_file, (long)block_id * BLOCK_SIZE, SEEK_SET);
   fwrite(frame->data, BLOCK_SIZE, 1, pool->db_file);
}

void write_record(BufferPool *pool, Record *rec, int block_id, int offset) {

   BufferFrame *frame = load_block(pool, block_id);
   frame->pin_count++;
   uint32_t *num_records = (uint32_t *)frame->data;
   if (offset >= MAX_RECORDS_PER_BLOCK) {
       fprintf(stderr, "Offset out of bounds\n");
       exit(1);
   }
   memcpy(frame->data + sizeof(uint32_t) + offset * RECORD_SIZE, rec, RECORD_SIZE);
   *num_records = offset + 1; // forcefully set num_records to the current offset + 1
   frame->dirty = 1; // explicitly mark as dirty
   frame->pin_count--;
}

Record *read_record(BufferPool *pool, int block_id, int offset) {
   BufferFrame *frame = load_block(pool, block_id);
   frame->pin_count++;
   uint32_t *num_records = (uint32_t *)frame->data;
   if (offset >= *num_records) {
       frame->pin_count--;
       return NULL;
   }
   Record *rec = malloc(RECORD_SIZE);
   memcpy(rec, frame->data + sizeof(uint32_t) + offset * RECORD_SIZE, RECORD_SIZE);
   frame->pin_count--;
   return rec;
}

void flush_buffer_pool(BufferPool *pool) {
   for (int i = 0; i < BUFFER_POOL_SIZE; i++) {
       if (pool->frames[i].dirty) {
           write_block(pool, &pool->frames[i], pool->frames[i].block_id);
           pool->frames[i].dirty = 0;
       }
   }
   fflush(NULL); // ensuring all writes are synced to disk
}

StorageBlockPtr allocate_block(BufferPool *pool) {

   fseek(pool->db_file, 0, SEEK_END);
   long offset = ftell(pool->db_file);
   
   StorageBlockPtr bp = {pool->db_file, offset};
   return bp;
} //allocate new block at end of file 

void file_handler_write(BufferPool *pool, StorageBlockPtr bp, char *data) {

   BufferFrame *frame = load_block(pool, bp.offset / BLOCK_SIZE);
   memcpy(frame->data, data, BLOCK_SIZE);
   frame->dirty = 1;

} //using ptr, write data to block 

void database_controller_load(BufferPool *pool, const char *txt_file) {

   FILE *data_file = fopen(txt_file, "r");
   if (!data_file) {
       perror("Cannot open games.txt");
       exit(1);
   }

   char line[256];
   fgets(line, sizeof(line), data_file); 
   int record_count = 0;
   int block_id = 0;
   int offset = 0;
   Schema s = get_schema();

   while (fgets(line, sizeof(line), data_file)) {
       Record rec;
       memset(&rec, 0, RECORD_SIZE);
       sscanf(line, "%15s\t%d\t%d\t%f\t%f\t%f\t%d\t%d\t%d", rec.GAME_DATE_EST, &rec.TEAM_ID_home, &rec.PTS_home,
              &rec.FG_PCT_home, &rec.FT_PCT_home, &rec.FG3_PCT_home, &rec.AST_home, &rec.REB_home, &rec.HOME_TEAM_WINS);
       write_record(pool, &rec, block_id, offset);
       offset++;
       if (offset >= MAX_RECORDS_PER_BLOCK) {
           offset = 0;
           block_id++;
       }
       record_count++;
   }
   fclose(data_file);
   flush_buffer_pool(pool);
   printf("Loaded %d records into the database.\n", record_count);
   printf("Total blocks used: %d\n", block_id + (offset > 0 ? 1 : 0));
   printf("Size of each record: %d bytes\n", RECORD_SIZE);

}

int main() {

   BufferPool pool;
   init_buffer_pool(&pool, "database.bin");
   database_controller_load(&pool, "games.txt");
   
   // verify the first block by printing the first few records to confirm that the code works 
   BufferFrame *first_frame = load_block(&pool, 0);
   uint32_t *num_records = (uint32_t *)first_frame->data;
   printf("Sample of first block (up to 5 records):\n");
   for (int i = 0; i < *num_records && i < 5; i++) {
       Record *rec = read_record(&pool, 0, i);
       if (rec) {
           printf("  Record %d: %s, %d, %d, %.3f\n", i + 1, rec->GAME_DATE_EST, rec->TEAM_ID_home, rec->PTS_home, rec->FG_PCT_home);
           free(rec);


       }
   }


   flush_buffer_pool(&pool);
   fclose(pool.db_file);
   return 0;
}