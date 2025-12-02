#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

int acc = 0;
// 1. 자물쇠(뮤텍스) 변수 선언
pthread_mutex_t mtx; 

void *TaskCode(void *argument) {
    int tid = *((int*) argument);
    
    for(int i=0; i<1000000; i++) {
        // 2. 임계 영역(Critical Section) 들어가기 전에 잠금!
        pthread_mutex_lock(&mtx);
        
        acc = acc + 1;  // 여기가 보호해야 할 공유 자원 접근 구역
        
        // 3. 작업 끝나면 반드시 잠금 해제!
        pthread_mutex_unlock(&mtx);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    pthread_t threads[4];
    int args[4];
    int i;

    // 4. 뮤텍스 초기화 (사용 전 필수)
    pthread_mutex_init(&mtx, NULL);

    /* 스레드 생성 부분 (기존 코드와 동일) */
    for (i=0; i<4; ++i) {
        args[i] = i;
        pthread_create(&threads[i], NULL, TaskCode, (void *) &args[i]);
    }

    /* 스레드 종료 대기 부분 (기존 코드와 동일) */
    for (i=0; i<4; ++i) {
        pthread_join(threads[i], NULL);
    }
    
    printf("Result: %d\n", acc); // 결과 확인
    return 0;
}
