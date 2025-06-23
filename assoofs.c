#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include "assoofs.h"
#include <linux/string.h>  


MODULE_LICENSE("GPL");
MODULE_AUTHOR("Tu Nombre");
MODULE_DESCRIPTION("Sistema de ficheros ASSOOFS");
// Prototipos de nuevas funciones
static uint64_t assoofs_sb_get_freeinode(struct super_block *sb);
static int assoofs_remove(struct inode *dir, struct dentry *dentry);
static void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode);
static void assoofs_save_sb_info(struct super_block *sb);
static uint64_t assoofs_sb_get_freeblock(struct super_block *sb);
static struct assoofs_inode_info *assoofs_get_inode_info(struct super_block *sb, uint64_t inode_no);
static ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos);
static ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos);
static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);
static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode);
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);


// Operaciones sobre directorios
const struct file_operations assoofs_dir_operations = {
    .owner = THIS_MODULE,
    .iterate_shared = assoofs_iterate,
};
const struct file_operations assoofs_file_operations = {
    .owner = THIS_MODULE,
    .read = assoofs_read,
    .write = assoofs_write,
};

// Operaciones sobre inodos
static struct inode_operations assoofs_inode_ops = {
    .lookup = assoofs_lookup,
    .create = assoofs_create,
    .mkdir = assoofs_mkdir,
    .unlink = assoofs_remove,
};

// Prototipos
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
int assoofs_fill_super(struct super_block *sb, void *data, int silent);

// Definición del sistema de ficheros
static struct file_system_type assoofs_type = {
    .owner = THIS_MODULE,
    .name = "assoofs",
    .mount = assoofs_mount,
    .kill_sb = kill_block_super,
};

// Operaciones sobre superbloque
static const struct super_operations assoofs_sops = {
    .drop_inode = generic_delete_inode,
};

// Función para inicializar el superbloque
int assoofs_fill_super(struct super_block *sb, void *data, int silent) { //puntero a la estructura sb que pasa el kernel vacia y lo demas son opciones
    printk(KERN_INFO "assoofs_fill_super called\n"); //un print que solo aparece en los logs (depuracion)

    struct buffer_head *bh;   
    struct assoofs_super_block_info *assoofs_sb;
//bh sera el buffer head con los datos leidos
    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);  //lee el bloque 0 donde esta el sb y nos pasa el buffer_head con los datos leidos
    assoofs_sb = (struct assoofs_super_block_info *)bh->b_data; //interpretamos los bytres leidos como una struct de sb

    if (assoofs_sb->magic != ASSOOFS_MAGIC) {  //comprobamos que lo que sacamos del bloque 0 de tipo sb sea assoofs
        printk(KERN_ERR "Invalid magic number: %llu\n", assoofs_sb->magic);
        brelse(bh); //libera el buffer head
        return -EINVAL;
    }

    sb->s_magic = assoofs_sb->magic;   
    sb->s_fs_info = assoofs_sb; //Guarda un puntero a nuestra copia en memoria del superbloque ASSOOFS 
    sb->s_op = &assoofs_sops; //le asigna las operaciones de sb que hay arriba
    struct inode *root_inode = new_inode(sb); //crea un nuevo inodo que sera el del directorio raiz
    inode_init_owner(&nop_mnt_idmap, root_inode, NULL, S_IFDIR);  //Inicializa como directorio (S_IFDIR) y establece propietario (root / idmap nulo)
    root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER; 
    root_inode->i_sb = sb; // su superbloque 
    root_inode->i_op = &assoofs_inode_ops;  //le pasamos las operaciones que podra hacer, estan en un struct arriba
    root_inode->i_fop = &assoofs_dir_operations; //esto porque sera un directorio asique sus operaciones son estas

    root_inode->i_private = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL); //reserva memoria para guardar informacion extendida
    struct buffer_head *inode_bh;
    struct assoofs_inode_info *root_inode_info;

    inode_bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER); //leemos el bloque donde estan los inodos el 1
    root_inode_info = (struct assoofs_inode_info *)inode_bh->b_data;  //interpreta los datos como inodo
    memcpy(root_inode->i_private, root_inode_info, sizeof(struct assoofs_inode_info)); //copia la informacion al nodo i_private del inodo raiz
    brelse(inode_bh); //libera el buffer de lectura anterior

    sb->s_root = d_make_root(root_inode); //marca este inodo como la raiz del sistema IMPORTANTE

    brelse(bh);  //libera el buffer

    printk(KERN_INFO "Superblock initialized successfully\n");
    return 0;
}

// Función para montar el sistema de ficheros
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data) {
    printk(KERN_INFO "assoofs_mount called\n");
    return mount_bdev(fs_type, flags, dev_name, data, assoofs_fill_super);
}

// Función de carga del módulo
static int __init assoofs_init(void) {
    printk(KERN_INFO "assoofs_init called\n");
    return register_filesystem(&assoofs_type);
}

// Función de descarga del módulo
static void __exit assoofs_exit(void) {
    printk(KERN_INFO "assoofs_exit called\n");
    unregister_filesystem(&assoofs_type);
}
//cuando se hace ls se llama a esta funcion 
static int assoofs_iterate(struct file *filp, struct dir_context *ctx) { //filp es el directorio sobre el que se hace ls y ctx el contexto donde ir emitiendo el listado
    struct inode *inode = file_inode(filp); //pillamos el inodo asociado a ese directorio
    struct super_block *sb = inode->i_sb;  //porsi acaso tambien cogemos su superbloque
    struct assoofs_inode_info *inode_info = inode->i_private; //obtenemos la estrucutra privada (cuantos hijos tiene y donde estan sus entradas en el directorio disco)
    struct buffer_head *bh; //variables para leer entradas
    struct assoofs_dir_record_entry *record;
    int i; //i :)

    printk(KERN_INFO "assoofs_iterate called\n");  //log util 

    if (ctx->pos)   //si es 0 es que ya se listo todo
        return 0;

    bh = sb_bread(sb, inode_info->data_block_number);  //leemos el bloque de datos con las entradas de directorio 
    record = (struct assoofs_dir_record_entry *)bh->b_data; //su estructura es esa 

    for (i = 0; i < inode_info->dir_children_count; i++) {  //se recorre cada hijo del directorio 
        if (record->entry_removed == ASSOOFS_FALSE) { //si no ha sido borrado 
            dir_emit(ctx, record->filename, strlen(record->filename),  //le damos al kernel su nombre y su inodo
                     record->inode_no, DT_UNKNOWN);
            ctx->pos += sizeof(struct assoofs_dir_record_entry); //avanzamos el contexto para futuras llamadas y que no se escriba encima
        }
        record++; //al siguiente
    }

    brelse(bh); //liberamos el buffer
    return 0;
}
//se le pasa el inodo del dir donde creo el fichero, la entrada (nombre del archivo a crear), permisos y flags (ignorar)
static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
    struct super_block *sb = dir->i_sb; //obtenemos superbloque, el sb extendido con el conteo de inodos y bloques libres 
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    struct assoofs_inode_info *parent_info = dir->i_private; // Y aqui los metadatos del dirctorio padre
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;

    printk(KERN_INFO "assoofs_create called\n");

    if (assoofs_sb->free_inodes == 0) { //si no hay inodos libres error 
        printk(KERN_ERR "No free inodes\n");
        return -ENOSPC;
    }

    inode = new_inode(sb);  //creamos un inodo nuebo
    inode->i_ino = assoofs_sb_get_freeinode(sb);   // uso la funcion de mas abajo para darle un numero libre
    inode->i_sb = sb;
    inode_init_owner(&nop_mnt_idmap, inode, dir, S_IFREG | mode); //se inicializa como fichero regular

    inode->i_op = &assoofs_inode_ops;  //se le asigna operaciones de inodo y de archivo 
    inode->i_fop = &assoofs_file_operations;

    inode_info = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);   //se crea la estructura privadas de metadatos
    inode_info->inode_no = inode->i_ino;  //numero de inodo
    inode_info->mode = S_IFREG | mode; //permisos
    inode_info->file_size = 0; //tamaño
    inode_info->data_block_number = assoofs_sb_get_freeblock(sb);  //para el numero de bloque libre uso la funcion de abajo

    inode->i_private = inode_info;  //guardamos todo en el inodo

    // se le añade al directorio padre el que se nos paso 
    bh = sb_bread(sb, parent_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    record += parent_info->dir_children_count;  //Nos desplazamos hasta la posición vacía (el siguiente slot libre en el array de entradas de directorio)

    strcpy(record->filename, dentry->d_name.name); //rellenamos la nueva entrada
    record->inode_no = inode_info->inode_no;
    record->entry_removed = ASSOOFS_FALSE;

    mark_buffer_dirty(bh);  //guardar cambios y liberar buffer
    brelse(bh);

    //se actualiza disco y estructuras
    assoofs_add_inode_info(sb, inode_info); //guardamos nuevo inodo Funcion de abajo

    parent_info->dir_children_count++;  //actualizar conteos
    assoofs_sb->inodes_count++;
    assoofs_sb->free_inodes--;

    assoofs_save_sb_info(sb);  // guardar superbloque actualizado en disco 
    struct buffer_head *new_bh = sb_getblk(sb, inode_info->data_block_number);
    memset(new_bh->b_data, 0, ASSOOFS_DEFAULT_BLOCK_SIZE);
    mark_buffer_dirty(new_bh);
    sync_dirty_buffer(new_bh);
    brelse(new_bh);

    d_instantiate(dentry, inode); // Asocia el dentry (nombre + path) con el inodo que acabamos de crear, necesario para acceso posterior (lookup, etc.)

    printk(KERN_INFO "File %s created successfully\n", dentry->d_name.name);
    return 0;
}
  
static uint64_t assoofs_sb_get_freeinode(struct super_block *sb) {  //devuelve el siguiente numero de inodo disponible 
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info; //accede al superbloque extendido (con los scontadores)
    uint64_t free_inode = ASSOOFS_LAST_RESERVED_INODE + 1 + assoofs_sb->inodes_count;  //el primero libre despues del ultimo reservado
    return free_inode;
}
static uint64_t assoofs_sb_get_freeblock(struct super_block *sb) {   //lo mismo pero para bloques
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    uint64_t free_block = ASSOOFS_LAST_RESERVED_BLOCK + 1 + assoofs_sb->inodes_count;
    return free_block;
}
static void assoofs_save_sb_info(struct super_block *sb) { //guarda los datos actualizados del superbloque en disco
    struct buffer_head *bh;  //obtenemos el superbloque extendido
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info; //lo identificamos

    bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER); //leemos el bloque 0
    memcpy(bh->b_data, assoofs_sb, sizeof(struct assoofs_super_block_info)); //copiamos la version actualizada en memoria
    mark_buffer_dirty(bh); //marcamos como modificado y se libera al bh
    sync_dirty_buffer(bh);
    brelse(bh);
}
static void assoofs_add_inode_info(struct super_block *sb, struct assoofs_inode_info *inode) { //añade un nuevo indo al almacen de inodos del disco
    struct buffer_head *bh;
    struct assoofs_inode_info *inode_info;
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;

    bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);
    inode_info = (struct assoofs_inode_info *)bh->b_data;
    inode_info += assoofs_sb->inodes_count;  // siguiente inodo libre

    memcpy(inode_info, inode, sizeof(struct assoofs_inode_info)); //copiar los nuevos datos del inodo al disco
    mark_buffer_dirty(bh);
    sync_dirty_buffer(bh);
    brelse(bh);
}
//crear direetorio es muy parecido al create
static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
    struct super_block *sb = dir->i_sb;
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
    struct assoofs_inode_info *parent_info = dir->i_private;
    struct inode *inode;
    struct assoofs_inode_info *inode_info;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;

    printk(KERN_INFO "assoofs_mkdir called\n");

    if (assoofs_sb->free_inodes == 0) {  //comprobar que hay inodos libres
        printk(KERN_ERR "No free inodes\n");
        return -ENOSPC;
    }

    inode = new_inode(sb);      //crear nuevo inodo 
    inode->i_ino = assoofs_sb_get_freeinode(sb);
    inode->i_sb = sb;
    inode_init_owner(&nop_mnt_idmap, inode, dir, S_IFDIR | mode);

    inode->i_op = &assoofs_inode_ops;
    inode->i_fop = &assoofs_dir_operations; //operaciones de directorio 

    inode_info = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
    inode_info->inode_no = inode->i_ino;
    inode_info->mode = S_IFDIR | mode;  //directorio 
    inode_info->file_size = 0;
    inode_info->data_block_number = assoofs_sb_get_freeblock(sb);

    inode->i_private = inode_info;

    // Cualquier duda revisar create que es mas o menos lo mismo 
    bh = sb_bread(sb, parent_info->data_block_number);
    record = (struct assoofs_dir_record_entry *)bh->b_data;
    record += parent_info->dir_children_count;

    strcpy(record->filename, dentry->d_name.name);
    record->inode_no = inode_info->inode_no;
    record->entry_removed = ASSOOFS_FALSE;

    mark_buffer_dirty(bh);
    brelse(bh);
    
    assoofs_add_inode_info(sb, inode_info);

    parent_info->dir_children_count++;
    assoofs_sb->inodes_count++;
    assoofs_sb->free_inodes--;

    assoofs_save_sb_info(sb);  
    struct buffer_head *new_bh = sb_getblk(sb, inode_info->data_block_number);
    memset(new_bh->b_data, 0, ASSOOFS_DEFAULT_BLOCK_SIZE);
    mark_buffer_dirty(new_bh);
    sync_dirty_buffer(new_bh);
    brelse(new_bh);

    d_instantiate(dentry, inode);

    printk(KERN_INFO "Directory %s created successfully\n", dentry->d_name.name);
    return 0;
}
//busca a ver si en el directorio padre esta el dentry(es el nombre del archivo a buscar) 
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
    struct super_block *sb = parent_inode->i_sb; //obtenemos el superbloque y metadatos del directorio padre
    struct assoofs_inode_info *parent_info = parent_inode->i_private; 
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;

    printk(KERN_INFO "assoofs_lookup called for name: %s\n", child_dentry->d_name.name);

    bh = sb_bread(sb, parent_info->data_block_number); //leer el bloque de directorio donde estan las entradas filename + inode num
    record = (struct assoofs_dir_record_entry *)bh->b_data;

    for (i = 0; i < parent_info->dir_children_count; i++) { //recorremos las entradas del directorio
        if (record->entry_removed == ASSOOFS_FALSE &&   //si el nombre coincide y no esta borrado es ese
            strcmp(record->filename, child_dentry->d_name.name) == 0) {
                                //cargar su inodo
            struct inode *inode;  
            struct buffer_head *inode_bh;
            struct assoofs_inode_info *inode_info;

            // Leer el inodo correspondiente
            inode = new_inode(sb);  //creamos un nuevo inodo vacio con el numero correcto
            inode->i_ino = record->inode_no;
            inode->i_sb = sb;

            inode_bh = sb_bread(sb, ASSOOFS_INODESTORE_BLOCK_NUMBER);  //leemos los inodos almacenados en el bloque
            inode_info = (struct assoofs_inode_info *)inode_bh->b_data;

            // Saltar hasta el inodo correcto
            inode_info += record->inode_no;  //satamos hasta ek que buscamos

            inode_init_owner(&nop_mnt_idmap, inode, parent_inode, inode_info->mode);  //configurar el propietario 
            inode->i_op = &assoofs_inode_ops;  //operaciones
            if (S_ISDIR(inode_info->mode))  //si es archivo o directorio 
                inode->i_fop = &assoofs_dir_operations;
            else
                inode->i_fop = &assoofs_file_operations;

            inode->i_private = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);  //guardamos sus metadatos
            memcpy(inode->i_private, inode_info, sizeof(struct assoofs_inode_info));

            brelse(inode_bh); //liberar bloques y demas
            brelse(bh);

            d_add(child_dentry, inode);  //asociamos el inodo al dentry
            return NULL;
        }
        record++; //al siguiente
    }

    brelse(bh);
    return NULL;
}
//lee los datos de un archivo (cat)
static ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos) { //flip es el puntero al archivo, buff buffer al que copiar los datos y etc
    struct inode *inode = file_inode(filp);    //obtencion de inodo y metadatos del archivo
    struct super_block *sb = inode->i_sb;
    struct assoofs_inode_info *inode_info = inode->i_private;
    struct buffer_head *bh;
    char *data;
    ssize_t ret;

    printk(KERN_INFO "assoofs_read called\n");

    if (*ppos >= inode_info->file_size) //si estamos al final no hay nada que leer
        return 0;

    bh = sb_bread(sb, inode_info->data_block_number);  //leer bloque de datos del archivo
    data = (char *)bh->b_data;

    if (len > inode_info->file_size - *ppos)   //ajustamos el len para no leer de mas
      len = inode_info->file_size - *ppos;

    if (copy_to_user(buf, data + *ppos, len)) { //copiar del kernel al buffer indicado 
        //por si falla pues dar error 
        brelse(bh);
        return -EFAULT;
    }

    brelse(bh);  //liberar y actualizar posiciones
    *ppos += len;
    ret = len;

    return ret;
}
//es la misma estructura de antes solo que ahora para escribir escribe
static ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos) {
    struct inode *inode = file_inode(filp); //lo de antes
    struct super_block *sb = inode->i_sb;
    struct assoofs_inode_info *inode_info = inode->i_private;
    struct buffer_head *bh;
    char *data;
    ssize_t ret;

    printk(KERN_INFO "assoofs_write called\n");

    bh = sb_bread(sb, inode_info->data_block_number);  //leemos el bloque de datos (donde vamos a escribir)
    data = (char *)bh->b_data;

    if (copy_from_user(data + *ppos, buf, len)) {  //copiamos del usuario al kernel lo que escribio 
        brelse(bh); //por si falla
        return -EFAULT;
    }

    mark_buffer_dirty(bh);  //marcamos el bloque como sucio y liberamos
    brelse(bh);

    *ppos += len; //actualizamos posicion y tamaño del archivo
    inode_info->file_size = *ppos;
    inode->i_size = inode_info->file_size;

    mark_inode_dirty(inode);  //marcamos el inodo como sucio hay que guardar su nueva info 

    ret = len;
    return ret;
}
//se nos pasa el inodo del directorio padre y el nombre del archivo a borrar
static int assoofs_remove(struct inode *dir, struct dentry *dentry) {
    struct inode *inode = d_inode(dentry);  //Obtenemos el inodo asociado al archivo/directorio que queremos eliminar (a partir del dentry) 
    struct super_block *sb = dir->i_sb; //ahora mediante el dir buscamos cual es el superbloque del sistema
    struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info; // en sb estamos apuntando al superbloque aqui apuntamos a 
    struct assoofs_inode_info *parent_info = dir->i_private;
    struct buffer_head *bh;
    struct assoofs_dir_record_entry *record;
    int i;

    printk(KERN_INFO "assoofs_remove called for %s\n", dentry->d_name.name);

    // 1. Marcar la entrada como eliminada en el directorio
    bh = sb_bread(sb, parent_info->data_block_number);  //leemos bloque donde estan las entradas del diretorio
    record = (struct assoofs_dir_record_entry *)bh->b_data;

    for (i = 0; i < parent_info->dir_children_count; i++) {  //buscamos la entrada por nombre y marcamos como eliminada
        if (strcmp(record->filename, dentry->d_name.name) == 0) {
            record->entry_removed = ASSOOFS_TRUE; //aqui lo marcamos 
            mark_buffer_dirty(bh);
            break;
        }
        record++;
    }
    brelse(bh);

    // 2. Decrementar contadores
    parent_info->dir_children_count--;
    assoofs_sb->inodes_count--;
    assoofs_sb->free_inodes++;
    assoofs_sb->free_blocks++;

    assoofs_save_sb_info(sb);  // Guardamos cambios del superbloque

    // 3. Borrar el inodo (liberarlo)
    clear_nlink(inode);   //Eliminamos todos los enlaces duros del inodo (clear_nlink), ponemos ->
    inode->i_size = 0;     //-> tamaño a 0 y marcamos el inodo como modificado para que se actualice.
    mark_inode_dirty(inode);

    printk(KERN_INFO "File %s removed successfully\n", dentry->d_name.name);
    return 0;
}


module_init(assoofs_init);
module_exit(assoofs_exit);

