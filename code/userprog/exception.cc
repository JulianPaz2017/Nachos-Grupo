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
#include "args.hh"
#include "filesys/directory_entry.hh"
#include "threads/system.hh"
#include "address_space.hh"  ///< Para GetPageTable() y GetNumPages() en PageFaultHandler.

#include "filesys/file_system.hh"

#include <stdio.h>
#include <string.h>


/// Función que ejecuta un nuevo programa de usuario (sin argumentos).
/// Se usa como función de Fork para SC_EXEC.
static void
StartProc(void *arg)
{
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();

    machine->Run();
}

/// Función que ejecuta un nuevo programa de usuario (con argumentos).
/// Se usa como función de Fork para SC_EXEC2.
static void
StartProcWithArgs(void *args)
{
    currentThread->space->InitRegisters();
    currentThread->space->RestoreState();

    // Escribir los argumentos en la pila del nuevo proceso
    unsigned argc = WriteArgs((char **)args);

    // Configurar a0 = argc, a1 = argv (apunta al inicio del arreglo argv)
    // Después de WriteArgs, sp apunta al primer puntero de argv
    int sp = machine->ReadRegister(STACK_REG);
    machine->WriteRegister(4, (int)argc);   // a0 = argc
    machine->WriteRegister(5, sp);          // a1 = argv

    // Reservar espacio para la función call argument area (16 bytes, MIPS ABI)
    machine->WriteRegister(STACK_REG, sp - 16);

    machine->Run();
    ASSERT(false);
}


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

#ifdef USE_TLB
/// Manejador de PageFaultException (TLB miss handler).
///
/// Se invoca cada vez que la MMU no encuentra una traducción válida en la
/// TLB para la dirección virtual accedida. El kernel debe cargar la entrada
/// correcta desde la pageTable del proceso hacia la TLB.
///
/// Política de reemplazo: índice circular (round-robin).
///   - Simple y predecible.
///   - Evita el costo de buscar la entrada menos usada.
///   - Suficiente para esta implementación educativa.
///
/// IMPORTANTE: NO se llama a IncrementPC(). Al retornar, NachOS reintenta
/// automáticamente la instrucción que disparó el fallo, ahora con la
/// traducción disponible en la TLB.
static void
PageFaultHandler(ExceptionType et)
{
    (void) et;  // No necesitamos el tipo de excepción en este handler.

    // Índice de la próxima entrada a reemplazar en la TLB.
    // 'static' asegura que el valor persiste entre invocaciones del handler.
    static unsigned tlbIndex = 0;

    // Contabilizar este fallo como un TLB miss en las estadísticas globales.
    // Cada invocación de este handler corresponde exactamente a un miss.
    // Los hits se cuentan en mmu.cc::RetrievePageEntry().
    stats->tlbMisses++;

    // Leer la dirección virtual que causó el TLB miss desde BAD_VADDR_REG.
    // La MMU escribe allí la dirección fallida antes de lanzar la excepción.
    unsigned badVAddr = (unsigned) machine->ReadRegister(BAD_VADDR_REG);

    // Calcular el número de página virtual (VPN = dirección / tamaño de página).
    unsigned vpn = badVAddr / PAGE_SIZE;

    DEBUG('a', "PageFaultHandler: TLB miss en VA 0x%X, VPN=%u -> TLB[%u].\n",
          badVAddr, vpn, tlbIndex);

    // Obtener la tabla de páginas del proceso en ejecución.
    const TranslationEntry *pageTable = currentThread->space->GetPageTable();
    unsigned numPages = currentThread->space->GetNumPages();

    // Sanity check: el VPN debe estar dentro del espacio de direcciones del proceso.
    // Si no, es un acceso ilegal (ej: puntero colgante, buffer overflow).
    if (vpn >= numPages) {
        fprintf(stderr, "PageFaultHandler: VPN %u fuera de rango (numPages=%u).\n",
                vpn, numPages);
        ASSERT(false);  // Acceso fuera del espacio de direcciones: error fatal.
    }

    // Reemplazamos la entrada indicada por el índice circular.
    machine->GetMMU()->tlb[tlbIndex] = pageTable[vpn];

    // Avanzar el índice (mod TLB_SIZE) para el próximo reemplazo.
    tlbIndex = (tlbIndex + 1) % TLB_SIZE;

    // No llamamos IncrementPC(): la instrucción se reintenta automáticamente.
}
#endif  // USE_TLB

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
                    char c;

                    do {
                        c = synchConsole->GetChar();
                        buf[bytesReaded] = c;
                        bytesReaded++; 

                    } while (c != '\n' && c != EOF && bytesReaded < sizeBuffer);
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

                // Copiar el buffer del kernel al espacio de usuario solo si se leyeron bytes
                if (bytesReaded > 0) {
                    WriteBufferToUser(buf, bufferAddr, bytesReaded);
                }

                // Retornamos al usuario la cantidad de bytes leídos
                machine->WriteRegister(2, bytesReaded); 

                delete[] buf;
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
                if (sizeBuffer <= 0) {
                    machine->WriteRegister(2, 0);
                    break;
                }

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

        case SC_EXIT: {
            int status = machine->ReadRegister(4);
            DEBUG('e', "Thread `%s` exiting with status %d.\n",
                  currentThread->GetName(), status);
                  
            if (processTable->IsEmpty()) {
                DEBUG('e', "Último proceso terminó. Apagando Nachos.\n");
                interrupt->Halt();
            }
            else {
                currentThread->Finish();
            }
            
            break;
        }

        case SC_EXEC: {
            // Leer la dirección del nombre del ejecutable
            int filenameAddr = machine->ReadRegister(4);
            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filename[FILE_NAME_MAX_LEN + 1];
            if (!ReadStringFromUser(filenameAddr,
                                    filename, sizeof filename)) {
                DEBUG('e', "Error: filename string too long.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Exec` requested for file `%s`.\n", filename);

            // Abrir el ejecutable
            OpenFile *executable = fileSystem->Open(filename);
            if (executable == nullptr) {
                DEBUG('e', "Error: unable to open file `%s`.\n", filename);
                machine->WriteRegister(2, -1);
                break;
            }

            // Crear nuevo hilo (joinable) y espacio de direcciones
            Thread *newThread = new Thread(filename, true);
            AddressSpace *space = new AddressSpace(executable);
            newThread->space = space;

            delete executable;

            // Registrar en la tabla de procesos
            int spaceId = processTable->Add(newThread);
            if (spaceId == -1) {
                DEBUG('e', "Error: process table full.\n");
                delete space;
                delete newThread;
                machine->WriteRegister(2, -1);
                break;
            }

            // Fork del hilo
            newThread->Fork(StartProc, nullptr);

            // Devolver el SpaceId
            machine->WriteRegister(2, spaceId);
            break;
        }

        case SC_JOIN: {
            int spaceId = machine->ReadRegister(4);
            DEBUG('e', "`Join` requested for SpaceId %d.\n", spaceId);

            if (!processTable->HasKey(spaceId)) {
                DEBUG('e', "Error: invalid SpaceId %d.\n", spaceId);
                machine->WriteRegister(2, -1);
                break;
            }

            Thread *childThread = processTable->Get(spaceId);
            childThread->Join();
            processTable->Remove(spaceId);

            // Devolver 0 (en un Nachos más completo se devolvería el exit status)
            machine->WriteRegister(2, 0);
            break;
        }

        case SC_EXEC2: {
            // r4 = nombre del ejecutable, r5 = argv
            int filenameAddr = machine->ReadRegister(4);
            int argvAddr     = machine->ReadRegister(5);

            if (filenameAddr == 0) {
                DEBUG('e', "Error: address to filename string is null.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            char filename[FILE_NAME_MAX_LEN + 1];
            if (!ReadStringFromUser(filenameAddr,
                                    filename, sizeof filename)) {
                DEBUG('e', "Error: filename string too long.\n");
                machine->WriteRegister(2, -1);
                break;
            }

            DEBUG('e', "`Exec2` requested for file `%s`.\n", filename);

            // Guardar los argumentos del proceso padre ANTES de cambiar de espacio
            char **args = nullptr;
            if (argvAddr != 0) {
                args = SaveArgs(argvAddr);
            }

            // Abrir el ejecutable
            OpenFile *executable = fileSystem->Open(filename);
            if (executable == nullptr) {
                DEBUG('e', "Error: unable to open file `%s`.\n", filename);
                machine->WriteRegister(2, -1);
                break;
            }

            // Crear nuevo hilo (joinable) y espacio de direcciones
            Thread *newThread = new Thread(filename, true);
            AddressSpace *space = new AddressSpace(executable);
            newThread->space = space;

            delete executable;

            // Registrar en la tabla de procesos
            int spaceId = processTable->Add(newThread);
            if (spaceId == -1) {
                DEBUG('e', "Error: process table full.\n");
                delete space;
                delete newThread;
                machine->WriteRegister(2, -1);
                break;
            }

            // Fork: si hay argumentos, usar StartProcWithArgs
            if (args != nullptr) {
                newThread->Fork(StartProcWithArgs, (void *)args);
            } else {
                newThread->Fork(StartProc, nullptr);
            }

            // Devolver el SpaceId
            machine->WriteRegister(2, spaceId);
            break;
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
#ifdef USE_TLB
    // Con TLB activa: los fallos de página (TLB misses) los atiende
    // PageFaultHandler, que carga la traducción desde pageTable a la TLB.
    machine->SetHandler(PAGE_FAULT_EXCEPTION,    &PageFaultHandler);
#else
    // Sin TLB: un fallo de página no debería ocurrir (usamos pageTable directa).
    machine->SetHandler(PAGE_FAULT_EXCEPTION,    &DefaultHandler);
#endif
    machine->SetHandler(READ_ONLY_EXCEPTION,     &DefaultHandler);
    machine->SetHandler(BUS_ERROR_EXCEPTION,     &DefaultHandler);
    machine->SetHandler(ADDRESS_ERROR_EXCEPTION, &DefaultHandler);
    machine->SetHandler(OVERFLOW_EXCEPTION,      &DefaultHandler);
    machine->SetHandler(ILLEGAL_INSTR_EXCEPTION, &DefaultHandler);
}

