/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};


// find_fit 까지만 진행하였습니다.

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))
// https://velog.io/@seanlion/malloclab#malloc-%EA%B5%AC%ED%98%84
// 가용 리스트 이미지: https://user-images.githubusercontent.com/61036124/105174781-68531800-5b66-11eb-921c-b6b3200f1052.jpg

// 기본 크기 정의 메크로
#define WSIZE 4 // word, header, footer size를 4바이트로 지정
#define DSIZE 8 // double word size를 8바이트로 지정
#define CHUNKSIZE (1<<12) // heap 확장시 사용되는 기본 크기(4kb)

// 크기 및 할당 비트 조작 메크로
#define PACK(size, alloc) ((size)| (alloc)) // 크기와 할당을 비트연산(or), True면 1  /  header나 footer 값 생성할때 쓰임
#define PUT(p,val) (*(unsigned int*)(p)=(int)(val)) // 블록 주소 담기(포인터, 워드 값)

// 블록 포인터 조작 메크로
#define HDRP(bp) ((char*)(bp) - WSIZE) // bp 상관없이 WSIZE 앞에 위치
#define FTRP(bp) ((char*)(bp) + GET_SIZE(HDRP(bp)) - DSIZE)

#define GET_SIZE(p) (GET(p) & ~0x7) // header, footer에서 블록 사이즈 가져오기
#define GET_ALLOC(p) (GET(p) & 0x1) // header에서 가용여부(할당비트 확인 (1인지 0인지))

#define NEXT_BLKP(bp) ((char*)(bp) + GET_SIZE(((char*)(bp)-WSIZE))) // 가용블럭의 사이즈를 구하고 기존 bp 포인터에 더해 다음 bp 포인터 위치 구하기
// -> 다음 블록의 블록 포인터 계산
#define PREV_BLKP(bp) ((char*)(bp) - GET_SIZE(((char*)(bp) - DSIZE))) // 이전 가용블럭의 사이즈를 구하고 bp 포인터에 빼서 이전의 bp 포인터 위치를 구하기
// -> 이전 블록의 블록 포인터 계산
static char *heap_listp;

/* 
 * mm_init - initialize the malloc package.
 */
int mm_init(void){ // heap 시작시 0부터 시작
    // 1. 초기 힙 공간 설정
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1){
        // heap_listp = mem_sbrk(4*WSIZE) -> heap을 4워드(4*WSIZE)만큼 확장하고, 확장 성공하면 heap_listp에 저장. 실패하면((void*)-1)) -1 반환
        return -1;
    }

    // 2. 초기 힙 구조 설정
    // PUT :  지정된 주소에 값을 저장
    // PACK : 크기와 할당 비트 결정
    PUT(heap_listp, 0); // 정렬을 위한 패딩
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더(8|1)(전체 블럭크기 | 할당여부)
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터(8|1)
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // 에필로그 헤더(0|1)
    heap_listp += (2*WSIZE); // heap_listp가 프롤로그 블록의 푸터를 가르키게
    // 실제 할당 가능한 첫번째 블록의 시작점 가르키게 됨

    // 4. 초기 가용 블록 생성
    // 초기 heap의 기본 크기 설정하기
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL) // extend heap 함수 호출해 CHUNKSIZE만큼 추가 힙 공간 요청, 초기 가용 블록 생성
    // extend_heap: 힙을 확장하고 새 가용 블록을 생성하는 함수
    // CHUNKSIZE/WSIZE: 힙 확장 크기 (워드 단위, 보통 4096/4 = 1024 워드)
        return -1;

    return 0; // 초기화 완료 -> 0 반환
}

// heap 확장하는 함수
static void *extend_heap(size_t words){ // 새 가용블록으로 heap 확장하기
    char *bp; // block pointer - 새로 할당될 블록의 시작점을 가르키는 포인터
    size_t size; // 실제 할당 크기를 바이트 단위로 저장할 변수
    size = (words % 2) ? (words+1) * WSIZE : words * WSIZE; // 사이즈 재할당(3항 연산자)
    // 더블 워드(8바이트) 정렬 보장
    // words가 홀수 -> 1을 더해 짝수로 만든 후 WSIZE를 곱하기
    // words가 짝수 -> 그대로 WSIZE 곱하기
    if ( (long)(bp = mem_sbrk(size)) == -1){ // sbrk로 힙을 size만큼 확장
        return NULL;
    }

    // free block 헤더 푸터, 에필로그 헤더 만들기
    PUT(HDRP(bp), PACK(size, 0)); // free block 헤더 만들기
    PUT(FTRP(bp), PACK(size, 0)); // free block 푸터 만들기
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0,1)); // 에필로그 헤더 만들기

    return coalesce(bp); // prev block이 free면, coalesce 하기
}

// 블록 합치는 함수
static void *coalesce(void *bp){
    size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 이전 블록의 할당 상태 확인
    // PREV_BLKP(bp)로 이전 블록으로 이동, FTRP로 그 블록의 푸터로 이동, GET_ALLOC으로 할당 상태 확인
    size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 다음 블록의 할당 상태 확인
    // NEXT_BLKP(bp)로 다음 블록으로 이동, HDRP로 그 블록의 헤더로 이동, GET_ALLOC으로 할당 상태 확인
    size_t size = GET_SIZE(HDRP(bp)); // 현재 블록의 사이즈 확인

    // 1. 이전과 다음 블록 모두 할당인 경우
    // 현재 블록 상태 할당에서 가용으로 변경
    if (prev_alloc && next_alloc){
        return bp;
    }
    // 2. 이전 블록 할당 상태, 다음 블록은 가용상태
    // 현재 블록과 다음 블록이 통합됨
    else if (prev_alloc && !next_alloc){
        size += GET_SIZE(HDRP(NEXT_BLKP(bp))); // 다음 블록의 헤더를 보고 그 블록의 크기만큼 지금 블록의 사이즈에 추가
        PUT(HDRP(bp), PACK(size, 0)); // 헤더 갱신
        PUT(FTRP(bp), PACK(size, 0)); // 푸터 갱신
    }
    // 3. 이전 블록 가용 상태, 다음 블록 할당 상태
    // 이전 블록은 현재 블록과 통합
    else if(!prev_alloc && next_alloc){
        size += GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0)); // 푸터에 먼저 조정하려는 크기로 상태 변경
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 현재 헤더에서 그 앞블록의 헤더 위치로 이동한 다음, 조정한 사이즈 추가
        bp = PREV_BLKP(bp); // bp를 그 앞블록의 헤더(늘린 블록의 헤더)로 이동
    }
    // 4. 이전 블록과 다음 블록 모두 가용 상태
    // 이전, 현재, 다음 3개의 블록 모두 하나의 가용 블록으로 통합
    else {
        size += GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // 이전 블록 헤더, 다음 블록 푸터까지로 사이즈 늘리기
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 헤더부터 앞으로 가서, 사이즈 넣고
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 푸터를 뒤에 가서 사이즈 넣기
        bp = PREV_BLKP(bp); // 그 전 블록으로 이동
    }
    return bp;
}
/* 
 * mm_malloc - Allocate a block by incrementing the brk pointer.
 *     Always allocate a block whose size is a multiple of the alignment.
 */
// void *mm_malloc(size_t size)
// {
//     int newsize = ALIGN(size + SIZE_T_SIZE);
//     void *p = mem_sbrk(newsize);
//     if (p == (void *)-1)
// 	return NULL;
//     else {
//         *(size_t *)p = size;
//         return (void *)((char *)p + SIZE_T_SIZE);
//     }
// }

void *mm_malloc(size_t size){ // 가용리스트에 블록 할당 하기
    size_t asize; // 블록 사이즈 조정
    size_t extendsize; // heap에 맞는 fit이 없으면 확장하기 위한 사이즈
    char *bp;

    if (size == 0) return NULL; // 인자로 받은 사이즈가 0이면 할당할 필요 없음

    if (size <= DSIZE){
        asize = 2 * DSIZE; // 헤더와 푸터 포함해서 블록 사이즈 다시 조정
        // DSIZE의 2배를 줌
    }
    else {
        asize = DSIZE* ( (size + (DSIZE) + (DSIZE - 1)) / DSIZE );
        // size보다 클 때, 블록이 가질 수 있는 크기 중에 최적화된 크기로 재조정.
    }
    // fit에 맞는 free 리스트 찾기
    if ((bp = find_fit(asize)) != NULL){
        place(bp, asize);
        return bp;
    }
    // fit이 맞는게 없으면, 메모리를 더 가져와 block을 위치시킨다
    extendsize = MAX(asize, CHUNKSIZE); // 처음에 세팅한 사이즈
    if ( (bp = extend_heap(extendsize / WSIZE)) == NULL){ // CHUNKSIZE - 블록을 늘리는 양, MAX_ADDR - 힙의 최대 크기
    // 인자로 단위 블록 수가 들어간다
        return NULL;
    }
    place(bp, asize); // 확장된 상태에서 asize 넣기
    return bp;
}

/*
* find_fit 함수 만들기
// 가용 리스트에 적절한 블록 찾는 함수
*/
static void *find_fit(size_t asize){ // first fit 검색 수행
    void *bp;
    for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)){
        // for문으로 에필로그 헤더까지. 사이즈가 0인 블럭(에필로그 헤더) 만나면 종료
        // 검사 조건 - 가용 블럭, 해당 블럭의 사이즈가 넣으려고 했던 사이즈보다 켜야함
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))){ // 이 블록이 가용하고, 내가 갖고 있는 asize를 담을 수 있다면
            return bp; // 찾았으므로 리턴
        }
    }
    return NULL; // 종료되면 null 리턴
}



// place 함수 - 할당할 블록을 찾은 위치에 넣기(필요시 분할)


/*
* free 함수 만들기
// 반환할 불록의 위치를 인자로 받음
// 해당 블록 가용여부 0 (가용)으로 만듦
*/

void mm_free(void *bp){
    size_t_size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0));
    PUT(FTRP(bp), PACK(size, 0));
    coalesce(bp);
}


/*
 * mm_realloc - Implemented simply in terms of mm_malloc and mm_free
 */
void *mm_realloc(void *ptr, size_t size)
{
    void *oldptr = ptr;
    void *newptr;
    size_t copySize;
    
    newptr = mm_malloc(size);
    if (newptr == NULL)
      return NULL;
    copySize = *(size_t *)((char *)oldptr - SIZE_T_SIZE);
    if (size < copySize)
      copySize = size;
    memcpy(newptr, oldptr, copySize);
    mm_free(oldptr);
    return newptr;
}














