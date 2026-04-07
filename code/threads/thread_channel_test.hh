// thread_channel_buffer_test.hh
// Test de canal: múltiples emisores envían números a múltiples receptores.
// Los receptores suman todos los valores recibidos en una variable global.
// Al finalizar se verifica que la suma sea la esperada.

#ifndef THREAD_CHANNEL_TEST_H
#define THREAD_CHANNEL_TEST_H

// Cantidad de valores a enviar en total.
#define TOTAL_MSGS     50

// Cantidad de emisores y receptores (se pueden variar).
#define NUM_SENDERS    1
#define NUM_RECEIVERS  20

void ThreadTestChannel();

#endif
