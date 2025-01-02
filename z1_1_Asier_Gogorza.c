#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdlib.h>

#define TENP_KOP 2

pthread_mutex_t mutex;
pthread_cond_t cond1, cond2;

pthread_t p1, p2, p3;
int clck, sched, proc;

int kontP, kontT, egina, execDenbora = 0;

typedef enum {
    READY,
    RUNNING,
    WAIT,
    TERMINATED
} state;

typedef struct {
    pid_t pid;
    state egoera;
    int lehentasuna; // Ezkerretik eskuinea lehentasun handienetik txikienera: 5 4 3 2 1
    int execDenboraP;
    int kont;
} PCB;

typedef struct {
    int id;
    PCB *pcb;
} haria; // Pasar x num de haris al ejecutar. En el main crear x haris con el pcb NULL.

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

typedef struct {
    int total_hari_kop; // cpu_kop * core_kop * hari_kop
    int *harimap; // Harien bitmap
    haria *hariak;
} machine;

ProcessQueue* prozesuenIlara;


//***** METODO LAGUNTZAILEAK *****//

PCB* pcb_sortu(int kont){
    PCB* pcbB = (PCB*)malloc(sizeof(PCB)); // PCB-a sortu
    if (pcbB == NULL) {
        printf("Errore bat egon da PCB-a sortzerakoan.\n");
        return NULL;
    }

    pcbB->pid = kont; // PCB-aren pid-a erazagutu    
    pcbB->egoera = READY; // PCB-aren egoera erazagutu
    int lehentasuna = (rand() % (5 - 1 + 1)) + 1;
    pcbB->lehentasuna = lehentasuna;
    int iraupena = (rand() % (3 - 1 + 1)) + 1;
    pcbB->execDenboraP = iraupena;
    pcbB->kont = iraupena;

    printf("%d prozesua sortu da %d lehentasunarekin eta %d txanda egongo da bizirik.\n", kont, lehentasuna, iraupena);
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

PCB* dequeue(ProcessQueue *ilara, QueueElem *kendu) {
    pthread_mutex_lock(&ilara->lock);
    if (isEmpty(ilara)) {
        printf("Ez daude elementurik kentzeko.\n");
        pthread_mutex_unlock(&ilara->lock);
        return NULL; // Return NULL if queue is empty
    }
    
    if (ilara->front->pcb->pid == kendu->pcb->pid) {
        ilara->front = ilara->front->next;
    } else {
        QueueElem* momentukoa = ilara->front;
        QueueElem* hurrengoa = momentukoa->next;
        while (hurrengoa != NULL) {
            if (hurrengoa->pcb->pid == kendu->pcb->pid) {
                momentukoa->next = kendu->next;
                break;
            }
            momentukoa = momentukoa->next;
            hurrengoa = momentukoa->next;
        }
    }
    
    //printf("%d PID-a duen prozesua ilaratik kendu da.\n", kendu->pcb->pid);

    ilara->kont--; // Decrement kont
    pthread_mutex_unlock(&ilara->lock);
}

PCB* peek(ProcessQueue* q) {
    if (isEmpty(q)) {
        printf("Ilara hutsik dago\n");
        return NULL; 
    }
    return q->front->pcb;
}

void prozesua_hasi (ProcessQueue* q) {
    QueueElem* momentukoaL = q->front;
    QueueElem* execL = NULL;
    int lehentasunHandienaL = 0;
    while (momentukoaL != NULL) {
        if (momentukoaL->pcb->lehentasuna > lehentasunHandienaL) {
            execL = momentukoaL;
            lehentasunHandienaL = momentukoaL->pcb->lehentasuna;
        }
        momentukoaL = momentukoaL->next;            
    }

    printf("%d PID-a duen prozesua hasi da.\n",execL->pcb->pid);
    printf("%d PID-a duen prozesuari %d falta zaio.\n", execL->pcb->pid, execL->pcb->kont);
    execL->pcb->egoera = RUNNING;
    execL->pcb->kont--;
}

void prozesua_bukatu (ProcessQueue* q, QueueElem* momentukoa) {
    if (isEmpty(q)) {
        printf("Prozesu guztiak bukatu dira.\n");
    } else {
        momentukoa->pcb->egoera = TERMINATED;
        printf("%d PID-a duen prozesua bukatu da.\n", momentukoa->pcb->pid);
        dequeue(q, momentukoa);
    }
    
}
void ilararen_administrazioa (ProcessQueue* q) {

    if (isEmpty(q)) {
        printf("Ilaran ez daude prozesurik.\n");
    } else {
        QueueElem* momentukoa = NULL;
        QueueElem* momentukoaGerorako = NULL;
        QueueElem* exekutatu = NULL;
        int pidRunning = -1;
        if (q->kont > 1) { // Ilaran prozesu bat baino gehiago badago gertatu daiteke prozesu bat aktibatuta ez egotea.
            momentukoa = q->front;
            int lehentasunaRunning = 0;
            while (momentukoa != NULL) {
                if (momentukoa->pcb->egoera == RUNNING) { // Aurkitu aktibatuta dagoena.
                    if (momentukoa->pcb->kont <= 0) { // Begiratu ea bukatu den. Bukatu bada berria aukeratu.
                        prozesua_bukatu(q, momentukoa);
                        prozesua_hasi(q);
                        break;
                    } else {
                        lehentasunaRunning = momentukoa->pcb->lehentasuna;
                        pidRunning = momentukoa->pcb->pid;
                        momentukoaGerorako = momentukoa;
                        break;
                    }
                }
                momentukoa = momentukoa->next;
            }

            if (momentukoaGerorako != NULL) {
                QueueElem* momentukoa2 = q->front;
                while (momentukoa2 != NULL) {
                    if (momentukoa2->pcb->lehentasuna > lehentasunaRunning) {
                        exekutatu = momentukoa2;
                        break;
                    }
                    momentukoa2 = momentukoa2->next;            
                }
            }
        } else {
            momentukoaGerorako = q->front;
            q->front->pcb->egoera = RUNNING;
        }

        if (exekutatu != NULL) {
            momentukoaGerorako->pcb->egoera = WAIT;
            exekutatu->pcb->egoera = RUNNING;
            
            printf("Lehentasuna dela eta %d PID-a duen prozesua itxoin behar du %d PID-a duen prozesua bukatu arte.\n", momentukoaGerorako->pcb->pid, exekutatu->pcb->pid);
            printf("%d PID-a duen prozesua hasi da.\n", exekutatu->pcb->pid);
            printf("%d PID-a duen prozesuari %d falta zaio.\n", exekutatu->pcb->pid, exekutatu->pcb->kont);
            exekutatu->pcb->kont--;
        } else if ((exekutatu == NULL) && (momentukoaGerorako != NULL)) {
            if (momentukoaGerorako->pcb->kont == momentukoaGerorako->pcb->execDenboraP) {
                printf("%d PID-a duen prozesua hasi da.\n", momentukoaGerorako->pcb->pid);
            }
            
            printf("%d PID-a duen prozesuari %d falta zaio.\n", momentukoaGerorako->pcb->pid, momentukoaGerorako->pcb->kont);
            momentukoaGerorako->pcb->kont--;
        }
    }  
}



//***** METODOAK *****//

void *erloju(void *arg)
{
    pthread_mutex_lock(&mutex); // Mutexa blokeatu
    int maiztasuna = *(int*) arg;
    int kontL = 1;
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

    int kontL = 1;
    int aldia = 0;
    while(1) {
        kontL++;
        egina++;

        if (kontL == maiztasuna) {
            aldia++;
            kontL = 1;
            printf("\nTIMER_SCHED %d\n", aldia);

            ilararen_administrazioa(prozesuenIlara);
        }

        pthread_cond_signal(&cond1); //clock-a "askatu" cond1 aldagaia aktibatuz

        pthread_cond_wait(&cond2, &mutex); //cond2 aldagaiaren zain geratu
    }
}

void* timer_proc(void* arg)
{
    pthread_mutex_lock(&mutex); //clock-ak askatzen duen mutexa hartu
    int maiztasuna = *(int*) arg;

    int kontL = 1;
    int aldia = 0;
    while(1) {

        kontL++;
        egina++;

        if (kontL == maiztasuna) {
            aldia++;
            printf("\nTIMER_PROC %d\n", aldia);
            kontL = 0;

            PCB* pcbL = pcb_sortu(kontP);
            enqueue(prozesuenIlara, pcbL);
            printf("Prozesu kopurua: %d\n", prozesuenIlara->kont);
            kontP++;
        }

        pthread_cond_signal(&cond1); //clock "askatu" cond1 bidaliz

        pthread_cond_wait(&cond2, &mutex); //cond2-ren zain geratu
    }
}

//***** MAIN *****//

int main(int argc, char *argv[])
{
    if(argc != 5) {
        printf("Erabilera: %s <clock_maiztasuna> <schdl_maiztasuna> <proc_maiztasuna> <hari_kopurua>\n", argv[0]);
        return 1;
    }

    printf("Sistema martxan jartzen... \n");

    int maiztasunaE = atoi(argv[1]);
    int maiztasunaS = atoi(argv[2]);
    int maiztasunaP = atoi(argv[3]);
    printf("Erlojuaren maiztasuna: %d \n", maiztasunaE);
    int hari_kop = atoi(argv[4]);
    printf("Hari kopurua: %d \n", hari_kop);

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

    printf("Mutexa eta cond aldagaiak martxan jarri dira. \n");

    // Hariak martxan jarri
    pthread_create(&p1, NULL, erloju, (void*) &maiztasunaE); // if-ak jarri ondo sortu direla konprobatzeko (0 bada ondo)
    pthread_create(&p2, NULL, timer_sched, (void*) &maiztasunaS);
    pthread_create(&p3, NULL, timer_proc, (void*) &maiztasunaP);
   
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

    printf("Mutexa eta cond aldagaiak ezabatuak \n");

    return 0;
}