#include "util.h"

int main(int argc, char *argv[]) {

    i = atoi(argv[0]);  //indice del processo corrente in shared_memory
    s_id = atoi(argv[1]); //Semaphore
    key_shm=getppid();   //chiave shared_memory
    key_sem=getppid()+1; //chiave semafori
    key_msq=getppid()+2; //chiave coda_messaggi

    msq_id = msgget(key_msq, 0666);
    m_id = shmget(key_shm, sizeof(*my_data), IPC_CREAT | 0666);
    my_data = shmat(m_id, NULL, 0);

    while(my_data->all_generated > 0); //aspetto la creazione di tutti i processi

    while(1) {
        /**
        * leggo solo i messaggi con tipo i, corrisponde al mio indice,
        * (il processo B invia con tipo I )
        */
        msgrcv(msq_id, &receive_from_b, (sizeof(receive_from_b)-sizeof(long)), i, 0);//leggo solo i messaggi con tipo i, corrisponde al mio indice, (il processo B invia con tipo I )

        i_marito = atoi(receive_from_b.msg_text); //salvo l'indice del marito che mi ha contattato

        /**
          *  Ciclo tutti i processi di tipo B per valutare quale può garantirmi
          *  il genoma con l'mcd massimo
          */
        mcd_massimo = 0;
        for(int z =1; my_data->vec[z].my_pid>0 ; z++){
            if(my_data->vec[z].morto == 0 && my_data->vec[z].type == 'B') {
                mcd_b = mcd(my_data->vec[z].genome, my_data->vec[i].genome);
                mcd_massimo = max(mcd_massimo, mcd_b);

                if(mcd_massimo == mcd_b)
                    migliore_snd = z;
            }
        }

        /**
          * Se il processo che ha contattato A corrisponde al migliore che A può
          * trovare o se il processo B ha genoma Multiplo di A oppure
          * se A ha gia rifiutato 10 processi B allora si accoppia con il
          * B che l'ha contattato (setta la variabile .morto a 1 per entrambi i
          * processi così che il gestore non può più ucciderli e nessun altro B può
          * contattare questo processo A), altrimenti informa B così che B possa
          * rimettersi in cerca di un altro porcesso A con cui accoppiarsi.
          */
        if(i_marito == migliore_snd || (my_data->vec[i_marito].genome % my_data->vec[i].genome) == 0 || dim_range > 10) {

          // aggiorno i dati in memoria condivisa dei due processi accoppiati
            reserveSem(s_id, 0);
            my_data->vec[i_marito].morto = 1;
            my_data->vec[i].morto = 1;
            my_data->vec[i].i_partner = i_marito;
            releaseSem(s_id, 0);

            /**
              * Il processo manda il suo indice al processo B che l'ha
              * contattato, se invio num >0 B capisce che ho accettato
              * la sua proposta di riproduzione.
              *
              * Copio la stringa text_to_b contenente il mio indice.
              *
              * Come tipo uso indice del padre che deve leggere msg
              */
            sprintf(text_to_b, "%d", i);
            strcpy(send_to_b.msg_text, text_to_b);
            send_to_b.msg_type = i_marito;

            //  mando messaggio a B che accetto e voglio riprodurmi
            msgsnd(msq_id, &send_to_b, (sizeof(send_to_b)-sizeof(long)), 0);

            // termino perchè ho finito il mio ciclo di vita
            exit(1);

        } else {
          /**
           * Comunico al processo B il rifiuto, salvo -1 come testo del
           * messaggio, questo per B significa che ho rifiutato la sua
           * richiesta di accoppiamento, lo sblocco dalla sua receive e
           * gli permetto di inviare altre richiste.
           * Imposto come tipo del messaggio l'indice del processo B che sto
           * contattando, così da sbloccare la sua msg_rcv.
           */
            sprintf(text_to_b, "%d",-1);
            dim_range++;
            strcpy(send_to_b.msg_text, text_to_b);
            send_to_b.msg_type = i_marito;

            msgsnd(msq_id, &send_to_b, (sizeof(send_to_b)-sizeof(long)), 0);
        }
    }
}
