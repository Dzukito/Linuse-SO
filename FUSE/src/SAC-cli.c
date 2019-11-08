#define FUSE_USE_VERSION 30

#include <stddef.h>
#include <fuse.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>
#include <sys/types.h>

#include "FUSE.h"


GBlock* disco;
size_t tamDisco;
GFile* tablaNodos[MAX_FILE_NUMBER];
int indiceTabla=-1;
int32_t socketConexion;

struct t_runtime_options {
	char* disco;
} runtime_options;

#define CUSTOM_FUSE_OPT_KEY(t, p, v) { t, offsetof(struct t_runtime_options, p), v }

void liberarTabla()
{
	for(int i = 0; i <= indiceTabla; i++)
	{
		free(tablaNodos[i]);
	}
}

//recibe un path y devuelve el nombre del archivo o directorio
char *nombreObjeto(const char *path)
{
    char *nombre=NULL;
	char *reverso =	string_reverse(path);
    int cont=0;
    while(reverso[cont]!='/')
    {
        cont++;
    }
    reverso[cont]='\0';
    nombre=string_reverse(reverso);
    return nombre;
}

//devuelve 1 si el directorio o archivo existe, 0 si no existe
int existeObjeto(const char *path)
{
	char *nombre = nombreObjeto(path);

	for (int indActual = 0; indActual <= indiceTabla; indActual++)
		if (strcmp(nombre,tablaNodos[indActual]->nombre) == 0)
			return 1;

	return 0;
}

//retorna el indice del archivo. -1 si no existe
int indiceObjeto(char *nombre)
{
	if(indiceTabla != -1)
	{
		for (int indActual = 0; indActual <= indiceTabla; indActual++)
			if (strcmp(nombre,tablaNodos[indActual]->nombre) == 0)
				return indActual;
	}

	return -1;
}

//devuelve 1 si existe y es un directorio, 0 si no
int esDirectorio(const char *path)
{
	char* nombre = nombreObjeto(path);
	int indiceDir = indiceObjeto(nombre);
	if(indiceDir>=0 && indiceDir<=MAX_FILE_NUMBER)
	{
		if (tablaNodos[indiceDir]->estado == 2)
			return 1;
	}

	return 0;
}

//devuelve 1 si existe y es un archivo, 0 si no
int esArchivo(const char *path)
{
	char* nombre = nombreObjeto(path);
	int indiceArch = indiceObjeto(nombre);
	if(indiceArch>=0 && indiceArch<=MAX_FILE_NUMBER)
	{
		if (tablaNodos[indiceArch]->estado == 1)
			return 1;
	}

	return 0;
}

//agrega un directorio a la lista
void agregarDirectorio(char* nombreDir, char* padre)
{
	indiceTabla++;

		tablaNodos[indiceTabla] = (GFile*) malloc(sizeof(GFile));

		//inicializo valores del nodo
		strcpy(tablaNodos[indiceTabla]->nombre,nombreDir);
		tablaNodos[indiceTabla]->file_size=0;
		tablaNodos[indiceTabla]->estado=2;//porque es directorio

		//linkeo al padre
		if(padre[0] != '\0')
		{
			int indicePadre = indiceObjeto(padre);
			tablaNodos[indiceTabla]->padre = tablaNodos[indicePadre];
		}
		else
			tablaNodos[indiceTabla]->padre = NULL;

}

//agrega un archivo a la lista
void agregarArchivo(char *nombreArch, char* padre)
{
	indiceTabla++;

		tablaNodos[indiceTabla] = (GFile*) malloc(sizeof(GFile));
		tablaNodos[indiceTabla]->contenido[0] = '\0';

		//inicializo valores del nodo
		strcpy(tablaNodos[indiceTabla]->nombre,nombreArch);
		tablaNodos[indiceTabla]->file_size=0;
		tablaNodos[indiceTabla]->estado=1;//porque es archivo

		//linkeo al padre
		if(padre[0] != '\0')
		{
			int indicePadre = indiceObjeto(padre);
			tablaNodos[indiceTabla]->padre = tablaNodos[indicePadre];
		}
		else
			tablaNodos[indiceTabla]->padre = NULL;


}


//busca el indice del archivo y copia el nuevo contenido. Si no existe el archivo, no hace nada
void escribirEnArchivo(const char *path, const char *contenido)
{
	char* nombre = nombreObjeto(path);

	int indArch = indiceObjeto(nombre);

	if (indArch == -1)
		return;

	strcpy(tablaNodos[indArch]->contenido, contenido);
}

//ve si un directorio esta vacio. Devuelve 1 si esta vacio, 0 si tiene algo
int estaVacio(char *nombre)
{

	int indiceArch = indiceObjeto(nombre);

	for (int indActual = 0; indActual <= indiceTabla; indActual++)
	{
		if(tablaNodos[indActual]->padre != NULL)
		{
			if(strcmp(tablaNodos[indiceArch]->padre->nombre, nombre)==0)
			return 0;
		}
	}
	return 1;
}

//"elimina" un objeto de la tabla
void eliminarObjeto(char* nombre)
{
	int objeto = indiceObjeto(nombre);

	tablaNodos[objeto]->contenido[0] = '\0';
	tablaNodos[objeto]->estado = 0;
	tablaNodos[objeto]->padre = NULL;
}



//va a ser llamada cuando el sistema pida los atributos de un archivo

static int hacer_getattr(const char *path, struct stat *st)
{

	char* nombre = malloc(MAX_FILENAME_LENGTH);
	int estadoNodo;

	if(strcmp(path,"/") != 0)
		nombre = nombreObjeto(path);
	else
		strcpy(nombre,"/");

	enviarInt(socketConexion, GETATTR);

	enviarString(socketConexion, nombre);

	st->st_uid = getuid();		//el duenio del archivo
	st->st_gid = getgid();		//el mismo?
	st->st_atime = time(NULL);	//last "A"ccess time
	st->st_mtime = time(NULL);	//last "M"odification time

	recibirInt(socketConexion, &estadoNodo);

	if(estadoNodo == 2)//directorio
	{
		st->st_mode = S_IFDIR | 0755; //bits de permiso
		st->st_nlink = 2;		//num de hardlinks
	}
	else if(estadoNodo == 1)//archivo
	{
		st->st_mode = S_IFREG | 0644;
		st->st_nlink = 1;
		st->st_size = 1024;		//tamanio de los archivos
	}
	else //no existe en el filesystem
	{
		return -ENOENT;
	}

	return 0;
}

//hace una lista de los archivos o directorios disponibles en una ruta específica

static int hacer_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{

	filler(buffer, ".", NULL, 0);
	filler(buffer, "..", NULL, 0);


	//enviar READDIR y path a servidor
	enviarInt(socketConexion, READDIR);
	char* pathAEnviar = malloc(300);
	char* nombreRecibido = malloc(MAX_FILENAME_LENGTH);
	int bytesRecibidos;

	strcpy(pathAEnviar,path);
	enviarString(socketConexion, pathAEnviar);

	/*		recibir buffer 		*/
	bytesRecibidos = recibirString(socketConexion, &nombreRecibido);

	while( strcmp(nombreRecibido,"dou") != 0)
	{
		filler(buffer, nombreRecibido, NULL, 0);
		bytesRecibidos = recibirString(socketConexion, &nombreRecibido);
	}

	//free(pathAEnviar);		a valgrind no le gusta
	free(nombreRecibido);

	return 0;

}

//lee el contenido de un archivo. Retorna el número de bytes que fueron leidos correctamente
static int hacer_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{

	int continuar;		//si llega un 0 continua, -1 sale
	char* cont = malloc(256);
	char* info = malloc(50);
	char* nombre = nombreObjeto(path);

	enviarInt(socketConexion, READ);
	enviarString(socketConexion, nombre);

	recibirInt(socketConexion,&continuar);

	if(continuar == -1)//no existe en el FS
		return -1;

	recibirString(socketConexion,&cont);//recibo el contenido del archivo

	memcpy(buffer, cont + offset, size);

	//msync(disco, tamDisco, MS_SYNC);

	return strlen(cont) - offset;


}

//va a ser llamada para crear un nuevo directorio. "modo" son los bits de permiso, ver documentacion de mkdir
static int hacer_mkdir(const char *path, mode_t modo)
{
	char* pathAEnviar = malloc(300);

	enviarInt(socketConexion, MKDIR);

	strcpy(pathAEnviar,path);
	enviarString(socketConexion, pathAEnviar);

	return 0;

}

//va a ser llamada para crear un nuevo archivo. "dispositivo" tiene que ser especificada si el nuevo archivo
//es un dispositivo, ver documentacion de mknod
static int hacer_mknod(const char *path, mode_t modo, dev_t dispositivo)
{

	char* pathAEnviar = malloc(300);

	enviarInt(socketConexion, MKNOD);

	strcpy(pathAEnviar,path);
	enviarString(socketConexion, pathAEnviar);

	return 0;

}

//va a ser llamada para escribir contenido en un archivo
static int hacer_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *info)
{
	escribirEnArchivo(path, buffer);
	return size;
}


//va a ser llamada para remover un directorio
static int hacer_rmdir (const char *path)
{

	enviarInt(socketConexion, RMDIR);

	char* nombre = nombreObjeto(path);

	int error;

	enviarString(socketConexion, nombre);

	/*			*/

	recibirInt(socketConexion, &error);
	if( error != 0 )
	{
		switch(error)
		{
			case EBUSY:
				return EBUSY;
			break;

			case ENOTDIR:
				return ENOTDIR;
			break;

			case ENOENT:
				return ENOENT;
			break;

			default:
			break;
		}
	}


	return 0;
}


//va a ser llamada para remover un archivo
static int hacer_unlink(const char *path)
{

	enviarInt(socketConexion, UNLINK);

	char* nombre = nombreObjeto(path);

	int error;

	enviarString(socketConexion, nombre);

	/*			*/

	recibirInt(socketConexion, &error);

	if( error != 0 )
	{
		switch(error)
		{
			case EISDIR:
				return EISDIR;
			break;

			case ENOENT:
				return ENOENT;
			break;

			default:
			break;
		}
	}


	return 0;


}

//va a ser llamada para renombar un archivo o directorio. Devuelve 0 si funca
static int hacer_rename(const char *oldpath, const char *newpath, unsigned int flags)
{

	int error = 0;

	enviarInt(socketConexion, RENAME);

	char* pathsAEnviar = malloc( sizeof(oldpath) + sizeof(newpath) + 1 );


	strcpy(pathsAEnviar, oldpath);
	strcat(pathsAEnviar, ",");
	strcat(pathsAEnviar, newpath);

	enviarString(socketConexion, pathsAEnviar);

	recibirInt(socketConexion, &error);

	return error;

}


void levantarClienteFUSE()
{

	/*pthread_t hiloAtendedor = 0;
	char * info = malloc(300);
	char * aux = malloc(50);*/

	socketConexion = levantarCliente(ip,puerto);

}


void levantarConfig()
{
	t_config * unConfig = retornarConfig(pathConfig);

	strcpy(ip,config_get_string_value(unConfig,"IP"));
	strcpy(puerto,config_get_string_value(unConfig,"LISTEN_PORT"));

	config_destroy(unConfig);

	loggearInfoServidor(ip,puerto);

}

static struct fuse_operations operaciones = {


    .getattr	= hacer_getattr,
    .readdir	= hacer_readdir,
	.read		= hacer_read,
	.mkdir		= hacer_mkdir,
	.mknod		= hacer_mknod,
	.write		= hacer_write,
	.rename		= hacer_rename,
	.unlink		= hacer_unlink,
	.rmdir		= hacer_rmdir,
};

enum {
	KEY_VERSION,
	//KEY_HELP,
};
/*
static struct fuse_opt fuse_options[] ={
		CUSTOM_FUSE_OPT_KEY("--disk %s", disco, 0),

		FUSE_OPT_KEY("-V", KEY_VERSION),
		FUSE_OPT_KEY("--version", KEY_VERSION),
		FUSE_OPT_KEY("-h", KEY_HELP),
		FUSE_OPT_KEY("--help", KEY_HELP),
		FUSE_OPT_END,
};*/

size_t tamArchivo(char* archivo)
{
	FILE* fd = fopen(archivo, "r");

	fseek(fd, 0L, SEEK_END);
	uint32_t tam = ftell(fd);

	fclose(fd);
	return tam;
}

int main( int argc, char *argv[] )
{

	remove("Linuse.log");
	iniciarLog("FUSE");
	loggearInfo("Se inicia el proceso FUSE...");
	levantarConfig();
	//levantarClienteFUSE();
	socketConexion = levantarCliente(ip,puerto);

	int ret = fuse_main(argc, argv, &operaciones, NULL);

	liberarTabla();

	return ret;

	/*
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);


	memset(&runtime_options, 0, sizeof(struct t_runtime_options));


	if (fuse_opt_parse(&args, &runtime_options, fuse_options, NULL) == -1){

		perror("Argumentos invalidos!");
		return EXIT_FAILURE;
	}


	if( runtime_options.disco != NULL )
	{
		printf("%s\n", runtime_options.disco);
	}

	tamDisco= tamArchivo(runtime_options.disco);

	int discoFD = open(runtime_options.disco, O_RDWR,0);

	disco = mmap(NULL, tamDisco, PROT_READ|PROT_WRITE, MAP_FILE|MAP_SHARED, discoFD, 0);

	disco = disco + 1 + BITMAP_SIZE_IN_BLOCKS; // mueve el puntero dos posiciones hasta la tabla de nodos. Cambiar
	//despues de probar

	tablaNodos = (GFile*) disco;

	tamDisco = tamDisco - (2 * BLOCK_SIZE);

	//int ret = fuse_main(args.argc, args.argv, &operaciones, NULL);

	//lo dejo como antes para el unmap
	disco = disco - 1 - BITMAP_SIZE_IN_BLOCKS;
	tamDisco = tamDisco + (2 * BLOCK_SIZE);

	munmap(disco,tamDisco);
	close(discoFD);*/


}

void liberarVariablesGlobales()
{
	destruirLog();
}
