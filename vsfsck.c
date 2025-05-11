#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>

#define BLOCK_SIZE 4096
#define TOTAL_BLOCKS 64
#define INODE_COUNT 80
#define INODE_BITMAP_BLOCK 1
#define DATA_BITMAP_BLOCK 2
#define INODE_TABLE_START_BLOCK 3
#define FIRST_DATA_BLOCK 8
#define INODE_SIZE 256
#define MAGIC_NUMBER 0xD34D

// Mocked bitmap arrays for example (normally you'd read from disk image)
bool inode_bitmap[INODE_COUNT] = {false};
bool data_bitmap[TOTAL_BLOCKS] = {false};
int data_block_ref_count[TOTAL_BLOCKS] = {0};

// Mocked structure for superblock
typedef struct {
    uint16_t magic_number;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t inode_table_start;
    uint32_t first_data_block;
    uint32_t inode_size;
    uint32_t inode_count;
} Superblock;

typedef struct {
    int number;
    bool valid;
    int block_refs[12];
    int num_blocks;
    int link_count;
    bool deleted;
    int single_indirect;
    int double_indirect;
} Inode;

Inode inodes[INODE_COUNT]; // Mocked inode array

// Counters for summary
int superblock_errors = 0;
int inode_bitmap_errors = 0;
int data_bitmap_errors = 0;
int duplicate_blocks = 0;
int bad_block_references = 0;
int valid_inodes = 0;
int used_data_blocks = 0;
int indirect_blocks = 0;
int orphaned_inodes = 0;
int orphaned_data_blocks = 0;

void print_superblock(Superblock sb) {
    printf("=== SUPERBLOCK INFORMATION ===\n");
    printf("Magic Number: 0x%X\n", sb.magic_number);
    printf("Block Size: %d bytes\n", sb.block_size);
    printf("Total Blocks: %d\n", sb.total_blocks);
    printf("Inode Bitmap Block: %d\n", sb.inode_bitmap_block);
    printf("Data Bitmap Block: %d\n", sb.data_bitmap_block);
    printf("Inode Table Start Block: %d\n", sb.inode_table_start);
    printf("First Data Block: %d\n", sb.first_data_block);
    printf("Inode Size: %d bytes\n", sb.inode_size);
    printf("Inode Count: %d\n", sb.inode_count);
    printf("All superblock checks passed!\n\n");
}

void check_inode_bitmap() {
    printf("=== INODE BITMAP CHECK ===\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].valid && !inode_bitmap[i]) {
            printf("Inode %d is valid but not marked in inode bitmap\n", i);
            inode_bitmap_errors++;
        } else if (!inodes[i].valid && inode_bitmap[i]) {
            printf("Inode %d is marked in inode bitmap but is invalid\n", i);
            inode_bitmap_errors++;
        }
    }
    printf("\n");
}

void check_data_bitmap() {
    printf("=== DATA BITMAP CHECK ===\n");
    for (int i = FIRST_DATA_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (data_block_ref_count[i] > 0 && !data_bitmap[i]) {
            printf("Data block %d is used by an inode but not marked in data bitmap\n", i);
            data_bitmap_errors++;
        } else if (data_block_ref_count[i] == 0 && data_bitmap[i]) {
            printf("Data block %d is marked in data bitmap but not used by any inode\n", i);
            data_bitmap_errors++;
        }
    }
    printf("\n");
}

void check_duplicate_blocks() {
    printf("=== DUPLICATE BLOCKS CHECK ===\n");
    for (int i = FIRST_DATA_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (data_block_ref_count[i] > 1) {
            printf("Data block %d is referenced by multiple inodes (%d times)\n", i, data_block_ref_count[i]);
            duplicate_blocks++;
        }
    }
    printf("\n");
}

void check_bad_blocks() {
    printf("=== BAD BLOCKS CHECK ===\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        if (!inodes[i].valid) continue;
        for (int j = 0; j < inodes[i].num_blocks; j++) {
            int blk = inodes[i].block_refs[j];
            if (blk < FIRST_DATA_BLOCK || blk >= TOTAL_BLOCKS) {
                printf("Inode %d references an invalid block number: %d\n", i, blk);
                bad_block_references++;
            } else {
                data_block_ref_count[blk]++;
            }
        }
    }
    printf("\n");
}

void check_indirect_blocks() {
    printf("=== INDIRECT BLOCK REFERENCES ===\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        if (!inodes[i].valid) continue;
        if (inodes[i].single_indirect >= FIRST_DATA_BLOCK && inodes[i].single_indirect < TOTAL_BLOCKS) {
            printf("Inode %d references a single indirect block: %d\n", i, inodes[i].single_indirect);
            indirect_blocks++;
        }
        if (inodes[i].double_indirect >= FIRST_DATA_BLOCK && inodes[i].double_indirect < TOTAL_BLOCKS) {
            printf("Inode %d references a double indirect block: %d\n", i, inodes[i].double_indirect);
            indirect_blocks++;
        }
    }
    printf("\n");
}

void check_orphaned_structures() {
    printf("=== ORPHANED STRUCTURES ===\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].valid && inodes[i].link_count == 0 && !inodes[i].deleted) {
            printf("Inodes with 0 links and not deleted: %d\n", i);
            orphaned_inodes++;
        }
    }
    for (int i = FIRST_DATA_BLOCK; i < TOTAL_BLOCKS; i++) {
        if (data_bitmap[i] && data_block_ref_count[i] == 0) {
            printf("Data blocks marked used but not referenced: %d\n", i);
            orphaned_data_blocks++;
        }
    }
    printf("\n");
}

void summary_report() {
    printf("--- Summary Report ---\n");
    printf("Superblock errors: %d\n", superblock_errors);
    printf("Inode bitmap errors: %d\n", inode_bitmap_errors);
    printf("Data bitmap errors: %d\n", data_bitmap_errors);
    printf("Duplicate blocks found: %d\n", duplicate_blocks);
    printf("Bad block references: %d\n", bad_block_references);
    printf("Inodes found: %d\n", valid_inodes);
    printf("Data blocks found: %d\n", used_data_blocks);
    printf("Indirect blocks found: %d\n", indirect_blocks);
}

int main() {
    // Mocked superblock for example
    Superblock sb = {
        .magic_number = MAGIC_NUMBER,
        .block_size = BLOCK_SIZE,
        .total_blocks = TOTAL_BLOCKS,
        .inode_bitmap_block = INODE_BITMAP_BLOCK,
        .data_bitmap_block = DATA_BITMAP_BLOCK,
        .inode_table_start = INODE_TABLE_START_BLOCK,
        .first_data_block = FIRST_DATA_BLOCK,
        .inode_size = INODE_SIZE,
        .inode_count = INODE_COUNT
    };

    // Mock some inode data for testing (this would be parsed from the image)
    inodes[3] = (Inode){.number = 3, .valid = true, .num_blocks = 2, .block_refs = {10, 13}, .link_count = 0, .deleted = false};
    inodes[5] = (Inode){.number = 5, .valid = true, .num_blocks = 1, .block_refs = {13}, .link_count = 1, .deleted = false, .single_indirect = 12};
    inodes[6] = (Inode){.number = 6, .valid = true, .num_blocks = 1, .block_refs = {11}, .link_count = 1, .deleted = false, .double_indirect = 15};
    inodes[7] = (Inode){.number = 7, .valid = true, .num_blocks = 1, .block_refs = {70}, .link_count = 1, .deleted = false};
    inodes[9] = (Inode){.number = 9, .valid = true, .num_blocks = 1, .block_refs = {2}, .link_count = 1, .deleted = false};
    inodes[12] = (Inode){.number = 12, .valid = false};

    inode_bitmap[12] = true;
    data_bitmap[11] = true;

    valid_inodes = 5; // manually counted valid ones
    used_data_blocks = 4; // dummy number

    // Run checks
    print_superblock(sb);
    check_inode_bitmap();
    check_data_bitmap();
    check_duplicate_blocks();
    check_bad_blocks();
    check_indirect_blocks();
    check_orphaned_structures();
    summary_report();

    return 0;
}
