#include "SimpleCache.h"

uint8_t L1Cache[L1_SIZE];
uint8_t L2Cache[L2_SIZE];
uint8_t DRAM[DRAM_SIZE];
uint32_t time;
CacheL1 SimpleCacheL1;
CacheL2 SimpleCacheL2;


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

void initCache() { 
  SimpleCacheL1.init = 0;
  SimpleCacheL2.init = 0; 
}

void accessL1(uint32_t address, uint8_t *data, uint32_t mode) {

  uint32_t index, Tag, offset;
  uint8_t TempBlock[BLOCK_SIZE];

  /* init cache */
  if (SimpleCacheL1.init == 0) {
    SimpleCacheL1.init = 1;
    for (int i = 0; i < (L1_SIZE/BLOCK_SIZE); i++) {
      SimpleCacheL1.line[i].Valid = 0;
      SimpleCacheL1.line[i].Dirty = 0;
      SimpleCacheL1.line[i].Tag = 0;
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

  CacheLine *Line = &SimpleCacheL1.line[index];

  /* access Cache*/

  if (!Line->Valid || Line->Tag != Tag) {         // if block not present - miss
    accessL2(address, TempBlock, MODE_READ); // get new block from DRAM

    if ((Line->Valid) && (Line->Dirty)) { // line has dirty block
      accessL2(address, &(L1Cache[index*BLOCK_SIZE]), MODE_WRITE); // then write back old block
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

/*********************** L2 cache *************************/

void accessL2(uint32_t address, uint8_t *data, uint32_t mode) {

  uint32_t index, Tag, MemAddress, offset;
  uint8_t TempBlock[BLOCK_SIZE];
  uint8_t ASSOCIATIVITY = 2;

  /* init cache */
  if (SimpleCacheL2.init == 0) {
    SimpleCacheL2.init = 1;
    for (int i = 0; i < (L2_SIZE/(BLOCK_SIZE*2)); i++) {
      for (int j = 0; j < ASSOCIATIVITY; j++) {
        SimpleCacheL2.set[i].line[j].Valid = 0;
        SimpleCacheL2.set[i].line[j].Dirty = 0;
        SimpleCacheL2.set[i].line[j].LRU = 0;
        SimpleCacheL2.set[i].line[j].Tag = 0;
      }
    }
    for (int i = 0; i < (L2_SIZE); i++)
      L2Cache[i] = 0;
  }

  /*  to implement this cache (2-way cache with 512 blocks, each block have 16*4=64 bytes)
      we need the following division of the address */
  uint8_t offset_bits = 6;
  uint8_t index_bits = 8;
  uint8_t tag_bits = 32 - (index_bits + offset_bits);

  Tag = address >> (index_bits + offset_bits);
  index = address << tag_bits;
  index = index >> (tag_bits + offset_bits);
  offset = address << (tag_bits + index_bits);
  offset = offset >> (tag_bits + index_bits);

  CacheLine *Line = SimpleCacheL2.set[index].line;

  MemAddress = address >> offset_bits;
  MemAddress = (MemAddress + index) << 1; // each set have 2 lines (2 positions in memory)

  /* access Cache*/

  int i;  //select the block to use on the set
  index = index << 1; //each index has 2 blocks (2-way)

  // if block not present - miss
  if ((!Line[0].Valid || Line[0].Tag != Tag) && (!Line[1].Valid || Line[1].Tag != Tag)) { 
    
    if (Line[0].LRU) {  //if block 0 is the LRU
      Line[0].LRU = 0;  //not used in this access
      Line[1].LRU = 1;  //will be replaced (used in this access)
      i = 1;
    } else {            //if block 1 is the LRU
      Line[0].LRU = 1;  //will be replaced (used in this access)
      Line[1].LRU = 0;  //not used in this access
      i = 0;
    }

    MemAddress = (MemAddress+i) << offset_bits;   // get address of the block in memory
    accessDRAM(MemAddress, TempBlock, MODE_READ); // get new block from DRAM

    if ((Line[i].Valid) && (Line[i].Dirty)) { // line has dirty block
      MemAddress = Line[i].Tag << index_bits;
      MemAddress = (MemAddress + index) << 1;       // each set have 2 lines (2 positions in memory)
      MemAddress = (MemAddress+i) << offset_bits;   // get address of the block in memory
      accessDRAM(MemAddress, &(L2Cache[(index+i)*BLOCK_SIZE]), MODE_WRITE); // then write back old block
    }

    memcpy(&(L2Cache[(index+i)*BLOCK_SIZE]), TempBlock,
          BLOCK_SIZE); // copy new block to cache line
    Line[i].Valid = 1;
    Line[i].Tag = Tag;
    Line[i].Dirty = 0;
  } // if miss, then replaced with the correct block

  else {  //if the block is present find which one have the same tag
    if (Line[0].Valid && Line[0].Tag == Tag) {
      Line[0].LRU = 1;  //will be used in this access
      Line[1].LRU = 0;  //not used in this access
      i = 0;
    } else {
      Line[0].LRU = 0;  //not used in this access
      Line[1].LRU = 1;  //will be used in this access
      i = 1;
    }
    MemAddress = (MemAddress+i) << offset_bits; // get address of the block in memory
  }

  if (mode == MODE_READ) {    // read data from cache line
    if (!(address % BLOCK_SIZE)) { // even word on block
      memcpy(data, &(L2Cache[(index+i)*BLOCK_SIZE]), WORD_SIZE);
    } else { // odd word on block
      memcpy(data, &(L2Cache[(index+i)*BLOCK_SIZE+offset]), WORD_SIZE);
    }
    time += L2_READ_TIME;
  }

  if (mode == MODE_WRITE) { // write data from cache line
    if (!(address % BLOCK_SIZE)) {   // even word on block
      memcpy(&(L2Cache[(index+i)*BLOCK_SIZE]), data, WORD_SIZE);
    } else { // odd word on block
      memcpy(&(L2Cache[(index+i)*BLOCK_SIZE+offset]), data, WORD_SIZE);
    }
    time += L2_WRITE_TIME;
    Line[i].Dirty = 1;
  }
}

void read(uint32_t address, uint8_t *data) {
  accessL1(address, data, MODE_READ);
}

void write(uint32_t address, uint8_t *data) {
  accessL1(address, data, MODE_WRITE);
}
