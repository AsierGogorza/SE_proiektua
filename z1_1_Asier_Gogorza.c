#include <stdio.h>
#include <stdbool.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdlib.h>
#include <limits.h>
#include <string.h>

#define TENP_KOP 2

#define PHYSICAL_MEMORY_SIZE (16 * 1024 * 1024) // 16 MB
#define VIRTUAL_MEMORY_SIZE (64 * 1024 * 1024)  // 64 MB
#define PAGE_SIZE (4 * 1024) // Tamaño de página: 4 KB
#define WORD_SIZE 4          // Tamaño de palabra: 4 bytes

// Calcular el número de páginas y marcos
#define NUM_FRAMES (PHYSICAL_MEMORY_SIZE / PAGE_SIZE)
#define NUM_PAGES (VIRTUAL_MEMORY_SIZE / PAGE_SIZE)

#define TLB_SIZE 8

pthread_mutex_t mutex;
pthread_cond_t cond1, cond2;

pthread_t p1, p2, p3;
int clck, sched, proc;

int kontP, kontT, egina = 0;

typedef enum {
    READY,
    RUNNING,
    WAIT,
    TERMINATED
} state;

// Estructura de la tabla de páginas
typedef struct {
    int frameZenb; // Número de marco físico asignado
    int valid;        // Bit de validez: 1 si está mapeada, 0 si no
} OrriTaula;

typedef struct {
    unsigned int pgb; // Orri-taularen helbide fisikoa.
    unsigned int code; // Kodearen segmentuaren helbide birtuala.
    unsigned int data; // Datuen segmentuaren helbide birtuala.
} MM;

typedef struct {
    pid_t pid;
    state egoera;
    int lehentasuna; // Ezkerretik eskuinea lehentasun handienetik txikienera: 5 4 3 2 1
    int execDenboraP; // Zenbat scheduler txanda iraungo ditu
    int kont;
    int ilara;
    MM mm;
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

typedef struct {
    unsigned int orriKop; // Número de página virtual
    unsigned int frameKop; // Número de marco físico
    int valid;                // Bit de validez
} TLB;

typedef struct {
    TLB tlb[TLB_SIZE];
    unsigned int ptbr; // Orri-taularen lehenengo erregistroa.
    unsigned int ir; // Exekutatu beharreko instrukzioaren erregistroa.
    unsigned int pc; // Exekutatuko den hurrengo instrukzioaren erregistroa.
} MMU;

typedef struct {
    int id;
    ProcessQueue *exekuzioIlara;
    MMU mmu;
} haria; 

typedef struct {
    int hari_kop;
    int *harimap; // Harien bitmap
    haria *hariak;
} machine;


unsigned int memoriaFisikoa[PHYSICAL_MEMORY_SIZE / WORD_SIZE];
unsigned int memoriaBirtuala[VIRTUAL_MEMORY_SIZE / WORD_SIZE];
OrriTaula orriTaula[NUM_PAGES];
ProcessQueue* prozesuenIlara;  
machine* makina;

//***** METODO LAGUNTZAILEAK *****//

void memoria_hasi() {
    memset(memoriaFisikoa, 0, sizeof(memoriaFisikoa));
    memset(memoriaBirtuala, 0, sizeof(memoriaBirtuala));
    for (int i = 0; i < NUM_PAGES; i++) {
        orriTaula[i].frameZenb = -1; // No asignado
        orriTaula[i].valid = 0;         // No válido
    }
}

// Simular la asignación de marcos físicos a páginas virtuales
void map_page_to_frame(int orriZenb, int frameZenb) {
    if (orriZenb < 0 || orriZenb >= NUM_PAGES) {
        printf("Error: Orri zenbakia ez da zuzena.\n");
        return;
    }
    if (frameZenb < 0 || frameZenb >= NUM_FRAMES) {
        printf("Error: Frame zenbakia ez da zuzena.\n");
        return;
    }
    orriTaula[orriZenb].frameZenb = frameZenb;
    orriTaula[orriZenb].valid = 1; // Marco asignado, página válida
}

// Traducir una dirección virtual a física
unsigned int helbidea_itzuli(unsigned int helbideBirtuala) {
    unsigned int orriZenb = helbideBirtuala / PAGE_SIZE; // Número de página
    unsigned int offset = helbideBirtuala % PAGE_SIZE;      // Desplazamiento

    if (orriZenb >= NUM_PAGES) {
        printf("Error: Helbide birtuala ez da zuzena\n");
        return 0;
    }

    if (!orriTaula[orriZenb].valid) {
        printf("Error: Orria ez da zuzena\n");
        return 0;
    }

    unsigned int frameZenb = orriTaula[orriZenb].frameZenb;
    return (frameZenb * PAGE_SIZE) + offset; // helbide fisikoa itzuli.
}

unsigned int hitza_irakurri(unsigned int helbideBirtuala) {
    unsigned int helbideFisikoa = helbidea_itzuli(helbideBirtuala);
    return memoriaFisikoa[helbideFisikoa / WORD_SIZE];
}

// Escribir una palabra en una dirección virtual
void hitza_idatzi(unsigned int helbideBirtuala, unsigned int value) {
    unsigned int helbideFisikoa = helbidea_itzuli(helbideBirtuala);
    memoriaFisikoa[helbideFisikoa / WORD_SIZE] = value;
}

// pcb-a sortu
PCB* pcb_sortu(int kont, int ilaraZenb){
    PCB* pcbB = (PCB*)malloc(sizeof(PCB)); // PCB-a sortu
    if (pcbB == NULL) {
        printf("Errore bat egon da PCB-a sortzerakoan.\n");
        return NULL;
    }

    pcbB->pid = kont; // PCB-aren pid-a erazagutu    
    pcbB->egoera = READY; // PCB-aren egoera erazagutu
    int lehentasuna = (rand() % (5 - 1)) + 1;
    pcbB->lehentasuna = lehentasuna;
    int iraupena = (rand() % (3 - 1)) + 1;
    pcbB->execDenboraP = iraupena;
    pcbB->kont = iraupena;
    pcbB->ilara = ilaraZenb;

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

// Ilara hutsik dagoen ikusteko.
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

    if (ilara->kont == 0) {
        makina->harimap[pcb->ilara] = 1;
    }

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

    if (ilara->kont == 0) {
        makina->harimap[kendu->pcb->ilara] = 0;
    }
    
    pthread_mutex_unlock(&ilara->lock);
}

void prozesua_hasi (ProcessQueue* q) {
    QueueElem* momentukoaL = q->front;
    QueueElem* execL = NULL;
    int lehentasunHandienaL = 0;
    if (q->kont != 0) {
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
    } else {
        printf("Ilara hutsik dago.\n");
    }
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
            if (momentukoaGerorako->pcb->kont == 0) {
                prozesua_bukatu(q, momentukoaGerorako);
                prozesua_hasi(q);
            } else {
                printf("%d PID-a duen prozesuari %d falta zaio.\n", momentukoaGerorako->pcb->pid, momentukoaGerorako->pcb->kont);
                momentukoaGerorako->pcb->kont--;
            }
            
        }
    }  
}

void makina_sortu (int harikop) {
    machine *makinaL = (machine *)malloc(sizeof(machine));
    haria *hariak = (haria*)malloc(sizeof(haria*) * harikop); // Harien bektorea alokatu.

    int harimapa[harikop];
    for (int i = 0; i < harikop; i++) {
        harimapa[i] = 0;
    }

    makinaL->hariak = hariak;
    makinaL->hari_kop = harikop;
    makinaL->harimap = harimapa;
    makina = makinaL;
}

//***** METODOAK *****//

void *erloju(void *arg)
{
    pthread_mutex_lock(&mutex); // Mutexa blokeatu.
    int maiztasuna = *(int*) arg;
    int kontL = 1;
    egina = 0;
    while(1) {
        kontL++; // Kontagailu lokala.
        if(kontL == maiztasuna) {
            kontL = 0;

            while(egina < TENP_KOP) {
                pthread_cond_wait(&cond1, &mutex); // cond1 aldagaiaren zain geratu (ondorioz mutexa desblokeatu).
            }
            egina = 0;
            pthread_cond_broadcast(&cond2); // timerrak "askatu".
        }
    }
}

void* timer_sched(void* arg)
{
    pthread_mutex_lock(&mutex); // Erlojuak askatzen duen mutexa hartu.
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
            for (int i = 0; i < makina->hari_kop; i++) {
                printf("\n***** %d. hariko ilaran *****\n", i);
                ilararen_administrazioa(makina->hariak[i].exekuzioIlara);
            } 
        }

        pthread_cond_signal(&cond1); //clock-a "askatu" cond1 aldagaia aktibatuz.
        pthread_cond_wait(&cond2, &mutex); //cond2 aldagaiaren zain geratu.
    }
}

void* timer_proc(void* arg)
{
    pthread_mutex_lock(&mutex); // clock-ak askatzen duen mutexa hartu.
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

            int hariZenb = 0;
            int kontMin = INT_MAX;
            int harikop = makina->hari_kop;
            for (int i = 0; i < harikop; i++) {
                if (makina->hariak[i].exekuzioIlara->kont < kontMin) {
                    hariZenb = i;
                    kontMin = makina->hariak[i].exekuzioIlara->kont;
                }
            }
            
            PCB* pcbL = pcb_sortu(kontP, hariZenb);
            ProcessQueue* momentukoa = makina->hariak[hariZenb].exekuzioIlara;
            enqueue(momentukoa, pcbL);
            printf("%d hariko prozesu kopurua: %d\n", hariZenb, momentukoa->kont);
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

    makina_sortu(hari_kop);
    memoria_hasi();

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

    for (int i = 0; i < hari_kop; i++) {
        makina->hariak[i].exekuzioIlara = ilara_sortu();
    }

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
