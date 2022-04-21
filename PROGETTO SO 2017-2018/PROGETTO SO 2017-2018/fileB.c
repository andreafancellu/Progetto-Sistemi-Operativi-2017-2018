//LIFECYCLE PROCESS B

#include "util.h"

int main(int argc, char *argv[]) {

    i = atoi(argv[0]);  //indice del processo corrente in shared_memory
    s_id = atoi(argv[1]);   //id per semafori
    key_shm=getppid();   //chiave shared_memory
    key_sem=getppid()+1; //chiave semafori
    key_msq=getppid()+2; //chiave coda_messaggi

    msq_id = msgget(key_msq, 0666);
    m_id = shmget(key_shm, sizeof(*my_data), IPC_CREAT |0666);
    my_data = shmat(m_id, NULL, 0);
    TEST_ERROR;

    while(my_data->all_generated > 0); //aspetto la creazione di tutti i processi

    while(1) {
        reserveSem(s_id, 2); //un B alla volta cerca

        /**
        * Ciclo tutti i processi di tipo A non ancora morti e cerco quello che
        * mi garantisca un figlio con l'mcd massimo, e salvo il suo indice
        */
        mcd_massimo = 0;
        for(int z=1; my_data->vec[z].my_pid > 0; z++){
            if(my_data->vec[z].morto == 0 && my_data->vec[z].type == 'A') {
                mcd_b = mcd(my_data->vec[z].genome, my_data->vec[i].genome);
                mcd_massimo = max(mcd_massimo,mcd_b);
                if(mcd_massimo == mcd_b)
                    migliore_snd = z;
            }
        }

        /**
        * Salvo l'indice del processo migliore come tipo del messaggio da
        * inviare, così che verrà ricevuto dal processo A interessato
        */
        sprintf(text_to_a, "%d", i);
        send_to_a.msg_type = migliore_snd;
        strcpy(send_to_a.msg_text, text_to_a);

        /**
        * Ulteriore verifica su A per vedere che non si stato ucciso nel mentre,
        * probabilmente è inutile quindi andrà tolta.
        * Converto a intero il messaggio inviato da A: Se A invia num negativo
        * indica che ha rifiutato altrimenti ha inviato il suo indice i della
        * struttura my_data->vec[i].
        */
        if(migliore_snd > 0 && my_data->vec[migliore_snd].morto == 0) {

            msgsnd(msq_id, &send_to_a, (sizeof(send_to_a)-sizeof(long)), 0);

            msgrcv(msq_id,&receive_from_a,(sizeof(receive_from_a)-sizeof(long)), i, 0);

            releaseSem(s_id, 2);

            i_moglie = atoi(receive_from_a.msg_text);

            if(i_moglie>0) {  //  se ha accettato proposta
                reserveSem(s_id,0);
                my_data->vec[i].morto = 1;

              // aggiungo indice moglie nella mia shared_memory
                my_data->vec[i].i_partner = i_moglie;
                releaseSem(s_id,0);

                exit(0);
            }
        } else
            releaseSem(s_id,2);

    }
}
