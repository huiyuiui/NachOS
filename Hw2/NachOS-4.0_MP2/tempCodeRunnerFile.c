#include<stdio.h>

int main(){
    int arr[10000];
    for(int i=0;i<10000;i++){
        arr[i] = i;
        printf("%d\n", arr[i]);
    }
}