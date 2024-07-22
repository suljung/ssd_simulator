#include <stdio.h>
#include <stdlib.h>
#include <limits.h>  // INT_MAX를 사용하기 위해 추가

// SSD 파라미터 정의
#define PAGE_SIZE 4096  // 4KiB
#define BLOCK_SIZE (4L * 1024 * 1024)  // 4MiB
#define SSD_SIZE (8L * 1024 * 1024 * 1024)  // 8GiB
#define PAGES_PER_BLOCK (BLOCK_SIZE / PAGE_SIZE)
#define BLOCKS_PER_SSD (SSD_SIZE / BLOCK_SIZE)
#define LOGICAL_SSD_SIZE (8L * 1000 * 1000 * 1000) // 8GB
#define MAX_LBA_NUM (LOGICAL_SSD_SIZE / PAGE_SIZE)
#define GC_THRESHOLD 3

// 페이지 구조체 정의
typedef struct {
    int is_valid;  // 유효성 정보를 페이지 자체에서 저장 (0: invalid, 1: valid)
    struct {
        unsigned long lba;  // LBA를 OOB에 저장
    } oob;  // OOB 영역화 
} Page;

// 블록 구조체 정의
typedef struct {
    Page *pages;  // 페이지 배열, 동적메모리 할당+Page 구조체와 연동을 위해 포인터 사용
    int free_page_offset;  // 다음 자유 페이지의 오프셋
    int is_full;  // 블록이 가득 찼는지 여부 (0: not full, 1: full)
    int valid_page_count;  // 유효 페이지의 수
} Block;

// SSD 구조체 정의
typedef struct {
    Block *blocks;  // 블록 배열
    int *free_block_queue;  // 자유 블록 큐
    int free_block_front;  // 큐의 front 인덱스
    int free_block_rear;  // 큐의 rear 인덱스
    int free_block_count;  // 자유 블록의 수
    unsigned long *mapping_table;  // 매핑 테이블
    unsigned long lba_num;  // 기록된 LBA의 개수
    unsigned long gc_write;  // 가비지 컬렉션에 의한 쓰기 횟수
    unsigned long user_write;  // 사용자에 의한 쓰기 횟수
} SSD;

// 큐 초기화 함수
void init_queue(SSD *ssd) {
    ssd->free_block_queue = (int *)malloc(sizeof(int) * BLOCKS_PER_SSD);
    ssd->free_block_front = 0;
    ssd->free_block_rear = -1;
    ssd->free_block_count = 0;
}

// 큐에 요소를 추가하는 함수
void enqueue(SSD *ssd, int block_index) {
    ssd->free_block_rear = (ssd->free_block_rear + 1) % BLOCKS_PER_SSD;
    ssd->free_block_queue[ssd->free_block_rear] = block_index;
    ssd->free_block_count++;
}

// 큐에서 요소를 제거하는 함수
int dequeue(SSD *ssd) {
    int block_index = ssd->free_block_queue[ssd->free_block_front];
    ssd->free_block_front = (ssd->free_block_front + 1) % BLOCKS_PER_SSD;
    ssd->free_block_count--;
    return block_index;
}

// SSD 초기화 함수
SSD* init_ssd() {
    SSD *ssd = (SSD *)malloc(sizeof(SSD));

    ssd->blocks = (Block *)malloc(sizeof(Block) * BLOCKS_PER_SSD);

    int i, j;
    for (i = 0; i < BLOCKS_PER_SSD; i++) {
        ssd->blocks[i].pages = (Page *)malloc(sizeof(Page) * PAGES_PER_BLOCK);
        ssd->blocks[i].free_page_offset = 0;  // 초기 자유 페이지 오프셋
        ssd->blocks[i].is_full = 0;  // 초기에는 블록이 가득 차지 않음
        ssd->blocks[i].valid_page_count = 0;  // 초기 유효 페이지 수

        for (j = 0; j < PAGES_PER_BLOCK; j++) {
            ssd->blocks[i].pages[j].is_valid = 0;  // 모든 페이지를 초기에는 invalid로 설정
        }
    }

    init_queue(ssd);

    // 모든 블록을 자유 블록 큐에 추가
    for (i = 0; i < BLOCKS_PER_SSD; i++) {
        enqueue(ssd, i);
    }

    // 매핑 테이블 초기화
    ssd->mapping_table = (unsigned long *)malloc(sizeof(unsigned long) * (SSD_SIZE / PAGE_SIZE));  // mapping table의 용량이 부족할 일은 없으니까 ssd_size / page_size로 계산한다.

    for (i = 0; i < (SSD_SIZE / PAGE_SIZE); i++) {
        ssd->mapping_table[i] = -1;  // 초기에는 모든 매핑을 -1로 설정
    }

    ssd->lba_num = 0;  // 초기 LBA 기록 개수는 0
    ssd->gc_write = 0;  // 초기 gc_write는 0
    ssd->user_write = 0;  // 초기 user_write는 0

    return ssd;
}

// SSD 메모리 해제 함수
void free_ssd(SSD *ssd) {
    int i;
    for (i = 0; i < BLOCKS_PER_SSD; i++) {
        free(ssd->blocks[i].pages);
    }
    free(ssd->blocks);
    free(ssd->free_block_queue);
    free(ssd->mapping_table);
    free(ssd);
}

// 자유 페이지를 찾는 함수
void find_free_page(SSD *ssd, unsigned long lba, int is_gc_write) {
    int block_index = ssd->free_block_queue[ssd->free_block_front];
    Block *block = &ssd->blocks[block_index];

    int page_offset = block->free_page_offset;  
    block->pages[page_offset].is_valid = 1;  // 페이지를 valid로 설정
    block->pages[page_offset].oob.lba = lba;  // OOB에 LBA 기록
    block->free_page_offset++;

    block->valid_page_count++;

    if (block->free_page_offset == PAGES_PER_BLOCK) {
        block->is_full = 1;
        dequeue(ssd);  // 블록이 가득 찼으므로 자유 블록 큐에서 제거
    }

    int physical_page_index = block_index * PAGES_PER_BLOCK + page_offset;  // 절대 페이지 인덱스 반환
    ssd->mapping_table[lba] = physical_page_index;  // 매핑 테이블에 기록

    if (is_gc_write) {
        ssd->gc_write++;  // 가비지 컬렉션에 의한 쓰기 증가
    } else {
        ssd->user_write++;  // 사용자에 의한 쓰기 증가
    }
}


// 가장 유효 페이지가 적은 블록을 찾는 함수
int find_victim_block(SSD *ssd) {
    int min_valid_pages = INT_MAX;
    int victim_block_index = -1;

    int i;
    for (i = 0; i < BLOCKS_PER_SSD; i++) {
        if (ssd->blocks[i].is_full && ssd->blocks[i].valid_page_count < min_valid_pages) {
            min_valid_pages = ssd->blocks[i].valid_page_count;
            victim_block_index = i;
        }
    }

    return victim_block_index;
}

// 가비지 컬렉션 함수
void gc(SSD *ssd) {
    int victim_block_index = find_victim_block(ssd);
    Block *victim_block = &ssd->blocks[victim_block_index];

    int i;
    for (i = 0; i < PAGES_PER_BLOCK; i++) {
        if (victim_block->pages[i].is_valid) {
            unsigned long lba = victim_block->pages[i].oob.lba;
            find_free_page(ssd, lba, 1);  // 가비지 컬렉션 쓰기로 기록
        }
    }

    // victim 블록의 모든 페이지를 invalid로 설정
    for (i = 0; i < PAGES_PER_BLOCK; i++) {
        victim_block->pages[i].is_valid = 0;
    }

    victim_block->free_page_offset = 0;
    victim_block->is_full = 0;
    victim_block->valid_page_count = 0;

    // victim 블록을 자유 블록 큐에 추가
    enqueue(ssd, victim_block_index);
}

// LBA를 처리하는 함수
void process_lba(SSD *ssd, unsigned long lba) {
    if (ssd->mapping_table[lba] != -1) {  // 기존 LBA가 존재하면
        int old_page_index = ssd->mapping_table[lba];
        int old_block_index = old_page_index / PAGES_PER_BLOCK;
        int old_page_offset = old_page_index % PAGES_PER_BLOCK;

        // 기존 페이지를 invalid로 설정
        ssd->blocks[old_block_index].pages[old_page_offset].is_valid = 0;
        ssd->blocks[old_block_index].valid_page_count--;
        ssd->lba_num--;  // 기존 LBA가 invalid되면 lba_num 감소
    }

    // 새로운 페이지에 LBA 기록
    find_free_page(ssd, lba, 0);
    ssd->lba_num++;  // 새로운 LBA가 기록되면 lba_num 증가

    // 가비지 컬렉션 체크
    if (ssd->free_block_count <= GC_THRESHOLD) {
        gc(ssd);
    }
}

// I/O 요청을 저장할 구조체 정의
typedef struct {
    double timestamp;
    int io_type; // 0: READ, 1: WRITE, 2: X, 3: TRIM
    unsigned long lba;
    unsigned int size;
    unsigned int stream_number;
} IORequest;

int main() {
    FILE *file = fopen("test-fio-small", "r");

    // IORequest 구조체를 위한 메모리 할당
    IORequest *request = (IORequest *)malloc(sizeof(IORequest));

    SSD *ssd = init_ssd();

    unsigned long processed_size = 0;
    unsigned long total_processed_size = 0;  // 전체 처리된 사이즈를 추적

    // 파일에서 줄을 읽어 구조체에 저장하고 처리
    while (fscanf(file, "%lf %d %lu %u %u", &request->timestamp, 
                  &request->io_type, &request->lba, 
                  &request->size, &request->stream_number) != EOF) {
        // 쓰기 요청만 처리
        if (request->io_type == 1) {
            process_lba(ssd, request->lba);
            processed_size += PAGE_SIZE;
            total_processed_size += PAGE_SIZE;

            // 8GiB의 배수가 될 때 WAF와 Utilization을 출력
            if (processed_size >= SSD_SIZE) {
                double WAF = (double)(ssd->gc_write + ssd->user_write) / ssd->user_write;
                double Utilization = (double)ssd->lba_num / MAX_LBA_NUM;

                printf("[Progress: %lu GiB] WAF: %.3f, Utilization: %.3f\n", 8*(total_processed_size / SSD_SIZE), WAF, Utilization);
                
                // 다음 8GiB를 위한 processed_size 초기화
                processed_size = 0;
            }
        }
    }

    // 메모리 해제
    free(request);
    free_ssd(ssd);
    fclose(file);

    return 0;
}
