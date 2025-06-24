#ifndef ASSOOFS_H
#define ASSOOFS_H

#include <linux/stat.h>


#define ASSOOFS_MAGIC 0x20200406   //NUMERO MAGICO asi el kernel sabra que es ASSOOFS
#define ASSOOFS_DEFAULT_BLOCK_SIZE 4096 //Tamaño del bloque en bytes
#define ASSOOFS_FILENAME_MAXLEN 255  //tamaño maximo de nombres de archivo (255 char)

#define ASSOOFS_SUPERBLOCK_BLOCK_NUMBER 0 //el numero del bloque del superbloque de su inodo y lo mismo para el directorio raiz
#define ASSOOFS_INODESTORE_BLOCK_NUMBER 1
#define ASSOOFS_ROOTDIR_BLOCK_NUMBER 2
#define ASSOOFS_ROOTDIR_INODE_NUMBER 0

#define ASSOOFS_MAX_FILESYSTEM_OBJECTS_SUPPORTED 64  El sistema solo soportara hasta 64 archivos/objetos/ficheros/cartoonNetwork

#define ASSOOFS_LAST_RESERVED_BLOCK ASSOOFS_ROOTDIR_BLOCK_NUMBER     //asi sabremos donde empezar despues del reformateo justo despues de todo el espacio reservado
#define ASSOOFS_LAST_RESERVED_INODE ASSOOFS_ROOTDIR_INODE_NUMBER

const int ASSOOFS_TRUE = 1;   //no hay booleans asi que toca usar esto
const int ASSOOFS_FALSE = 0;

struct assoofs_super_block_info {  //estructura del SUPERBLOQUE ya lo tenemos puesto en el mk pero aqui lo definimos
	uint64_t version;
	uint64_t magic;
	uint64_t block_size;
	uint64_t inodes_count;
	uint64_t free_blocks;
	uint64_t free_inodes;
	char padding[4048];
};

struct assoofs_dir_record_entry { //lo mismo con el directorio de entrada
	char filename[ASSOOFS_FILENAME_MAXLEN]; //nombre del archivo
	uint64_t inode_no;  
	uint64_t entry_removed;  //si ha sido borrado se hace para hacer soft deletes y asi poder recuperar archivos eliminados
};

struct assoofs_inode_info {   //info que tendra el inodo
	mode_t mode; //tipo y permisos, directorio o archivo 
	uint64_t inode_no;  //identificador unico 
	uint64_t data_block_number; //bloque de datos asociado (en algun lugar tiene que estar)
	union {                                                                  //IMPORTANTE se usa union porque solo vamos a usar una, si es archivo pues tenemos nuesto espacio y si es directorio pues sus entradas (ahorrar espacio)
    	uint64_t file_size; //tamaño de bytes
    	uint64_t dir_children_count;  //numero de entradas del directorio 
	};
};

#endif



