#include <unistd.h>
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <linux/types.h> 
#include <stdlib.h>
#include <string.h>
#include "assoofs.h"
//estamos reservando para el sistema bloques para el directorio raiz y para un archivo ejemplo que sera el readme
#define WELCOMEFILE_DATABLOCK_NUMBER (ASSOOFS_LAST_RESERVED_BLOCK + 1)        //bloque de datos donde se almacena el contenido de README.txt
#define WELCOMEFILE_INODE_NUMBER (ASSOOFS_LAST_RESERVED_INODE + 1)            //Numero de inodo que lo identificara

static int write_superblock(int fd) {      //escribe un superbloque con la informacion esencial del sistema
	struct assoofs_super_block_info sb = {
    	.version = 1,
    	.magic = ASSOOFS_MAGIC, //permite que el kernel reconozca que es un sistema ASSOOFS es algo asi como una firma
    	.block_size = ASSOOFS_DEFAULT_BLOCK_SIZE,   //tamaño de cada bloque
    	.inodes_count = 2, // root dir + README.txt   esos son los inodos que tendra ya de por si
    	.free_blocks = (15), // 1111    son bloques que estan libres por si los quiere usar
    	.free_inodes = (3), // 11    lo mismo
	};
	ssize_t ret = write(fd, &sb, sizeof(sb));
	if (ret != ASSOOFS_DEFAULT_BLOCK_SIZE) {   //comprueba el tamaño
    	printf("Bytes written [%d] are not equal to the default block size.\n", (int)ret);
    	return -1;
	}
	printf("Super block written successfully.\n");
	return 0;
}

static int write_root_inode(int fd) {   //crea el inodo directorio raiz
	struct assoofs_inode_info root_inode = {
    	.mode = S_IFDIR,  //le indica al kernel que es un directorio no un archivo
    	.inode_no = ASSOOFS_ROOTDIR_INODE_NUMBER,   //Es el numero de indo que siempre es 0 algo asi como la red que siempre es0 su ip
    	.data_block_number = ASSOOFS_ROOTDIR_BLOCK_NUMBER,  //bloque asociado apuanta al bloque con las entras del directorio 
    	.dir_children_count = 1,    //numero de entradas, para este caso solo es 1, el Readme.txt
	};
	ssize_t ret = write(fd, &root_inode, sizeof(root_inode));     //comprobamos tamaño para ver que se ha escrito bien
	if (ret != sizeof(root_inode)) {
    	printf("The inode store was not written properly.\n");
    	return -1;
	}
	printf("Root directory inode written successfully.\n");
	return 0;
}

static int write_welcome_inode(int fd, const struct assoofs_inode_info *i) {  //este es el inodo del README se le pasa el descriptor y el readme
	ssize_t ret = write(fd, i, sizeof(*i));    //Su tamaño lo calculamos con lo que devuelva write
	if (ret != sizeof(*i)) {
    	printf("The welcomefile inode was not written properly.\n");
    	return -1;
	}
	printf("Welcomefile inode written successfully.\n");

	off_t nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - (sizeof(*i) * 2);
	ret = lseek(fd, nbytes, SEEK_CUR);   //salto para alinear con los siguiente bloque tras los dos inodos
	if (ret == (off_t)-1) {
    	printf("The padding bytes are not written properly.\n");
    	return -1;
	}
	printf("Inode store padding bytes (after two inodes) written successfully.\n");
	return 0;
}

static int write_dirent(int fd, const struct assoofs_dir_record_entry *record) { //entrada del directorio de archivo de README dento del directorio raiz
	ssize_t nbytes = sizeof(*record), ret;
	ret = write(fd, record, nbytes);
	if (ret != nbytes) {
    	printf("Writing the rootdirectory datablock (name+inode_no pair) has failed.\n");
    	return -1;
	}
	printf("Root directory datablocks written successfully.\n");

	nbytes = ASSOOFS_DEFAULT_BLOCK_SIZE - sizeof(*record);
	ret = lseek(fd, nbytes, SEEK_CUR);
	if (ret == (off_t)-1) {
    	printf("Writing the padding for rootdirectory children datablock has failed.\n");
    	return -1;
	}
	printf("Padding after the rootdirectory children written successfully.\n");
	return 0;
}

static int write_block(int fd, char *block, size_t len) {
	ssize_t ret = write(fd, block, len);
	if (ret != len) {
    	printf("Writing file body has failed.\n");
    	return -1;
	}
	printf("Block has been written successfully.\n");
	return 0;
}

int main(int argc, char *argv[]) {
	if (argc != 2) {   //comprovamos que se nos pase el segundo argumento que es la imagen .img
    	printf("Usage: mkassoofs <device>\n");
    	return -1;
	}

	int fd = open(argv[1], O_RDWR);  //abrimos el archivo imagen
	if (fd == -1) { 
    	perror("Error opening the device");
    	return -1;
	}

	char welcomefile_body[] = "Autor: Juan Alberto Pablos Yugueros\nDNI: 71716147P"\nObservaciones: el sistema falla al crear directorios, no he podido arreglarlo ya que cuando lo solucionaba fallaba al crear el README.txt.;   //esto es la declaracion de lo que escribiremos en el Readme

	struct assoofs_inode_info welcome = {   //EStructura del inodo del README
    	.mode = S_IFREG, //Se le indica que es un archivo no un directorio 
    	.inode_no = WELCOMEFILE_INODE_NUMBER,  //su numero de indo ya estaba reservado a si que se lo damos es el 1
    	.data_block_number = WELCOMEFILE_DATABLOCK_NUMBER,  // el bloque de datos donde guardara su contenido
    	.file_size = sizeof(welcomefile_body), //Tamaño en bytes
	};

	struct assoofs_dir_record_entry record = {
    	.filename = "README.txt",  //nombre del archivo
    	.inode_no = WELCOMEFILE_INODE_NUMBER,  //inodo al que apunta
    	.entry_removed = ASSOOFS_FALSE,  //saber si fue borrado 0 no 1 si
	};

	int ret = 1;
	do { //bucle para realizar cada operacion y asi tener el formateo
    	if (write_superblock(fd)) break;
    	if (write_root_inode(fd)) break;
    	if (write_welcome_inode(fd, &welcome)) break;
    	if (write_dirent(fd, &record)) break;
    	if (write_block(fd, welcomefile_body, welcome.file_size)) break;
    	ret = 0;
	} while (0);

	close(fd);
	return ret;
}


