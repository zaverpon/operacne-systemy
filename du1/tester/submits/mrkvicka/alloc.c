#include "wrapper.h"

#define HEADER_SIZE 4
#define ALLOC_HEADER_SIZE 4
#define FREE_NODE_SIZE 4

#define FLAG_FREE 1
#define FLAG_USED 0

#define FAIL (-1)
#define OK 0

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

static int block_raw(int block_addr) {
    return read_int(block_addr);
}

static void set_block_raw(int block_addr, int raw) {
    write_int(block_addr, raw);
}

static int block_size(int block_addr) {
    return block_raw(block_addr) >> 1;
}

static int block_is_free(int block_addr) {
    return block_raw(block_addr) & 1;
}

static void set_block_size(int block_addr, int size) {
    int flag = block_is_free(block_addr);
    set_block_raw(block_addr, (size << 1) | flag);
}

static void set_block_free(int block_addr, int is_free) {
    int size = block_size(block_addr);
    set_block_raw(block_addr, (size << 1) | is_free);
}

static int block_data_addr(int block_addr) {
    return block_addr + HEADER_SIZE;
}

static int next_block_addr(int block_addr) {
    return block_addr + HEADER_SIZE + block_size(block_addr);
}

static int block_is_valid(int block_addr) {
    if (block_addr < ALLOC_HEADER_SIZE) {
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

static int get_free_head(void) {
    return read_int(0);
}

static void set_free_head(int addr) {
    write_int(0, addr);
}

static int get_next_free(int block_addr) {
    return read_int(block_addr + HEADER_SIZE);
}

static void set_next_free(int block_addr, int next) {
    write_int(block_addr + HEADER_SIZE, next);
}

static void remove_from_free_list(int block_addr) {
    int prev = FAIL;
    int curr = get_free_head();

    while (curr != FAIL) {
        if (curr == block_addr) {
            int next = get_next_free(curr);

            if (prev == FAIL) {
                set_free_head(next);
            } else {
                set_next_free(prev, next);
            }
            return;
        }

        prev = curr;
        curr = get_next_free(curr);
    }
}

static void insert_into_free_list_front(int block_addr) {
    set_next_free(block_addr, get_free_head());
    set_free_head(block_addr);
}

void my_init(void) {
    if ((int)msize() < ALLOC_HEADER_SIZE + HEADER_SIZE + FREE_NODE_SIZE) {
        return;
    }

    int first_block = ALLOC_HEADER_SIZE;
    int first_size = (int)msize() - ALLOC_HEADER_SIZE - HEADER_SIZE;

    set_free_head(first_block);
    set_block_raw(first_block, (first_size << 1) | FLAG_FREE);
    set_next_free(first_block, FAIL);
}

int my_alloc(unsigned int size) {
    if (size == 0) {
        return FAIL;
    }

    int need = (int)size;
    if (need < FREE_NODE_SIZE) {
        need = FREE_NODE_SIZE;
    }

    int prev_free = FAIL;
    int block = get_free_head();

    while (block != FAIL) {
        if (!block_is_valid(block) || !block_is_free(block)) {
            return FAIL;
        }

        int curr_size = block_size(block);
        int next_free = get_next_free(block);

        if (curr_size >= need) {
            if (curr_size >= need + HEADER_SIZE + FREE_NODE_SIZE) {
                int old_size = curr_size;
                int new_block = block + HEADER_SIZE + need;
                int new_size = old_size - need - HEADER_SIZE;

                set_block_raw(block, (need << 1) | FLAG_USED);

                set_block_raw(new_block, (new_size << 1) | FLAG_FREE);
                set_next_free(new_block, next_free);

                if (prev_free == FAIL) {
                    set_free_head(new_block);
                } else {
                    set_next_free(prev_free, new_block);
                }
            } else {
                if (prev_free == FAIL) {
                    set_free_head(next_free);
                } else {
                    set_next_free(prev_free, next_free);
                }

                set_block_free(block, FLAG_USED);
            }

            return block_data_addr(block);
        }

        prev_free = block;
        block = next_free;
    }

    return FAIL;
}

int my_free(unsigned int addr) {
    if (addr < (unsigned int)(ALLOC_HEADER_SIZE + HEADER_SIZE) || addr >= msize()) {
        return FAIL;
    }

    int prev_phys = FAIL;
    int block = ALLOC_HEADER_SIZE;

    while (block + HEADER_SIZE <= (int)msize()) {
        if (!block_is_valid(block)) {
            return FAIL;
        }

        if ((unsigned int)block_data_addr(block) == addr) {
            if (block_is_free(block)) {
                return FAIL;
            }

            int final_block = block;
            int final_size = block_size(block);

            int right = next_block_addr(block);
            if (right + HEADER_SIZE <= (int)msize() && block_is_valid(right) && block_is_free(right)) {
                remove_from_free_list(right);
                final_size += HEADER_SIZE + block_size(right);
            }

            if (prev_phys != FAIL && block_is_valid(prev_phys) && block_is_free(prev_phys)) {
                int merged_size = block_size(prev_phys) + HEADER_SIZE + final_size;
                set_block_raw(prev_phys, (merged_size << 1) | FLAG_FREE);
                return OK;
            } else {
                set_block_raw(final_block, (final_size << 1) | FLAG_FREE);
                insert_into_free_list_front(final_block);
                return OK;
            }
        }

        prev_phys = block;

        int next = next_block_addr(block);
        if (next <= block) {
            return FAIL;
        }
        block = next;
    }

    return FAIL;
}