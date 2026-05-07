/// Entry points into the Nachos kernel from user programs.
///
/// There are two kinds of things that can cause control to transfer back to
/// here from user code:
///
/// * System calls: the user code explicitly requests to call a procedure in
///   the Nachos kernel.  Right now, the only function we support is `Halt`.
///
/// * Exceptions: the user code does something that the CPU cannot handle.
///   For instance, accessing memory that does not exist, arithmetic errors,
///   etc.
///
/// Interrupts (which can also cause control to transfer from user code into
/// the Nachos kernel) are handled elsewhere.
///
/// For now, this only handles the `Halt` system call.  Everything else core-
/// dumps.
///
/// Copyright (c) 1992-1993 The Regents of the University of California.
///               2016-2021 Docentes de la Universidad Nacional de Rosario.
/// All rights reserved.  See `copyright.h` for copyright notice and
/// limitation of liability and disclaimer of warranty provisions.


#include "transfer.hh"
#include "syscall.h"
#include "filesys/directory_entry.hh"
#include "threads/system.hh"

#include "filesys/file_system.hh"

#include <stdio.h>



static void
IncrementPC()
{
    unsigned pc;

    pc = machine->ReadRegister(PC_REG);
    machine->WriteRegister(PREV_PC_REG, pc);
    pc = machine->ReadRegister(NEXT_PC_REG);
    machine->WriteRegister(PC_REG, pc);
    pc += 4;
    machine->WriteRegister(NEXT_PC_REG, pc);
}

/// Do some default behavior for an unexpected exception.
///
/// NOTE: this function is meant specifically for unexpected exceptions.  If
/// you implement a new behavior for some exception, do not extend this
/// function: assign a new handler instead.
///
/// * `et` is the kind of exception.  The list of possible exceptions is in
///   `machine/exception_type.hh`.
static void
DefaultHandler(ExceptionType et)
{
    int exceptionArg = machine->ReadRegister(2);

    fprintf(stderr, "Unexpected user mode exception: %s, arg %d.\n",
            ExceptionTypeToString(et), exceptionArg);
    ASSERT(false);
}

/// Handle a system call exception.
///
/// * `et` is the kind of exception.  The list of possible exceptions is in
///   `machine/exception_type.hh`.
///
/// The calling convention is the following:
///
/// * system call identifier in `r2`;
/// * 1st argument in `r4`;
/// * 2nd argument in `r5`;
/// * 3rd argument in `r6`;
/// * 4th argument in `r7`;
/// * the result of the system call, if any, must be put back into `r2`.
///
/// And do not forget to increment the program counter before returning. (Or
/// else you will loop making the same system call forever!)
static void
SyscallHandler(ExceptionType _et)
{
    int scid = machine->ReadRegister(2);

    switch (scid) {

        case SC_HALT:
            DEBUG('e', "Shutdown, initiated by user program.\n");
            interrupt->Halt();
            break;

        case SC_CREATE: { // int Create(const char *name)
            int filenameAddr = machine->ReadRegister(4);

            // Si la dirección de memoria es nullptr, error
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filename[FILE_NAME_MAX_LEN + 1];

            // Si el tamaño del nombre del archivo es mayor al tamaño máximo, error
            if (!ReadStringFromUser(filenameAddr,
                                    filename, sizeof filename)) {
                DEBUG('e', "Error: filename string too long (maximum is %u bytes).\n",
                      FILE_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Create` requested for file `%s`.\n", filename);

            // Creamos el archivo exitosamente y devolvemos 0
            if (fileSystem->Create(filename,DEFAULT_FILE_SIZE)) {
                machine->WriteRegister(2, 0);
            }

            // En caso contrario error
            else {
                DEBUG('e', "Error: fileSystem->Create fails for file `%s`.\n", filename);
                machine->WriteRegister(2, -1);
            }

            break;
        }

        case SC_OPEN: {// OpenFileId Open(const char *name)
            // Obtenemos el nombre del archivo que queremos abrir
            int filenameAddr = machine->ReadRegister(4);
            
            // Si la dirección de memoria es nullptr, error
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filename[FILE_NAME_MAX_LEN + 1];

            // Si el tamaño del nombre del archivo es mayor al tamaño máximo, error
            if (!ReadStringFromUser(filenameAddr,
                                    filename, sizeof filename)) {
                DEBUG('e', "Error: filename string too long (maximum is %u bytes).\n",
                      FILE_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Open` requested for file `%s`.\n", filename); 
            
            // Abrimos el archivo
            OpenFile *openFile = fileSystem->Open(filename);

            // Si el archivo no se pudo abrir, devolvemos -1
            if (openFile == nullptr) {
                DEBUG('e', "Error: fileSystem->Open fails for file `%s`.\n", filename);
                machine->WriteRegister(2, -1);
            }
            else {
                // Guardamos el archivo en la tabla de archivos abiertos
                int fileId = currentThread->AddOpenFile(openFile);
                
                // Devolvemos al usaurio el fileID que obtuvimos al añadir el archivo
                machine->WriteRegister(2, fileId);
            }
            
            break;
        }

        case SC_CLOSE: {// int Close(OpenFileId id)
            // Obtenemos el fileID del archivo que queremos cerrar
            int fileID = machine->ReadRegister(4); 

            // Si el fileID es menor a cero, error
            if (fileID < 0) {
                DEBUG('e', "Error: invalid close, the fileID is lower than 0: `%d`.\n", fileID);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Close` requested for id %u.\n", fileID);

            // Buscamos el fileID dentro de la tabla de procesos 
            // del thread
            OpenFile * openFile = currentThread->RemoveOpenFile(fileID);

            // Si openFile == nullptr, devolvemos -1
            if (openFile == nullptr) {
                DEBUG('e', "Error: fileID `%d` is not related to an opened file.\n", fileID);
                machine->WriteRegister(2, -1);
                break;
            }

            // Cerramos el openFile
            delete openFile;

            // Devolvemos 0 para indicar que se cerró con éxito
            machine->WriteRegister(2, 0);
            break;
        }

        case SC_REMOVE: {// int Remove(const char *name)
            // Obtenemos el nombre del archivo que queremos abrir
            int filenameAddr = machine->ReadRegister(4);
            
            // Si la dirección de memoria es nullptr, error
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filename[FILE_NAME_MAX_LEN + 1];

            // Si el tamaño del nombre del archivo es mayor al tamaño máximo, error
            if (!ReadStringFromUser(filenameAddr,
                                    filename, sizeof filename)) {
                DEBUG('e', "Error: filename string too long (maximum is %u bytes).\n",
                      FILE_NAME_MAX_LEN);
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Remove` requested for file `%s`.\n", filename); 
            
            // NO PODEMOS CERRAR EL ARCHIVO YA QUE NO TENEMOS EL fileID
            // Removemos el archivo de la lista de archivos abiertos
            //OpenFile * openFile = currentThread->RemoveOpenFile(fileID);

            // Si el fileID está asociado a un archivo abierto, lo cerramos
            //if (openFile != nullptr) delete openFile;

            // Eliminamos el archivo
            if (!fileSystem->Remove(filename)) {
                DEBUG('e', "Error: fileSystem->Remove fails for file `%s`.\n", filename);
                machine->WriteRegister(2, -1);
                break;
            }

            // En caso contrario devolvemos 0 al usuario
            machine->WriteRegister(2,0);
            
            break;
        }
        
        case SC_READ: {// int Read(char *buffer, int size, OpenFileId id)
            // Obtenemos los argumentos de la función
            int bufferAddr = machine->ReadRegister(4);
            int sizeBuffer = machine->ReadRegister(5);
            int fileID     = machine->ReadRegister(6);

            // Si la dirección de memoria del buffer es nullptr, error
            if (bufferAddr == 0) {
                DEBUG('e', "Error: address to buffer is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Read` requested for file `%u`.\n", fileID); 

            // Si se desea leer de la salida estánda, devolvemos error
            if (fileID == CONSOLE_OUTPUT) {
                DEBUG('e', "Error: sc_read from stdout.\n");
                machine->WriteRegister(2, -1);
                break;
            }
            else {
                char *buf = new char[sizeBuffer];
                int bytesReaded = 0;

                // Si se desea leer de la entrada estándar, leemos de la consola
                if (fileID == CONSOLE_INPUT) {
                    // Leemos de a un caracter hasta encontrar un '\n' o EOF o bien hasta
                    // leer 'sizeBuffer' caracteres
                    DEBUG('e', "hole\n");
                    char c;

                    do {
                        c = synchConsole->GetChar();
                        DEBUG('e', "charReaded %c\n", c);
                        buf[bytesReaded] = c;
                        bytesReaded++; 

                    } while (c != '\n' && c != EOF && bytesReaded < sizeBuffer);
                    
                    DEBUG('e', "ultimo caracter %c\n", c);
                }
                // En el caso contrario, leemos desde el archivo indicado
                else {
                    // Buscamos el fileID dentro de la tabla de fd del thread actual
                    OpenFile *openFile = currentThread->GetOpenFile(fileID);
                                    
                    // Si es nullptr devolvemos error al usuario
                    if (openFile == nullptr) {
                        DEBUG('e', "Error: fileID `%d` is not related to an opened file.\n", fileID);
                        machine->WriteRegister(2, -1);
                    }

                    // En caso contrario, leemos desde el archivo
                    bytesReaded = openFile->Read(buf, sizeBuffer);
                }

                // Copiar el buffer del kernel al espacio de usuario
                WriteBufferToUser(buf, bufferAddr, bytesReaded);

                // Retornamos al usuario la cantidad de bytes leídos
                machine->WriteRegister(2, bytesReaded); 

                delete[] buf;
                DEBUG('e', "termine de leer \n");
            }

            break;
        }
        
        case SC_WRITE: { // int Write(const char *buffer, int size, OpenFileId id)
            // Obtenemos los argumentos de la función
            int bufferAddr = machine->ReadRegister(4);
            int sizeBuffer = machine->ReadRegister(5);
            int fileID     = machine->ReadRegister(6);

            // Si la dirección de memoria del buffer es nullptr, error
            if (bufferAddr == 0) {
                DEBUG('e', "Error: address to buffer is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Write` requested for file `%u`.\n", fileID); 

            // Si se desea leer de la salida estánda, devolvemos error
            if (fileID == CONSOLE_INPUT) {
                DEBUG('e', "Error: sc_write from stdin.\n");
                machine->WriteRegister(2, -1);
                break;
            }
            else {
                char *buf = new char[sizeBuffer];
                ReadBufferFromUser(bufferAddr, buf, sizeBuffer);

                int bytesWritten = 0;

                // Si se desea escribir en la salida estándar, escribimos en la consola
                if (fileID == CONSOLE_OUTPUT) {
                    // Escribimos de a un caracter en la consola
                    for(int i=0 ; i < sizeBuffer ; i++) synchConsole->PutChar(buf[i]);

                    bytesWritten = sizeBuffer;
                }
                // En el caso contrario, escribimos el archivo indicado
                else {
                    // Buscamos el fileID dentro de la tabla de fd del thread actual
                    OpenFile *openFile = currentThread->GetOpenFile(fileID);
                                    
                    // Si es nullptr devolvemos error al usuario
                    if (openFile == nullptr) {
                        DEBUG('e', "Error: fileID `%d` is not related to an opened file.\n", fileID);
                        machine->WriteRegister(2, -1);
                    }

                    // En caso contrario, leemos desde el archivo
                    bytesWritten = openFile->Write(buf, sizeBuffer);
                }

                // Retornamos al usuario la cantidad de bytes leídos
                machine->WriteRegister(2, bytesWritten);         

                delete[] buf;
                break;
            }
        }

        default:
            fprintf(stderr, "Unexpected system call: id %d.\n", scid);
            ASSERT(false);

    }

    IncrementPC();
}


/// By default, only system calls have their own handler.  All other
/// exception types are assigned the default handler.
void
SetExceptionHandlers()
{
    machine->SetHandler(NO_EXCEPTION,            &DefaultHandler);
    machine->SetHandler(SYSCALL_EXCEPTION,       &SyscallHandler);
    machine->SetHandler(PAGE_FAULT_EXCEPTION,    &DefaultHandler);
    machine->SetHandler(READ_ONLY_EXCEPTION,     &DefaultHandler);
    machine->SetHandler(BUS_ERROR_EXCEPTION,     &DefaultHandler);
    machine->SetHandler(ADDRESS_ERROR_EXCEPTION, &DefaultHandler);
    machine->SetHandler(OVERFLOW_EXCEPTION,      &DefaultHandler);
    machine->SetHandler(ILLEGAL_INSTR_EXCEPTION, &DefaultHandler);
}