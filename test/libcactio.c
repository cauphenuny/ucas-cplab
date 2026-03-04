#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

// printers
void print_int(int x) {
    printf("%d\n", x);
}

void print_float(float x) {
    printf("%f\n", x);
}

void print_double(double x) {
    printf("%lf\n", x);
}

void print_bool(bool x) {
    printf("%s\n", x ? "true" : "false");
}

// scanners
int get_int() {
    int x;
    //fprintf(stderr, "please enter an int number:\n");
    scanf("%d", &x);
    return x;
}

float get_float() {
    float x;
    //fprintf(stderr, "please enter a float number:\n");
    scanf("%f", &x);
    return x;
}

double get_double() {
    double x;
    //fprintf(stderr, "please enter a double number:\n");
    scanf("%lf", &x);
    return x;
}

int get_array(void *arr) {
    int num = ((int *)arr)[0];
    return num;
}

void put_array(int size, int *arr) {
    for (int i = 0; i < size; i++) {
        printf("%d ", arr[i]);
    }
    printf("\n");
}