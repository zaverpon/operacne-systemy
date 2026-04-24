#include "wrapper.h"

#define HEADER_SIZE 5
#define FLAG_FREE 1
#define FLAG_USED 0
#define FAIL	(-1)
#define OK  	(0)

static int read_int(int addr) {
    int b0 = mread(addr);
    int b1 = mread(addr + 1);
    int b2 = mread(addr + 2);
    int b3 = mread(addr + 3);

    return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24);
}

static void write_int(int addr, int value) {
    mwrite(addr, value & 0xFF);
    mwrite(addr + 1, (value >> 8) & 0xFF);
    mwrite(addr + 2, (value >> 16) & 0xFF);
    mwrite(addr + 3, (value >> 24) & 0xFF);
}

static int block_size(int block_addr) {
    return read_int(block_addr);
}

static void set_block_size(int block_addr, int size) {
    write_int(block_addr, size);
}

static int block_is_free(int block_addr) {
    return mread(block_addr + 4);
}

static void set_block_free(int block_addr, int is_free) {
    mwrite(block_addr + 4, is_free);
}

static int block_data_addr(int block_addr) {
    return block_addr + HEADER_SIZE;
}

static int next_block_addr(int block_addr) {
    return block_addr + HEADER_SIZE + block_size(block_addr);
}

static int block_is_valid(int block_addr) {
    if (block_addr < 0) {
        return 0;
    }
    if (block_addr + HEADER_SIZE > (int)msize()) {
        return 0;
    }

    int size = block_size(block_addr);
    if (size < 0) {
        return 0;
    }
    if (block_addr + HEADER_SIZE + size > (int)msize()) {
        return 0;
    }

    int flag = block_is_free(block_addr);
    if (flag != FLAG_FREE && flag != FLAG_USED) {
        return 0;
    }

    return 1;
}

void my_init(void) {
    if ((int)msize() < HEADER_SIZE) {
        return;
    }

    set_block_size(0, (int)msize() - HEADER_SIZE);
    set_block_free(0, FLAG_FREE);
}

int my_alloc(unsigned int size) {
    if (size == 0) {
        return FAIL;
    }
    if (size > msize()) {
        return FAIL;
    }

    int block = 0;

    while (block + HEADER_SIZE <= (int)msize()) {
        if (!block_is_valid(block)) {
            return FAIL;
        }

        int curr_size = block_size(block);

        if (block_is_free(block) && curr_size >= (int)size) {
            if (curr_size >= (int)size + HEADER_SIZE + 1) {
                int old_size = curr_size;
                int new_block = block + HEADER_SIZE + (int)size;
                int new_size = old_size - (int)size - HEADER_SIZE;

                set_block_size(block, (int)size);
                set_block_free(block, FLAG_USED);

                set_block_size(new_block, new_size);
                set_block_free(new_block, FLAG_FREE);
            } else {
                set_block_free(block, FLAG_USED);
            }

            return block_data_addr(block);
        }

        int next = next_block_addr(block);
        if (next <= block) {
            return FAIL;
        }
        block = next;
    }

    return FAIL;
}

int my_free(unsigned int addr) {
    if (addr < HEADER_SIZE || addr >= msize()) {
        return FAIL;
    }

    int prev = -1;
    int block = 0;

    while (block + HEADER_SIZE <= (int)msize()) {
        if (!block_is_valid(block)) {
            return FAIL;
        }

        if ((unsigned int)block_data_addr(block) == addr) {
            if (block_is_free(block)) {
                return FAIL;
            }

            set_block_free(block, FLAG_FREE);

            int right = next_block_addr(block);
            if (right + HEADER_SIZE <= (int)msize() && block_is_valid(right) && block_is_free(right)) {
                int new_size = block_size(block) + HEADER_SIZE + block_size(right);
                set_block_size(block, new_size);
            }

            if (prev != -1 && block_is_valid(prev) && block_is_free(prev)) {
                int new_size = block_size(prev) + HEADER_SIZE + block_size(block);
                set_block_size(prev, new_size);
            }

            return OK;
        }

        prev = block;

        int next = next_block_addr(block);
        if (next <= block) {
            return FAIL;
        }
        block = next;
    }

    return FAIL;
}