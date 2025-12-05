#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <string.h>

#define N_PROC 10          // 프로세스 개수
#define TIME_QUANTUM 1     // 타임퀀텀 (초)
#define MAX_BURST 10       // 초기 CPU burst 범위 1~10
#define MAX_IO_BURST 5     // I/O 시간 1~5

// --------------------- 자료구조 정의 ---------------------

typedef enum {
    NEW,
    READY,
    RUNNING,
    WAITING,   // I/O 대기
    TERMINATED
} state_t;

const char *state_name(state_t s) {
    switch (s) {
        case NEW:        return "NEW";
        case READY:      return "READY";
        case RUNNING:    return "RUN";
        case WAITING:    return "WAIT";
        case TERMINATED: return "DONE";
        default:         return "UNK";
    }
}

typedef struct {
    int   id;              // 논리적 ID (0~N_PROC-1)
    pid_t pid;             // 실제 PID
    int   total_burst;     // 초기 CPU burst
    int   remaining_burst; // 남은 CPU burst
    int   waiting_time;    // READY 상태로 기다린 시간
    int   turnaround_time; // 완료 시간 (도착시간 0 가정)
    state_t state;
    int   io_time_left;    // I/O 남은 시간
    int   completion_time; // 완료 시각
} PCB;

// 원형 큐 (프로세스 인덱스 저장)
typedef struct {
    int data[N_PROC];
    int head;
    int tail;
    int size;
} Queue;

void q_init(Queue *q) {
    q->head = q->tail = q->size = 0;
}

int q_empty(Queue *q) {
    return q->size == 0;
}

void q_push(Queue *q, int v) {
    if (q->size >= N_PROC) return;   // 오버플로우 무시
    q->data[q->tail] = v;
    q->tail = (q->tail + 1) % N_PROC;
    q->size++;
}

int q_pop(Queue *q) {
    if (q_empty(q)) return -1;
    int v = q->data[q->head];
    q->head = (q->head + 1) % N_PROC;
    q->size--;
    return v;
}

// --------------------- 자식 프로세스 코드 ---------------------

/*
 * 자식 프로세스는 실제 CPU burst를 알지 못하고,
 * 부모가 SIGCONT를 보내면 1초 동안 한다고 가정하여
 * 메시지를 출력하고 sleep(1)만 한다.
 * 스케줄링 / burst 감소 / I/O 여부 결정은 모두 부모에서 시뮬레이션.
 */
void child_work(int id) {
    // 생성되자마자 STOP 해서 부모 스케줄러가 깨울 때까지 대기
    signal(SIGCONT, SIG_DFL);       // SIGCONT는 기본 동작(continue)
    kill(getpid(), SIGSTOP);        // 부모가 스케줄 시작할 때까지 STOP

    while (1) {
        // 부모가 SIGCONT 를 보내면 여기부터 다시 실행된다고 가정
        printf("  [Child %2d, pid=%5d] running...\n", id, getpid());
        fflush(stdout);

        sleep(1);                   // 타임 퀀텀 1초 동안 "일" 함
        // 이후 SIGSTOP 을 받을 때까지 다시 대기
    }
}

// --------------------- 시뮬레이션 함수 ---------------------

double run_simulation(double io_prob, int *out_total_time, double *out_avg_wait) {
    PCB   pcb[N_PROC];
    Queue ready_q;
    Queue io_q;
    q_init(&ready_q);
    q_init(&io_q);

    // 1. 자식 프로세스 생성 및 PCB 초기화
    for (int i = 0; i < N_PROC; i++) {
        pid_t pid = fork();
        if (pid < 0) {
            perror("fork");
            exit(1);
        } else if (pid == 0) {
            // Child
            child_work(i);
            exit(0);
        } else {
            // Parent
            int burst = (rand() % MAX_BURST) + 1;   // 1~MAX_BURST
            pcb[i].id              = i;
            pcb[i].pid             = pid;
            pcb[i].total_burst     = burst;
            pcb[i].remaining_burst = burst;
            pcb[i].waiting_time    = 0;
            pcb[i].turnaround_time = 0;
            pcb[i].state           = READY;
            pcb[i].io_time_left    = 0;
            pcb[i].completion_time = -1;
            q_push(&ready_q, i);
        }
    }

    int finished     = 0;
    int current_time = 0;

    printf("\n=== Simulation start (IO probability = %.2f) ===\n", io_prob);
    printf("Initial CPU bursts: ");
    for (int i = 0; i < N_PROC; i++) {
        printf("P%d=%d ", i, pcb[i].total_burst);
    }
    printf("\n\n");

    // 테이블 형태로 로그 출력
    printf("-----------------------------------------------------------------------------------------\n");
    printf(" time |  PID  | prc | state | rem | wait | io_left | readyQ | ioQ | event\n");
    printf("-----------------------------------------------------------------------------------------\n");

    // 2. 라운드 로빈 스케줄링 루프
    while (finished < N_PROC) {
        int running_idx = -1;

        // 2-0. ready 큐에서 다음 프로세스 선택
        if (!q_empty(&ready_q)) {
            running_idx = q_pop(&ready_q);
            PCB *p = &pcb[running_idx];
            p->state = RUNNING;
            // 실행 시작: 자식 깨우기
            kill(p->pid, SIGCONT);
        }

        // 한 타임 퀀텀 동안 실행
        sleep(TIME_QUANTUM);
        current_time += TIME_QUANTUM;

        char event_desc[64] = "idle";

        // 2-1. 방금 실행한 프로세스 처리
        if (running_idx != -1) {
            PCB *p = &pcb[running_idx];

            // 타임퀀텀 끝났으므로 STOP
            kill(p->pid, SIGSTOP);
            p->remaining_burst--;

            // I/O 수행 여부 결정
            double r = (double)rand() / RAND_MAX;
            if (p->remaining_burst <= 0) {
                // CPU burst 소진하면 종료
                p->state           = TERMINATED;
                p->completion_time = current_time;
                p->turnaround_time = p->completion_time; // 도착시간 0

                kill(p->pid, SIGTERM);            // 자식 종료
                waitpid(p->pid, NULL, WNOHANG);   // 좀비 방지 (블록 X)
                finished++;

                snprintf(event_desc, sizeof(event_desc),
                         "P%d finished", p->id);
            } else if (r < io_prob) {
                // I/O 요청 발생
                p->state        = WAITING;
                p->io_time_left = (rand() % MAX_IO_BURST) + 1; // 1~MAX_IO_BURST
                if (p->io_time_left <= 0) p->io_time_left = 1; // 안전장치
                q_push(&io_q, running_idx);
                snprintf(event_desc, sizeof(event_desc),
                         "P%d -> I/O (%d)", p->id, p->io_time_left);
            } else {
                // 다시 ready 큐로 복귀
                p->state = READY;
                q_push(&ready_q, running_idx);
                snprintf(event_desc, sizeof(event_desc),
                         "P%d time slice", p->id);
            }
        }

        // 2-2. I/O 큐 처리
        int io_q_size = io_q.size;
        for (int k = 0; k < io_q_size; k++) {
            int idx = q_pop(&io_q);
            PCB *p = &pcb[idx];
            if (p->state != WAITING) continue;
            p->io_time_left--;
            if (p->io_time_left <= 0) {
                p->state = READY;
                q_push(&ready_q, idx);
            } else {
                q_push(&io_q, idx);  // 아직 I/O 남음
            }
        }

        // 2-3. READY 상태인 프로세스들의 waiting time 1씩 증가
        for (int i = 0; i < N_PROC; i++) {
            if (pcb[i].state == READY) {
                pcb[i].waiting_time++;
            }
        }

        // 2-4. readyQ, ioQ 내용 문자열로 생성 (로그용)
        char ready_str[64] = "";
        char io_str[64]    = "";

        // ready 큐 스냅샷
        {
            Queue tmp = ready_q;
            char buf[8];
            while (!q_empty(&tmp)) {
                int idx = q_pop(&tmp);
                snprintf(buf, sizeof(buf), "P%d ", pcb[idx].id);
                strncat(ready_str, buf,
                        sizeof(ready_str) - strlen(ready_str) - 1);
            }
        }
        // io 큐 스냅샷
        {
            Queue tmp = io_q;
            char buf[8];
            while (!q_empty(&tmp)) {
                int idx = q_pop(&tmp);
                snprintf(buf, sizeof(buf), "P%d ", pcb[idx].id);
                strncat(io_str, buf,
                        sizeof(io_str) - strlen(io_str) - 1);
            }
        }

        // 2-5. PCB 실시간 테이블 한 줄 출력
        if (running_idx != -1) {
            PCB *p = &pcb[running_idx];
            printf(" %4d | %5d | P%-2d | %-5s | %3d | %4d | %7d | %-6s | %-3s | %s\n",
                   current_time,
                   p->pid,
                   p->id,
                   state_name(p->state),
                   p->remaining_burst,
                   p->waiting_time,
                   p->io_time_left,
                   ready_str[0] ? ready_str : "-",
                   io_str[0]    ? io_str    : "-",
                   event_desc);
        } else {
            // ready 큐가 비어있고 I/O만 남은 경우 등
            printf(" %4d |  ---- | --  | %-5s | --- | ---- | ------- | %-6s | %-3s | %s\n",
                   current_time,
                   "idle",
                   ready_str[0] ? ready_str : "-",
                   io_str[0]    ? io_str    : "-",
                   event_desc);
        }

        // 2-6. DEADLOCK 검사: ready와 io 큐가 모두 비었는데 끝난 게 아니면 비정상
        if (q_empty(&ready_q) && q_empty(&io_q)) {
            int all_done = 1;
            for (int i = 0; i < N_PROC; i++) {
                if (pcb[i].state != TERMINATED) {
                    all_done = 0;
                    break;
                }
            }
            if (all_done) break;

            printf("DEADLOCK DETECTED -- forcing termination\n");
            break;
        }
    }

    printf("-----------------------------------------------------------------------------------------\n");

    // 3. 통계 계산
    int total_wait = 0;
    for (int i = 0; i < N_PROC; i++) {
        total_wait += pcb[i].waiting_time;
    }
    double avg_wait = (double)total_wait / N_PROC;

    printf("\n[Result] IO probability = %.2f\n", io_prob);
    printf("Process | burst | waiting_time | turnaround_time\n");
    for (int i = 0; i < N_PROC; i++) {
        printf("P%-7d %6d %14d %16d\n",
               pcb[i].id,
               pcb[i].total_burst,
               pcb[i].waiting_time,
               pcb[i].turnaround_time);
    }
    printf("Average waiting time = %.2f\n\n", avg_wait);

    if (out_total_time) *out_total_time = current_time;
    if (out_avg_wait)   *out_avg_wait   = avg_wait;
    return avg_wait;
}

// --------------------- main: I/O 비율별 실험 ---------------------

int main(void) {
    srand((unsigned int)time(NULL));

    // 이 배열로 실험할 I/O 비율 설정
    double io_probs[] = {0.0, 0.3, 0.6};
    int    n_sim      = sizeof(io_probs) / sizeof(io_probs[0]);

    printf("HW03: OS Scheduling Simulation (Round Robin with signals)\n");
    printf("Number of processes: %d, time quantum: %d sec, CPU burst: 1-%d\n",
           N_PROC, TIME_QUANTUM, MAX_BURST);
    printf("This program will run simulations for different I/O probabilities.\n\n");

    double avg_waits[10];

    for (int i = 0; i < n_sim; i++) {
        int    total_time = 0;
        double avg_wait   = 0.0;

        avg_waits[i] = run_simulation(io_probs[i], &total_time, &avg_wait);

        // 남은 자식이 있으면 정리
        while (waitpid(-1, NULL, WNOHANG) > 0) {
            ;
        }
    }

    // I/O 비율에 따른 평균 대기 시간 요약 테이블
    printf("==== Summary: effect of I/O probability on average waiting time ====\n");
    printf(" IO_prob | avg_waiting_time\n");
    printf("---------------------------\n");
    for (int i = 0; i < n_sim; i++) {
        printf("  %.2f   |    %.2f\n", io_probs[i], avg_waits[i]);
    }
    printf("\n");

    return 0;
}

