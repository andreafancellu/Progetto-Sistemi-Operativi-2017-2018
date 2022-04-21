#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <semaphore.h>
#include <string.h>
#include <sys/msg.h>
#include <pthread.h>


#define MSG_MAX_SIZE 1000 // dimensione msgbuf

#define TEST_ERROR	if (errno) { fprintf(stderr, \
						"%s:%d: PID=%5d: Error %d (%s)\n", __FILE__, \
						__LINE__, getpid(), errno, strerror(errno)); }


struct shared_data *my_data;	// puntatore ad area di memoria condivisa
struct msgbuf send_to_a;		// messaggio da inviare ai processi A
struct msgbuf send_to_b; 		// messaggio da inviare ai processi B
struct msgbuf receive_from_a;	// messaggio ricevuto dai processi A
struct msgbuf receive_from_b; 	// messaggio ricevuto dai processi B


// CHIAVI PER STRUTTURE CONDIVISE
key_t key_shm; 		// memora condivisa
key_t key_msq; 		// coda di messaggi
key_t key_sem; 		// semaforo

// VARIABILI IN INPUT
int genes;			// valore per calcolare il genoma dei processi
int init_people; 	// numero iniziale di individui nella popolazione
int sim_time; 		// durata simulazione
int birth_death;	//	intervallo di tempo per uccisione
static int terminazione = 0; //	accesso nei due IF all'interno di SIGALRM_handler per gestione terminazione simulazione
int tipo_a = 0;	//	per evitare creazione solo A || B
char text_to_a[30]; //	stringa che contiene messaggio da inviare ad A
char text_to_b[30]; //	stringa in cui scrivo il messaggio diretto a b
int i_moglie; //	indice processo A
int i_marito; //	indice del processo B che contatta/ da cui viene contattata
pid_t child_pid, alarm_pid, new_child_pid;
int status; //	dove salvo il valore di EXIT dei processi A-B
int migliore_snd=-1; //	indice processo A migliore da essere contattato per un determinato B || =1 perchè setto il primo evito errori
int mcd_b;
int mcd_massimo=1; //	negativo cosi so che nel primo richiamo di max non rimane questo
int i, j, k, l, z,r; //	r ciclo interno father
int s_id, m_id, msq_id; //	ID : sem, shm, msq
int rcv_byte = 0; //	byte ricevuti da A inviati da B
int byte_rcv = 0;	//	byte ricevuti da B inviati da A
int insert;	//	indice per trovare posizione libera in my_data->vec[X]
int dim_range = 0;

char *names[] = { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", \
                 "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z" };
int indice_nome; //intero che tiro a sorte da 0 a 25 per selezionare lettera

// SHARED MEMORY
struct single_data {
	char type; //	Tipo processo ('A' o 'B')
  char name [65535]; //	Nome processo (Da 'A' a 'Z')
  unsigned long genome; //	Genoma processo (Da 2 a 2+genes)
  int morto;	// 0 vivo, 1 morto
  pid_t my_pid; //	Pid processo scrittore
  int i_partner; //	Indice partner con cui ti stai per riprodurre
	int riprodotto; // 0 non riprodotto , 1 riprodotto.
};

struct shared_data {
  unsigned long cur_idx; //	Indice dell'array per prendere una struttura
  struct single_data vec[10000];
  int indice_totale; //	Numero totale processo
	int all_generated; //	0 non pronti , 1 pronto
	int end;	// condizione per avviare wait e terminazione
	pid_t loop_pid; // pid del processo che esegue SIGALRM_handler, per inviargli SIGKILL
	pid_t pid_gestore;
};


// SEMAFORI
union semun {
	int val;				// valore per SETVAL
	struct semid_ds* buf;	// buffer per IPC_STAT, IPC_SET
	unsigned short* array;	// array per GETALL, SETALL
	#if defined(__linux__)	// parte specifica di Linux
	struct seminfo* __buf;	// buffer per IPC_INFO
	#endif
};

// MESSAGE QUEUE
struct msgbuf {
	long msg_type;					// tipo del messaggio da inviare
	char msg_text[MSG_MAX_SIZE];	// testo del messaggio
};

// PROTOTIPI DI FUNZIONI
extern int initSemAvaiable(int semId, int semNum);
extern int initSemInUse(int semId, int semNum);
extern int reserveSem(int semId, int semNum);
extern int releaseSem(int semId, int semNum);
int max(int a, int b);
int mcd(int a, int b);
void father(char *arg[],char *argv[]);
void creation(int indice, pid_t pid);
void creationNewSons(int indice, pid_t pid, int indice_A, int indice_B);
void SIGALRM_handler();
void SIGINT_handler();
void printPopulation();
void printStats();
void detach();
void endSimulation(int secondi);


// SEMAFORI

// Initialize semaphore to 1 (SEMAPHORE AVAILABLE)
int initSemAvaiable(int semId, int semNum) {
	union semun arg;
  	arg.val = 1;
  	return semctl(semId, semNum, SETVAL, arg);
}

// Initialize semaphore to 0 (SEMAPHORE IN USE)
int initSemInUse(int semId, int semNum) {
  	union semun arg;
  	arg.val = 0;
  	return semctl(semId, semNum, SETVAL, arg);
}

// Reserve semaphore - decrement it by 1
int reserveSem(int semId, int semNum) {	// Reserve semaphore - decrement it by 1
  	struct sembuf sops;
  	sops.sem_num = semNum;
  	sops.sem_op = -1;
  	sops.sem_flg = 0;
  	return semop(semId, &sops, 1); //1 è il numero delle operazioni
}

// Release semaphore - increment it by 1
int releaseSem(int semId, int semNum) {
  	struct sembuf sops;
  	sops.sem_num = semNum;
  	sops.sem_op = 1;
  	sops.sem_flg = 0;
  	return semop(semId, &sops, 1);
}

// calcola il massimo tra due numeri interi
int max(int a, int b) {
	return (a > b) ? a : b;
}

//calcola mcd tra due numeri interi
int mcd(int a, int b) {
	while (a != b) {
		if (a > b)
			a = a - b;
		else
			b = b - a;
	}
	return a;
}

/**
  * Si occupa della gestione del segnale SIGINT.
 	* uccide tutti i processi in esecuzione,
 	* esegue la wait per non lasciare processi zombie
 	* e conclude terminando l'esecuzione del programma
 */
void SIGINT_handler() {
	int i = 1;
	kill(my_data->loop_pid,SIGKILL);
	while(my_data->vec[i].my_pid > 0){
		my_data->vec[i].morto=1;
		kill(my_data->vec[i].my_pid,SIGKILL);
		i++;
	}
	while(waitpid(-1,&status,WNOHANG | WUNTRACED | WCONTINUED) >0);

	detach();
	exit(-1); //termino esecuzione processo
}

/**
 * La funzione creation inizializza tutti i valori di
 * un processo in meoria condivisa
 * @param indice [valore inidice in memoria condivisa]
 * @param pid    [pid del processo associato a quell'inidice]
 */
void creation(int indice, pid_t pid) {
	int i;
	int num_totale=0;
	srand(getpid());
	for(i=1; my_data->vec[i].my_pid > 0; i++) { //controlla < =
		num_totale++;
		if(my_data->vec[i].type=='A')
			tipo_a++;
	}

	/**
	 * Controllo se ci sono solo processi A o B e forzo la generazione
	 * in caso fosse vero che ci sono solo A o solo B.
	 * @param tipo_a [Parametro conteggiato in precedenza per controllare la
	 * diversità degli individui]
	 */
	if(tipo_a == num_totale)	//se sono presenti solo processi A allora creo un B
			my_data->vec[indice].type = 'B';

	else if(tipo_a == 0)	//se sono presenti solo processi B allora creo un A
		my_data->vec[indice].type= 'A';

	else	//altrimenti random
		my_data->vec[indice].type = rand() % ('B' - 'A' + 1) + 'A';

	indice_nome = rand() % (26 - 0);
    strcpy(my_data->vec[indice].name, (names[indice_nome])); //names[indice_nome];
    my_data->vec[indice].genome = rand() % (2 + genes - 2 + 1) + 2;
    my_data->vec[indice].my_pid = pid;
    my_data->vec[indice].morto = 0;
    my_data->vec[indice].riprodotto = 0;
    my_data->vec[indice].i_partner = -1;
	if(my_data->end == 1){
		exit(0);
	}
}

/**
 * La funzione creationNewSons è simile alla funzione creation,
 * con la differenza che certe caratteristiche del processo che stiamo
 * creando sono date dai due processi di tipo A e B che l'hanno generato.
 *
 * Per ulteriori informazioni consultare documento progetto.
 * @param indice   [indice in memoria condivisa del processo che stiamo per creare]
 * @param pid      [pid processo che stiamo per creare]
 * @param indice_A [indice genitore tipo A]
 * @param indice_B [indice genitore tipo B]
 */
void creationNewSons(int indice, pid_t pid, int indice_A, int indice_B) { //indice= posizione nell'array "vec"
	int i;
	int lunghezza=strlen(my_data->vec[indice_A].name) + strlen(my_data->vec[indice_B].name) +1;
	char buffer[lunghezza];
	int num_processi_tot=0;
	reserveSem(s_id, 0);
	srand(getpid());
	int x = mcd(my_data->vec[indice_A].genome, my_data->vec[indice_B].genome);
	for(i=1; my_data->vec[i].my_pid > 0; i++) {
		num_processi_tot++;
		if(my_data->vec[i].type=='A')
			tipo_a++;
	}

	/**
	 * Controllo se ci sono solo processi A o B e forzo la generazione
	 * in caso fosse vero che ci sono solo A o solo B.
	 * @param tipo_a [Parametro conteggiato in precedenza per controllare la
	 * diversità degli individui]
	 */
	if(tipo_a == num_processi_tot)	//se ci sono solo processi A creo un B
			my_data->vec[indice].type = 'B';
	else if(tipo_a == 0)	//se ci sono solo processi B creo un A
		my_data->vec[indice].type= 'A';
	else	//altrimenti random
		my_data->vec[indice].type = rand() % ('B' - 'A' + 1) + 'A';

	indice_nome = rand() % 26 + 0;
	sprintf(buffer,"%s%s%s",my_data->vec[indice_A].name,my_data->vec[indice_B].name,names[indice_nome]);
	strcpy(my_data->vec[indice].name ,buffer);
	my_data->vec[indice].genome = rand() % (x + genes - x + 1) + x;
  	my_data->vec[indice].my_pid = pid;
  	my_data->vec[indice].morto = 0;
	my_data->vec[indice].riprodotto = 0;
  	my_data->vec[indice].i_partner = -1;
	releaseSem(s_id, 0);
	if(my_data->end == 1) {
		exit(0);
	}

}

/**
 * Termina l'esecuzioone della simulazione
 * @param secondi [secondi dopo la quale termina]
 */
void endSimulation(int secondi) {
	sleep(secondi);

	int i = 1;
	kill(my_data->loop_pid,SIGKILL);
	while(my_data->vec[i].my_pid > 0){
		my_data->vec[i].morto=1;
		kill(my_data->vec[i].my_pid,SIGKILL);
		i++;
	}

	my_data->end=1; // condizione di accesso al secondo if in SIGALRM_handler
	kill(my_data->pid_gestore, SIGALRM);
	printf("ELIMINATI TUTTI I FIGLI\n");
	exit(-1);
}

/**
 * Dealloca tutte le IPC
 */
 void detach() {
 	shmctl(m_id,IPC_RMID,NULL); //shared_memory
 	semctl(s_id, 0, IPC_RMID);	//semaphore
 	msgctl(msq_id,IPC_RMID,NULL);	//msg_queue
 }

 /**
  * Stampa le informazioni sulla popolazione
  */
void printPopulation() {
	printf("La popolazione di processi, composta da %d individui, ", init_people);

	printf("ha le seguenti caratteristiche:\nNUM:\tPID:\tTYPE:\tGENOME:\tNAME:\n");
	for(k = 1; k <= 10000 && my_data->vec[k].my_pid > 0; k++) {
		printf("%d: \t", k);
		if(my_data->vec[k].morto == 1)
			printf("R.I.P\t");
		else
			printf("%d\t", my_data->vec[k].my_pid);
	   	printf("%c\t%lu\t%s\r", my_data->vec[k].type, my_data->vec[k].genome, my_data->vec[k].name);
	   	printf("\n");
   }
}

/**
 * Stampa le statisticeh finali
 */
void printStats() {
 	int totA = 0;
	int totB = 0;
	int maxlen = 0;
	int idxlen = 0;
	int maxgen = 0;
	int idxgen = 0;
	for(i = 1; my_data->vec[i].my_pid > 0; i++) {
		if(my_data->vec[i].type == 'A')
			totA++;
		else
			totB++;
		if((strlen(my_data->vec[i].name) > maxlen)) {
			maxlen = max(strlen(my_data->vec[i].name), maxlen);
			idxlen = i;
		}
		if(my_data->vec[i].genome > maxgen) {
			maxgen = max(my_data->vec[i].genome, maxgen);
			idxgen = i;
		}

	}
	printPopulation();
	printf("\n\n\n\n\nLa simulazione è terminata.\nSono stati generati %d processi di cui ", i-1);	//-1 perchè esce dal ciclo
	printf("%d di tipo A e %d di tipo B\n\n", totA, totB);
	sleep(2);
	printf("Il processo con il genoma più grande è il numero %d e ha tali caratteristiche: ",idxgen);
	sleep(2);
	printf("\nNUM:\tPID:\tTYPE:\tGENOME:\tNAME:\n");
	printf("%d\t%d\t%c\t%lu\t%s\r\n\n",idxgen, my_data->vec[idxgen].my_pid,my_data->vec[idxgen].type, my_data->vec[idxgen].genome, my_data->vec[idxgen].name);
	sleep(2);
	printf("Il processo con il nome più lungo è il numero %d e il nome è %s :", idxlen,my_data->vec[idxlen].name);
	sleep(2);
	printf("\nNUM:\tPID:\tTYPE:\tGENOME:\tNAME:\n");
	printf("%d\t%d\t%c\t%lu\t%s\r",idxlen,my_data->vec[idxlen].my_pid, my_data->vec[idxlen].type, my_data->vec[idxlen].genome, my_data->vec[idxlen].name);

	sleep(3);
	printf("\n\n\n\t\t\tLA SIMULAZIONE È TERMINATA\n\n\n");
	sleep(1);
	printf("\n\n\n\t\t\tprogetto a cura di:\n\t\t\t\tBonicco Paolo, matricola 833708\n");
	printf("\t\t\t\tBruno Federico, matricola 835438\n");
	printf("\t\t\t\tFancellu Andrea, matricola 838776\n\n\n");
	printf("\t\t\tTHE END\n\n\n\n\n");

}


/**
	* Stampa le caratteristiche della popolazione in tempo reale.
	* Alla prima invocazione inoltre richiama la funzione endSimulation
	* che avvierà la simulazione dopo sim_time secondi decisi dall'utente.
	* Dopo sim_tim secondi la variabile my_data-> end verrà settata a 1.
	* Quindi dopo aver eseguito la wait verranno stampate delle statistiche
	* relative alla simulazione , al termine delle quali verranno rimosse
	* le IPC structure e la simulazione terminerà.
*/
void SIGALRM_handler() {
	  if(terminazione == 0){ //eseguo una sola volta
	  		terminazione++;
	      if(fork() == 0){
	      		endSimulation(sim_time); //invoco funzione per terminazione
							 											//sim_time= durata esecuzione progetto
	      }
	  }
	  if(my_data->end == 1){
	      printf("sto eseguendo la wait \n");
	  	  while(waitpid(-1,&status,WNOHANG | WUNTRACED | WCONTINUED) >0);
	  	  sleep(3);
	      printStats();
	      detach();
	      exit(0);
	  }
	  printPopulation();
}
