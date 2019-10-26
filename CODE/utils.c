#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <termios.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "utils.h"
#include "error.h"
#include "DataLayer.h"

void setLinkLayer(linkLayer *linkLayer, char port[])
{
    strcpy(linkLayer->port, port);
    linkLayer->baudRate = BAUDRATE;
    linkLayer->sequenceNumber = 1; //TODO put smth here that i dont know
    linkLayer->timeout = TIMEOUT;
    linkLayer->numTransmissions = ATEMPTS;
}

char *makeControlPacket(int type, char path[], off_t filesize, int *controlPackLen)
{

    unsigned int L1 = sizeof(filesize);
    unsigned int L2 = strlen(path);

    *controlPackLen = 5 + L1 + L2; //5 = C + T1 + L1 + T2 + L2 + (char) path length is byte
    char *controlPacket = (char *)malloc(*controlPackLen);

    char c;
    switch (type)
    {
    case (START):
        c = C2;
        break;
    case (END):
        c = C3;
        break;
    }
    controlPacket[0] = c;                         //C
    controlPacket[1] = T1;                        //T1
    controlPacket[2] = L1;                        //L1
    *(off_t *)(controlPacket + 3) = filesize;     //V1
    controlPacket[3 + L1] = T2;                   //T2
    controlPacket[4 + L1] = (char)L2;             //L2
    strcat((char *)controlPacket + 5 + L1, path); //V2

    return controlPacket;
}

char *makeDatePacket(char data[], int *dataPackLen, off_t filesize, linkLayer *linkLayer)
{

    *dataPackLen = 4 + filesize; // C+N+L1+L2
    char *dataPacket = (char *)malloc(*dataPackLen);

    dataPacket[0] = C1;
    dataPacket[1] = linkLayer->sequenceNumber % 255; //N – número de sequência (módulo 255)
    dataPacket[2] = (char)filesize / 256;            //L2 L1 – indica o número de octetos (K) do campo de dados (K = 256 * L2 + L1)
    dataPacket[3] = (char)filesize % 256;            //L2 L1 – indica o número de octetos (K) do campo de dados (K = 256 * L2 + L1)
    strcat((char *)dataPacket + 5, data);            //P1 ... PK – campo de dados do pacote (K octetos)
    //memcpy(dataPacket+4,data,);
    //increment sequenceNumber
    linkLayer->sequenceNumber++;

    return dataPacket;
}

void set_reception(supervision_instance_data_t *machine, unsigned char pack)
{
    switch (machine->state)
    {
    case (start):
        if (pack == FLAG)
            machine->state = flag_rcv;
        break;
    case (flag_rcv):
        if (pack == A_3)
        {
            machine->state = a_rcv;
            break;
        }
        if (pack != FLAG)
            machine->state = start;
        break;
    case (a_rcv):
        if (pack == SET)
        {
            machine->state = c_rcv;
            break;
        }
        if (pack == FLAG)
        {
            machine->state = flag_rcv;
            break;
        }
        machine->state = start;
        break;
    case (c_rcv):
        if (pack == (A_3 ^ SET))
        {
            machine->state = bcc_ok;
            break;
        }
        if (pack == FLAG)
        {
            machine->state = flag_rcv;
            break;
        }
        machine->state = start;
        break;
    case (bcc_ok):
        if (pack == FLAG)
        {
            machine->state = stop;
            break;
        }
        machine->state = start;
        break;
    case (stop):
        break;
    }
}

void ua_reception(supervision_instance_data_t *machine, unsigned char pack)
{
    switch (machine->state)
    {
    case (start):
        if (pack == FLAG)
            machine->state = flag_rcv;
        break;
    case (flag_rcv):
        if (pack == A_3)
        {
            machine->state = a_rcv;
            break;
        }
        if (pack != FLAG)
            machine->state = start;
        break;
    case (a_rcv):
        if (pack == UA)
        {
            machine->state = c_rcv;
            break;
        }
        if (pack == FLAG)
        {
            machine->state = flag_rcv;
            break;
        }
        machine->state = start;
        break;
    case (c_rcv):
        if (pack == (A_3 ^ UA))
        {
            machine->state = bcc_ok;
            break;
        }
        if (pack == FLAG)
        {
            machine->state = flag_rcv;
            break;
        }
        machine->state = start;
        break;
    case (bcc_ok):
        if (pack == FLAG)
        {
            machine->state = stop;
            break;
        }
        machine->state = start;
        break;
    case (stop):
        break;
    }
}

void disc_reception(supervision_instance_data_t *machine, unsigned char pack)
{
    switch (machine->state)
    {
    case (start):
        printf("start\n");
        if (pack == FLAG)
            machine->state = flag_rcv;
        break;
    case (flag_rcv):
        printf("flag\n");
        if (pack == A_3)
        {
            machine->state = a_rcv;
            break;
        }
        if (pack != FLAG)
            machine->state = start;
        break;
    case (a_rcv):
        printf("a\n");
        if (pack == DISC)
        {
            machine->state = c_rcv;
            break;
        }
        if (pack == FLAG)
        {
            machine->state = flag_rcv;
            break;
        }
        machine->state = start;
        break;
    case (c_rcv):
        printf("c\n");
        if (pack == (A_3 ^ DISC))
        {
            machine->state = bcc_ok;
            break;
        }
        if (pack == FLAG)
        {
            machine->state = flag_rcv;
            break;
        }
        machine->state = start;
        break;
    case (bcc_ok):
        printf("bcc\n");
        if (pack == FLAG)
        {
            machine->state = stop;
            break;
        }
        machine->state = start;
        break;
    case (stop):
        break;
    }
}

unsigned char BCC_make(char *buffer, int size)
{
    unsigned char BCC;
    BCC = buffer[0];
    for (int i = 1; i < size; i++)
    {
        BCC = BCC ^ buffer[i];
    }
    return BCC;
}

unsigned char *BCC_stuffing(unsigned char BCC)
{
    //todo checka isto, porque nao sei o que queres guardar na string
    unsigned char *BCC_stuffed = malloc(sizeof(char));
    if (BCC == ESC)
    {
        BCC_stuffed[0] = ESC;
        BCC_stuffed[1] = ESC_NEXT;
    }
    else if (BCC == FLAG)
    {
        BCC_stuffed[0] = ESC;
        BCC_stuffed[1] = FLAG_NEXT;
    }
    return BCC_stuffed;
}

unsigned char *startFileName(unsigned char *start)
{
    int aux = (int)start[8];
    int i = 0;
    unsigned char *fileName = (unsigned char *)malloc(aux + 1);

    while (i < aux)
    {
        fileName[i] = start[i + 9];
        i++;
    }
    fileName[aux] = '\0';
    return fileName;
}

void setThingsFromStart(off_t *sizeOfAllMessages, unsigned char *fileName, unsigned char *startTransmition)
{
    int aux = (int)startTransmition[8];
    int i = 0;
    unsigned char *fileNameAux = (unsigned char *)malloc(aux + 1);

    while (i < aux)
    {
        fileNameAux[i] = startTransmition[i + 9];
        i++;
    }
    fileNameAux[aux] = '\0';

    *fileName = *fileNameAux;
    *sizeOfAllMessages = (off_t)(startTransmition[3] << 24) | (startTransmition[4] << 16) | (startTransmition[5] << 8) | (startTransmition[6]);
}

int endReached(unsigned char *message, int sizeOfMessage, unsigned char *startTransmition, int sizeOfStartTransmition)
{
    if (sizeOfMessage != sizeOfStartTransmition)
    {
        return FALSE;
    }
    else
    {
        if (message[0] == C3)
        {
            for (size_t i = 1; i <= (size_t)sizeOfMessage; i++)
            {
                if (message[i] != startTransmition[i])
                {
                    return FALSE;
                }
            }
        }
        else
            return FALSE;
    }
return TRUE;
}

unsigned char *headerRemoval(unsigned char *message, int sizeOfMessage, int *sizeWithNoHeader)
{

    unsigned char *aux = (unsigned char *)malloc(sizeOfMessage - 4);

    for (size_t i = 0; (i + 4) < (size_t)sizeOfMessage; i++)
    {
        aux[i] = message[i + 4];
    }

    *sizeWithNoHeader = sizeOfMessage - 4;
    return aux;
}

int checkBCC2(unsigned char *message, int sizeMessage)
{
  int i = 1;
  unsigned char BCC2 = message[0];
  for (; i < sizeMessage - 1; i++)
  {
    BCC2 ^= message[i];
  }
  if (BCC2 == message[sizeMessage - 1])
  {
    return TRUE;
  }
  else
    return FALSE;
}

void sendControlMessage(int fd, unsigned char C)
{
  unsigned char message[5];
  message[0] = FLAG;
  message[1] = A_3;
  message[2] = C;
  message[3] = message[1] ^ message[2];
  message[4] = FLAG;
  write(fd, message, 5);
}

