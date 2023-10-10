#include "SimpleCache.h"

uint8_t L1Cache[L1_SIZE];
uint8_t L2Cache[L2_SIZE];
uint8_t DRAM[DRAM_SIZE];
uint32_t time;
Cache SimpleCache;

/**************** Time Manipulation ***************/
void resetTime() { time = 0; }

uint32_t getTime() { return time; }

/****************  RAM memory (byte addressable) ***************/
void accessDRAM(uint32_t address, uint8_t *data, uint32_t mode) {

  if (address >= DRAM_SIZE - WORD_SIZE + 1)
    exit(-1);

  if (mode == MODE_READ) {
    memcpy(data, &(DRAM[address]), BLOCK_SIZE);
    time += DRAM_READ_TIME;
  }

  if (mode == MODE_WRITE) {
    memcpy(&(DRAM[address]), data, BLOCK_SIZE);
    time += DRAM_WRITE_TIME;
  }
}

/*********************** L1 cache *************************/

void initCache() { SimpleCache.init = 0; }

void accessL1(uint32_t address, uint8_t *data, uint32_t mode) {

  uint32_t index, Tag, MemAddress, offset;
  uint8_t TempBlock[BLOCK_SIZE];

  /* init cache */
  if (SimpleCache.init == 0) {
    SimpleCache.init = 1;
    for (int i = 0; i < (L1_SIZE/BLOCK_SIZE); i++) {
      SimpleCache.line[i].Valid = 0;
      SimpleCache.line[i].Dirty = 0;
      SimpleCache.line[i].Tag = 0;
    }
    for (int i = 0; i < (L1_SIZE); i++)
      L1Cache[i] = 0;
  }

  /*  to implement this cache (direct mapped cache with 256 blocks, each block have 16*4=64 bytes)
      we need the following division of the address */
  uint8_t offset_bits = 6;
  uint8_t index_bits = 8;
  uint8_t tag_bits = 32 - (index_bits + offset_bits);

  Tag = address >> (index_bits + offset_bits);
  index = address << tag_bits;
  index = index >> (tag_bits + offset_bits);
  offset = address << (tag_bits + index_bits);
  offset = offset >> (tag_bits + index_bits);

  CacheLine *Line = &SimpleCache.line[index];

  MemAddress = address >> offset_bits;
  MemAddress = MemAddress << offset_bits;

  /* access Cache*/

  if (!Line->Valid || Line->Tag != Tag) {         // if block not present - miss
    accessDRAM(MemAddress, TempBlock, MODE_READ); // get new block from DRAM

    if ((Line->Valid) && (Line->Dirty)) { // line has dirty block
      MemAddress = Line->Tag << index_bits;
      MemAddress = (MemAddress + index) << offset_bits;        // get address of the block in memory
      accessDRAM(MemAddress, &(L1Cache[index*BLOCK_SIZE]), MODE_WRITE); // then write back old block
    }

    memcpy(&(L1Cache[index*BLOCK_SIZE]), TempBlock,
           BLOCK_SIZE); // copy new block to cache line
    Line->Valid = 1;
    Line->Tag = Tag;
    Line->Dirty = 0;
  } // if miss, then replaced with the correct block

  if (mode == MODE_READ) {    // read data from cache line
    if (!(address % BLOCK_SIZE)) { // even word on block
      memcpy(data, &(L1Cache[index*BLOCK_SIZE]), WORD_SIZE);
    } else { // odd word on block
      memcpy(data, &(L1Cache[index*BLOCK_SIZE+offset]), WORD_SIZE);
    }
    time += L1_READ_TIME;
  }

  if (mode == MODE_WRITE) { // write data from cache line
    if (!(address % BLOCK_SIZE)) {   // even word on block
      memcpy(&(L1Cache[index*BLOCK_SIZE]), data, WORD_SIZE);
    } else { // odd word on block
      memcpy(&(L1Cache[index*BLOCK_SIZE+offset]), data, WORD_SIZE);
    }
    time += L1_WRITE_TIME;
    Line->Dirty = 1;
  }
}

void read(uint32_t address, uint8_t *data) {
  accessL1(address, data, MODE_READ);
}

void write(uint32_t address, uint8_t *data) {
  accessL1(address, data, MODE_WRITE);
}
