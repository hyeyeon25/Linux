#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <string.h>

#define PASSWORD "123"

void alarmHandler(int signo) {
    printf("\n[경고] 10초 초과! 프로그램 종료\n");
    exit(0);
}

void resetHandler(int signo) {
    printf("\n[알림] Ctrl+C 입력 → 제한시간 10초로 리셋!\n");
    alarm(10);
}

int main() {
    char input[100];

    signal(SIGALRM, alarmHandler);
    signal(SIGINT, resetHandler);

    alarm(10); // 10초 타이머 시작

    printf("비밀번호를 10초 안에 입력하세요.\n");

    while (1) {
        printf("입력: ");
        scanf("%s", input);

        if (strcmp(input, PASSWORD) == 0) {
            printf("비밀번호 성공!\n");
            break;
        } else {
            printf("틀림! 다시 입력.\n");
        }
    }

    return 0;
}

