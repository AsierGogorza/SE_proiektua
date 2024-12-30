#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>

#define TENP_KOP 2

pthread_mutex_t mutex;
pthread_cond_t cond1, cond2, cond3;

pthread_t p1, p2, p3;
int clck, sched, proc;

int kontP, kontT, egina = 0;

typedef struct {
    int total_hari_kop; // cpu_kop * core_kop * hari_kop
    int *harimap; // Harien bitmap
    haria *hariak;
} machine;

typedef struct {
    int id;
    PCB *pcb;
} haria

typedef struct {
    pid_t pid;
} PCB;

// Prozesua ilaran gordetzeko elementua.
typedef struct {
    PCB *pcb;                    // Pointer to PCB
    struct QueueElem *next;           // Pointer to the next node
} QueueElem;

// Prozesuen ilara.
typedef struct {
    int kont;
    QueueElem *front;
    QueueElem *rear;
    pthread_mutex_t lock; // Ilaran eragiketak egiterakoan blokeatu muturrak ondo adierazteko.
} ProcessQueue;

ProcessQueue* prozesuenIlara;

//***** METODO LAGUNTZAILEAK *****//

PCB* pcb_sortu(int kont){
    PCB* pcbB = (PCB*)malloc(sizeof(PCB)); // PCB-a sortu
    if (pcbB == NULL) {
        printf("Errore bat egon da PCB-a sortzerakoan.\n");
        return NULL;
    }

    pcbB->pid = kont; // PCB-aren pid-a erazagutu

    return pcbB;
}

ProcessQueue* ilara_sortu() {
    ProcessQueue *ilara = (ProcessQueue *)malloc(sizeof(ProcessQueue));
    if (!ilara) {
        printf("Prozesuen ilara ezin izan da sortu.\n");
        return NULL;
    }
    ilara->front = NULL;
    ilara->rear = NULL;
    ilara->kont = 0;
    pthread_mutex_init(&ilara->lock, NULL);
    return ilara;
}

// Function to check if the queue is empty
int isEmpty(ProcessQueue* q) { return (q->kont == 0); }

void enqueue(ProcessQueue *ilara, PCB *pcb) {
    QueueElem *elem_berria = (QueueElem *)malloc(sizeof(QueueElem));
    if (!elem_berria) {
        printf("Elementu berria ezin izan da sortu.\n");
        return;
    }
    elem_berria->pcb = pcb;
    elem_berria->next = NULL;

    pthread_mutex_lock(&ilara->lock);
    if (isEmpty(ilara)) {
        ilara->front = elem_berria; // Set front if queue is empty
    } else {
        ilara->rear->next = elem_berria; // Link the new node
    }
    ilara->rear = elem_berria; // Update the rear
    ilara->kont++; // Increment kont
    pthread_mutex_unlock(&ilara->lock);
}

PCB* dequeue(ProcessQueue *ilara) {
    pthread_mutex_lock(&ilara->lock);
    if (isEmpty(ilara)) {
        printf("Ez daude elementurik kentzeko.\n");
        pthread_mutex_unlock(&ilara->lock);
        return NULL; // Return NULL if queue is empty
    }

    QueueElem *burua = ilara->front; // Lortu buruan dagoen elementua.
    PCB *pcb = burua->pcb; // Kendutako elementuaren pcb-a gorde.
    ilara->front = ilara->front->next; // Ilararen burua eguneratu.

    if (ilara->front == NULL) {
        ilara->rear = NULL; // Ilara hustu bada (burua == NULL) buztana NULL bezala eguneratu.
    }

    pthread_cond_broadcast(&cond3); // Abixatu leku bat egin dela

    free(burua); // Free the old front node
    ilara->kont--; // Decrement kont
    pthread_mutex_unlock(&ilara->lock);
    return pcb;
}

PCB* peek(ProcessQueue* q) {
    if (isEmpty(q)) {
        printf("Ilara hutsik dago\n");
        return NULL; 
    }
    return q->front->pcb;
}

//***** METODOAK *****//

void *erloju(void *arg)
{
    pthread_mutex_lock(&mutex); // Mutexa blokeatu
    int maiztasuna = *(int*) arg;
    int kontL = 0;
    egina = 0;
    while(1) {

        kontL++; // Kontagailu lokala
        

        if(kontL == maiztasuna) {
            kontL = 0;

            while(egina < TENP_KOP)
            {
                pthread_cond_wait(&cond1, &mutex); // cond1 aldagaiaren zain geratu (ondorioz mutexa desblokeatu)
            }

            egina = 0;

            pthread_cond_broadcast(&cond2); //timerrak "askatu"
        }
    }
}

void* timer_sched(void* arg)
{
    pthread_mutex_lock(&mutex); // Erlojuak askatzen duen mutexa hartu 
    int maiztasuna = *(int*) arg;

    int kontL;
    while(1) {
        
        kontL++;
        egina++;

        if (kontL == maiztasuna) {
            kontL = 0;

            printf("Schedulerrak eragiketak egiten ditu. \n");
        }


        pthread_cond_signal(&cond1); //clock-a "askatu" cond1 aldagaia aktibatuz

        pthread_cond_wait(&cond2, &mutex); //cond2 aldagaiaren zain geratu
    }
}

void* timer_proc(void* arg)
{
    pthread_mutex_lock(&mutex); //clock-ak askatzen duen mutexa hartu
    int maiztasuna = *(int*) arg;

    int kontL = 0;
    while(1) {

        kontL++;
        egina++;

        if (kontL == maiztasuna) {
            kontL = 0;

            printf("Prozesu bat sortu da, pid = %d \n", kontP);
            kontP++;
        }

        pthread_cond_signal(&cond1); //clock "askatu" cond1 bidaliz

        pthread_cond_wait(&cond2, &mutex); //cond2-ren zain geratu
    }
}

//***** MAIN *****//

int main(int argc, char *argv[])
{
    if(argc < 4) {
        printf("Erabilera: %s <clock_maiztasuna> <sched_maiztasun> <proc_maiztasun> \n", argv[0]);
        return 1;
    }

    printf("Sistema martxan jartzen... \n");

    printf("Erlojuaren maiztasuna: %d \n", atoi(argv[1]));
    int maiztasunaErloju = atoi(argv[1]);
    int maiztasunaScheduler = atoi(argv[2]);
    int maiztasunaProc = atoi(argv[3]);

    // Mutexa eta cond aldagaiak hasieratu
    if (pthread_mutex_init(&mutex, NULL) != 0) {
        printf("\nMutex hasieraketan arazo bat egon da. \n");
        return 1;
    }

    if (pthread_cond_init(&cond1, NULL) != 0) {
        printf("\n1. kondizio hasieraketan arazo bat egon da. \n");
        return 1;
    }

    if (pthread_cond_init(&cond2, NULL) != 0) {
        printf("\n2. kondizio hasieraketan arazo bat egon da. \n");
        return 1;
    }

    if (pthread_cond_init(&cond3, NULL) != 0) {
        printf("\n3. kondizio hasieraketan arazo bat egon da. \n");
        return 1;
    }

    printf("Mutexa eta cond aldagaiak martxan jarri dira. \n");

    // Hariak martxan jarri
    pthread_create(&p1, NULL, erloju, (void*) &maiztasunaErloju); // if-ak jarri ondo sortu direla konprobatzeko (0 bada ondo)
    pthread_create(&p2, NULL, timer_sched, (void*) &maiztasunaScheduler);
    pthread_create(&p3, NULL, timer_proc, (void*) &maiztasunaProc);
   
    prozesuenIlara = ilara_sortu();

    printf("Hariak martxan jarri dira. \n");

    // Hariak bukatu
    pthread_join(p1, NULL);
    pthread_join(p2, NULL);
    pthread_join(p3, NULL);

    printf("Hariak bukatuta. \n");

    // Mutexa eta cond aldagaiak amaitu
    pthread_mutex_destroy(&mutex);
    pthread_cond_destroy(&cond1);
    pthread_cond_destroy(&cond2);
    pthread_cond_destroy(&cond3);

    printf("Mutexa eta cond aldagaiak ezabatuak \n");

    return 0;
}