#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

// 스레드에게 전달할 '작업 지시서' 구조체
typedef struct {
    int start_index;   // 너는 여기서부터
    int end_index;     // 여기까지 계산해
    int terms;         // 테일러 급수 항 개수
    double *x;         // 입력 배열 주소
    double *result;    // 결과 저장할 주소
} ThreadArgs;

// 스레드가 실행할 함수 (기존 sinx_taylor를 변형)
void *ThreadTask(void *args) {
    ThreadArgs *my_data = (ThreadArgs *)args;
    
    // 내 담당구역(start ~ end)만 반복
    for(int i = my_data->start_index; i < my_data->end_index; i++) {
        double value = my_data->x[i];
        double numer = my_data->x[i] * my_data->x[i] * my_data->x[i];
        double denom = 6.;
        int sign = -1;

        for(int j=1; j <= my_data->terms; j++) {
            value += (double)sign * numer / denom;
            numer *= my_data->x[i] * my_data->x[i];
            denom *= (2.*(double)j+2.) * (2.*(double)j+3.);
            sign *= -1;
        }
        my_data->result[i] = value; // 결과 저장
    }
    return NULL;
}

int main() {
    int num_elements = 1000; // 예시 데이터 개수
    int num_threads = 4;     // 스레드 개수
    
    // 데이터 메모리 할당 및 초기화 (생략 가능하나 구조상 필요)
    double *x = (double*)malloc(sizeof(double) * num_elements);
    double *result = (double*)malloc(sizeof(double) * num_elements);
    // ... x 배열에 값 채우는 코드 필요 ...

    pthread_t threads[num_threads];
    ThreadArgs args[num_threads];
    
    int chunk_size = num_elements / num_threads; // 1000 / 4 = 250개씩

    // 스레드 생성
    for(int i=0; i<num_threads; i++) {
        args[i].x = x;
        args[i].result = result;
        args[i].terms = 10; // 정밀도
        args[i].start_index = i * chunk_size;
        
        // 마지막 스레드는 남은 거 다 처리하게 (나머지 처리)
        if (i == num_threads - 1) 
            args[i].end_index = num_elements;
        else 
            args[i].end_index = (i + 1) * chunk_size;

        pthread_create(&threads[i], NULL, ThreadTask, (void *)&args[i]);
    }

    // 스레드 종료 대기 (Join)
    for(int i=0; i<num_threads; i++) {
        pthread_join(threads[i], NULL);
    }

    printf("계산 끝!\n");
    // 메모리 해제
    free(x);
    free(result);
    return 0;
}
