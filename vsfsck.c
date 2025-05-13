#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define BLOCK_SIZE 4096
#define TOTAL_BLOCKS 64
#define INODE_SIZE 256
#define INODES_PER_BLOCK (BLOCK_SIZE / INODE_SIZE)
#define INODE_TABLE_BLOCKS 5
#define INODE_COUNT (INODE_TABLE_BLOCKS * INODES_PER_BLOCK)

#define MAGIC 0xD34D

typedef struct {
    uint16_t magic;
    uint32_t block_size;
    uint32_t total_blocks;
    uint32_t inode_bitmap_block;
    uint32_t data_bitmap_block;
    uint32_t inode_table_start;
    uint32_t data_block_start;
    uint32_t inode_size;
    uint32_t inode_count;
    uint8_t reserved[4058];
} __attribute__((packed)) Superblock;

typedef struct {
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t atime;
    uint32_t ctime;
    uint32_t mtime;
    uint32_t dtime;
    uint32_t links_count;
    uint32_t blocks;
    uint32_t direct_block;
    uint32_t single_indirect;
    uint32_t double_indirect;
    uint32_t triple_indirect;
    uint8_t reserved[156];
} __attribute__((packed)) Inode;

Superblock sb;
Inode inodes[INODE_COUNT];
uint8_t inode_bitmap[BLOCK_SIZE];
uint8_t data_bitmap[BLOCK_SIZE];
uint8_t data_block_owner[TOTAL_BLOCKS];

FILE* img;

void read_block(int block_num, void* buf) {
    fseek(img, block_num * BLOCK_SIZE, SEEK_SET);
    fread(buf, BLOCK_SIZE, 1, img);
}

void write_block(int block_num, void* buf) {
    fseek(img, block_num * BLOCK_SIZE, SEEK_SET);
    fwrite(buf, BLOCK_SIZE, 1, img);
}

void load_data() {
    read_block(0, &sb);
    read_block(sb.inode_bitmap_block, inode_bitmap);
    read_block(sb.data_bitmap_block, data_bitmap);
    for (int i = 0; i < INODE_TABLE_BLOCKS; i++) {
        uint8_t block[BLOCK_SIZE];
        read_block(sb.inode_table_start + i, block);
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            memcpy(&inodes[i * INODES_PER_BLOCK + j], block + j * INODE_SIZE, INODE_SIZE);
        }
    }
}

void save_inodes() {
    for (int i = 0; i < INODE_TABLE_BLOCKS; i++) {
        uint8_t block[BLOCK_SIZE];
        for (int j = 0; j < INODES_PER_BLOCK; j++) {
            memcpy(block + j * INODE_SIZE, &inodes[i * INODES_PER_BLOCK + j], INODE_SIZE);
        }
        write_block(sb.inode_table_start + i, block);
    }
}

void fix_superblock() {
    printf("[INFO] Checking superblock...\n");
    int FIXING = 0;
    if (sb.magic != MAGIC) {
        printf("[FIXING] Magic number corrected from 0x%X to 0x%X\n", sb.magic, MAGIC);
        sb.magic = MAGIC; FIXING = 1;
    }
    if (sb.block_size != BLOCK_SIZE) {
        printf("[FIXING] Block size corrected from %u to %u\n", sb.block_size, BLOCK_SIZE);
        sb.block_size = BLOCK_SIZE; FIXING = 1;
    }
    if (sb.total_blocks != TOTAL_BLOCKS) {
        printf("[FIXING] Total blocks corrected from %u to %u\n", sb.total_blocks, TOTAL_BLOCKS);
        sb.total_blocks = TOTAL_BLOCKS; FIXING = 1;
    }
    if (sb.inode_bitmap_block != 1 || sb.data_bitmap_block != 2 ||
        sb.inode_table_start != 3 || sb.data_block_start != 8) {
        printf("[FIXING] Corrected superblock block pointers\n");
        sb.inode_bitmap_block = 1;
        sb.data_bitmap_block = 2;
        sb.inode_table_start = 3;
        sb.data_block_start = 8;
        FIXING = 1;
    }
    if (sb.inode_size != INODE_SIZE) {
        printf("[FIXING] Inode size corrected from %u to %u\n", sb.inode_size, INODE_SIZE);
        sb.inode_size = INODE_SIZE; FIXING = 1;
    }
    if (sb.inode_count != INODE_COUNT) {
        printf("[FIXING] Inode count corrected from %u to %u\n", sb.inode_count, INODE_COUNT);
        sb.inode_count = INODE_COUNT; FIXING = 1;
    }
    if (FIXING) write_block(0, &sb);
    else printf("[OK] Superblock is valid\n");
}

void fix_inode_bitmap() {
    printf("[INFO] Checking inode bitmap...\n");
    for (int i = 0; i < INODE_COUNT; i++) {
        int valid = (inodes[i].links_count > 0 && inodes[i].dtime == 0);
        int marked = (inode_bitmap[i / 8] >> (i % 8)) & 1;
        if (valid && !marked) {
            inode_bitmap[i / 8] |= (1 << (i % 8));
            printf("[FIXING] Set inode %d as used in bitmap\n", i);
        } else if (!valid && marked) {
            inode_bitmap[i / 8] &= ~(1 << (i % 8));
            printf("[FIXING] Cleared unused inode %d from bitmap\n", i);
        }
    }
    write_block(sb.inode_bitmap_block, inode_bitmap);
}

void fix_data_bitmap_and_blocks() {
    printf("[INFO] Checking data bitmap and block usage...\n");
    memset(data_block_owner, 0, sizeof(data_block_owner));
    int found_duplicates = 0;
    int found_bad_blocks = 0;

    for (int i = 0; i < INODE_COUNT; i++) {
        if (inodes[i].links_count == 0 || inodes[i].dtime != 0) continue;
        uint32_t blk = inodes[i].direct_block;

        if (blk == 0) continue; // unused block

        if (blk >= sb.data_block_start && blk < TOTAL_BLOCKS) {
            if (data_block_owner[blk] == 0) {
                data_block_owner[blk] = i + 1; // first owner
                data_bitmap[blk / 8] |= (1 << (blk % 8));
            } else {
                printf("[ERROR] Data block %u referenced by inode %d and inode %d\n", blk, data_block_owner[blk]-1, i);
                found_duplicates = 1;
                inodes[i].direct_block = 0;
                printf("[FIXING] Removed duplicate reference from inode %d\n", i);
            }
        } else {
            printf("[ERROR] Inode %d references invalid data block %u\n", i, blk);
            found_bad_blocks = 1;
            inodes[i].direct_block = 0;
            printf("[FIXING] Cleared invalid block reference in inode %d\n", i);
        }
    }

    for (int i = sb.data_block_start; i < TOTAL_BLOCKS; i++) {
        int marked = (data_bitmap[i / 8] >> (i % 8)) & 1;
        if (!data_block_owner[i] && marked) {
            data_bitmap[i / 8] &= ~(1 << (i % 8));
            printf("[FIXING] Cleared unused block %d from bitmap\n", i);
        }
    }

    if (!found_duplicates) {
        printf("[OK] No duplicate blocks found\n");
    } else {
        printf("[FIXING] Duplicate blocks found\n");
    }

    if (!found_bad_blocks) {
        printf("[OK] No bad blocks found\n");
    } else {
        printf("[FIXING] Bad blocks found\n");
    }

    write_block(sb.data_bitmap_block, data_bitmap);
    save_inodes();
}

int main() {
    img = fopen("vsfs.img", "r+b");
    if (!img) { perror("vsfs.img"); return 1; }

    load_data();
    fix_superblock();
    fix_inode_bitmap();
    fix_data_bitmap_and_blocks();

    fclose(img);
    printf("[SUMMARY] File system FIXING and saved.\n");
    return 0;
}