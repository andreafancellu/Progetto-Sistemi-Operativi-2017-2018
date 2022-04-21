#include "util.h"
int main(int argc, char *argv[]) {

    printf("\n\t\tPROGETTO SISTEMI OPERATIVI 2017/2018\n\n");
    printf("INSERIRE IL NUMERO DI PROCESSI CON CUI SI VUOLE INIZIARE LA SIMULAZIONE\n\t\t\t");
    scanf("%d", &init_people);
    if(init_people <= 1) {
        printf("\nLA SIMULAZIONE NECESSITA DI ALMENO 2 PROCESSI");
        exit(-1);
    }

    printf("\n\nINSERIRE LA DURATA IN SECONDI DELLA SIMULAZIONE\n\t\t\t");
    scanf("%d", &sim_time);
    if(sim_time <=1) {
        printf("\nLA SIMULAZIONE DEVE DURARE ALMENO 1 SECONDO");
        exit(-1);
    }

    printf("\n\nINSERIRE OGNI QUANTI SECONDI SI VUOLE UCCIDERE UN PROCESSO\n\t\t\t");
    scanf("%d", &birth_death);
    if(birth_death < 1) {
        printf("\nL'INTERVALLO DEVE VALERE ALMENO 1 SECONDO");
        exit(-1);
    }

    printf("\n\nINSERIRE IL VALORE DI GENES\n\t\t\t");
    scanf("%d", &genes);
    if(genes < 0) {
        printf("\nIL GENOMA DEVE VALERE ALMENO 2");
        exit(-1);
    }


    // CHIAVI PER IPC
    key_shm=getpid();   //chiave shared_memory
    key_sem=getpid()+1; //chiave semafori
    key_msq=getpid()+2; //chiave coda_messaggi


    //GESTIONE SEGNALE SIGKINT
    struct sigaction ctrlc;
    ctrlc.sa_handler = SIGINT_handler;
    sigemptyset(&ctrlc.sa_mask);
    ctrlc.sa_flags = 0;
    sigaction(SIGINT, &ctrlc, NULL);

    //GESTIONE SEGNALE SIGALRM
    struct sigaction alrm;
    alrm.sa_handler = SIGALRM_handler;
    sigemptyset(&alrm.sa_mask);
    alrm.sa_flags = 0;
    sigaction(SIGALRM, &alrm, NULL);

    //SEMAPHORE
    s_id = semget(key_sem, 4, 0666 | IPC_CREAT | IPC_CREAT);
    TEST_ERROR;
    initSemAvaiable(s_id, 0);
    TEST_ERROR;
    initSemInUse(s_id, 1);
    TEST_ERROR;
    initSemAvaiable(s_id, 2);
    TEST_ERROR;
    initSemAvaiable(s_id, 3);
    TEST_ERROR;

    /**
     * DESCRIZIONE SEMAFORI UTILIZZATI:
     * (s_id , 0) --> Viene utilizzato per l'accesso in memoria condivisa,
     * in modo che quando un processo va a legger eun valore in memoria condivisa
     * è sicuro che quel valore non sta per essere modificato e quindi evita una
     * inconsistenza del dato
     *
     * (s_id , 1) --> Viene utilizzato per sbloccare la funzione father ogni
     * birth_death secondi
     *
     * (s_id , 2) --> Viene utilizzato per la sincronizzazione tra il gestore e
     * i vari processi. Compare nella funzione father e in fileB, questo serve per
     * far si che il gestore (funzione father) non termini un processo B mentre è in una
     * sezione critica (ad esempio sta aspettando la risposta di un A).
     * Non è stato necessario sincronizzare anche A con il semaforo perchè è già
     * stato gestito con i la msg_rcv e msg_snd. B quando contatta un A tiene riservato
     * il semafoto fino a quando A non l'ho contatta di nuovo, così che il gestore
     * non possa uccidere nessuno.
     *
     * (s_id , 3) --> Sincronizza la creazione dei figli nella prima parte della
     * funzione father.
     */

    //SHARED MEMORY
    m_id = shmget(key_shm, sizeof(*my_data), IPC_CREAT |IPC_EXCL | 0666); //Creazione SHM se ancora no esiste
    TEST_ERROR;
    my_data = shmat(m_id, NULL, 0); //Attaching SHM
    TEST_ERROR;

    //MESSAGE QUEUE
    msq_id = msgget(key_msq, IPC_CREAT | 0666); //Creazione Message Queue
    TEST_ERROR;

    char *arg[3];
    arg[0] = (char*) malloc(sizeof(int));
    arg[1] = (char*) malloc(sizeof(int));
    arg[2] = NULL;

    //INIZIALIZZAZIONE variabili shared_memory

    /**
     * Ad ogni exceve verrà decrementata all_generated, così quando sarà uguale
     * a 0 ogni processo avrà eseguito la execve e inizierà il ciclo di vita dei
     * processi A e B.
     */
    my_data->all_generated = init_people;

    my_data->end = 0; //condizione di fine esecuzione
    my_data->pid_gestore = getpid();

    for(i = 1; i <= init_people; i++) {
        switch (child_pid = fork()) {
            case -1:  //  FORK ERROR
                TEST_ERROR;
                printf("Error in fork()\n");
                exit(EXIT_FAILURE);

            case 0: //  Esecuzione del figlio
                reserveSem(s_id, 0);
                TEST_ERROR;

                //  Salviamo le informazioni del processo in shared_memory
                creation(i, getpid());
			          TEST_ERROR;

                releaseSem(s_id, 0);
                TEST_ERROR;

    	          reserveSem(s_id, 0);
                sprintf(arg[0], "%d", i);
                sprintf(arg[1], "%d", s_id);

    	        if(my_data->vec[i].type == 'A') {
                    my_data->all_generated--;
                    releaseSem(s_id, 0);
                    execve(argv[1], arg, NULL);
    		        TEST_ERROR;
    	        } else {
                  my_data->all_generated--;
                  releaseSem(s_id, 0);
  		            execve(argv[2], arg, NULL);
                  TEST_ERROR;
    	        }

              printf("Error\n");
              TEST_ERROR;
              exit(-1);

            default:
                break;
        }
    }
    /**
      * La variabile my_data->all_generated viene inizializzata a init_people
      * prima dell'esecuzione di ogni execve viene decrementata questa variabile.
      * quando l'ultimo processo eseguira' la execve la simulazione potrà
      * avere inizio.
      */
    while(my_data->all_generated >0);//aspetto creazione di tutti gli init_people figli

    //tutti i processi generati, la simulazione ha inizio.
    printf("All process generated\n");

    /**
      * invochiamo la funzione SIGALRM_handler che effettua la Stampa
      * della situazione attuale della simulazione. Inoltre alla prima
      * invocazione viene fatto partire il timer che determinerà la
      * durata della simulazione.
      */
    SIGALRM_handler();

    TEST_ERROR;

    if((alarm_pid = fork()) == 0) {
        TEST_ERROR;
        while(1) {
            my_data->loop_pid = getpid();
            sleep(birth_death);
            releaseSem(s_id, 1);
            kill(getppid(), SIGALRM); //ogni tot sec manda stampa dei figli
            if(my_data->end == 1){
                printf("sto avviando la procedura di terminazione dell'esecuzione \n");
                exit(0);
            }
        }
    }
    father(arg, argv);
}

void father(char *arg[],char *argv[]) {
    /**
      * Cerca i processi A e B che si sono accoppiati e si occupa della creazione
      * dei nuovi processi figlio. Utilizza la funzione creationNewSons per inserire
      * i nuovi processi in shared_memory con le caratteristiche eredite
      * dal processo A e dal processo B.
      */
    while(1) {
        for(r = 1;my_data->vec[r].my_pid > 0; r++) {

            //  Se il processo ha fatto figlio e ancora non l'ho creato il figlio
            if(my_data->vec[r].i_partner > 0 && my_data->vec[r].riprodotto == 0) {
                my_data->vec[r].riprodotto = 1;
                if((new_child_pid = fork()) == 0) {
                    reserveSem(s_id,3);
                    insert = 1;
                    while(my_data->vec[insert].my_pid > 0)
                        insert++;

                    creationNewSons(insert, getpid(), r, my_data->vec[r].i_partner);

                    sprintf(arg[0], "%d", insert);
                    sprintf(arg[1], "%d", s_id);

                    if((my_data->vec[insert].type)=='A') {
                        releaseSem(s_id,3);
                        execve(argv[1],arg,NULL);
                    } else {
                        releaseSem(s_id,3);
                        execve(argv[2],arg,NULL);
                    }
                }
            }
        }
        /**
          * Sceglie il primo processo che è ancora vivo
          * e che non si sta attualmente riproducento e
          * lo termina. Crea qunidi un altro processo con caratteristiche
          * casuali e lo immette nella popolazione al posto di
          * quello precedentemente rimosso.
          */

        reserveSem(s_id, 2);
        j = 1;
        while(my_data->vec[j].morto == 1 || my_data->vec[j].i_partner > 0)
          j++;

        if(my_data->vec[j].my_pid > 0) { //sicurezza di non puntare ad un processo che non esiste.

            reserveSem(s_id, 0);
            my_data->vec[j].morto = 1;
            releaseSem(s_id, 0);
            kill(my_data->vec[j].my_pid, SIGKILL); //termino processo selezionato

            if((child_pid = fork()) == 0) { //creazione nuove processo da immettere in popolazione
                insert = 1;
                while(my_data->vec[insert].my_pid > 0)
                    insert++;

                creation(insert, getpid()); //invoco creation per inserire caratteristiche processo in shared
                sprintf(arg[0], "%d", insert);
                sprintf(arg[1], "%d", s_id);

                if((my_data->vec[insert].type)=='A') {
                    execve(argv[1], arg, NULL);
                } else {
                    execve(argv[2], arg, NULL);
                }
            }
            releaseSem(s_id, 2);
        } else {
            releaseSem(s_id, 2);
        }
        /**
          * Il gestore  si addormenta sul semaforo (s_id,1) che verrà
          * sbloccato solamente dopo birth_death secondi permettendogli
          * di continuare l'esecuzione delle azioni precedentemente viste.
          */
        reserveSem(s_id, 1);
    }

    printf("ERRORE: terminazione inaspettata.\n");
}
