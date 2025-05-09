#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/buffer_head.h>
#include <linux/slab.h>
#include "assoofs.h"

MODULE_LICENSE("GPL");  //metadatos
MODULE_AUTHOR("Tu Nombre");
MODULE_DESCRIPTION("Sistema de ficheros ASSOOFS");
// Prototipos de nuevas funciones
static ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos);   //leer o escribir un archivo
static ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos);   
static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl);   //crear un archivo 
static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode);   //crear un directorio
struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags);  //Buscar un archivo o directorio
static int assoofs_iterate(struct file *filp, struct dir_context *ctx);  //lista el contenido de un directorio


// Operaciones sobre directorios
const struct file_operations assoofs_dir_operations = {  
	.owner = THIS_MODULE,
	.iterate_shared = assoofs_iterate,
};
// Operaciones sobre archivos
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
};

// Prototipos
static struct dentry *assoofs_mount(struct file_system_type *fs_type, int flags, const char *dev_name, void *data);
int assoofs_fill_super(struct super_block *sb, void *data, int silent);

// Definición del sistema de ficheros
static struct file_system_type assoofs_type = {   //Registra el sistema de ficheros
	.owner = THIS_MODULE,
	.name = "assoofs",
	.mount = assoofs_mount,  //se llama con un mount -t assoofs...
	.kill_sb = kill_block_super,     //desmonta
};

// Operaciones sobre superbloque
static const struct super_operations assoofs_sops = {
	.drop_inode = generic_delete_inode,                        //operacion de borrado de inodos
};

// Función para inicializar el superbloque
int assoofs_fill_super(struct super_block *sb, void *data, int silent) {       //crea el superbloque y el directorio raiz
	printk(KERN_INFO "assoofs_fill_super called\n");

	struct buffer_head *bh;
	struct assoofs_super_block_info *assoofs_sb;

	bh = sb_bread(sb, ASSOOFS_SUPERBLOCK_BLOCK_NUMBER);
	assoofs_sb = (struct assoofs_super_block_info *)bh->b_data;

	if (assoofs_sb->magic != ASSOOFS_MAGIC) {
    	printk(KERN_ERR "Invalid magic number: %llu\n", assoofs_sb->magic);
    	brelse(bh);
    	return -EINVAL;
	}

	sb->s_magic = assoofs_sb->magic;
	sb->s_fs_info = assoofs_sb;
	sb->s_op = &assoofs_sops;
	struct inode *root_inode = new_inode(sb);
	inode_init_owner(&nop_mnt_idmap, root_inode, NULL, S_IFDIR);
	root_inode->i_ino = ASSOOFS_ROOTDIR_INODE_NUMBER;
	root_inode->i_sb = sb;	
	root_inode->i_op = &assoofs_inode_ops;
	root_inode->i_fop = &assoofs_dir_operations;

	root_inode->i_private = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
	//memcpy(root_inode->i_private, assoofs_sb, sizeof(struct assoofs_super_block_info)); // Opcional: si quieres guardar algo

	sb->s_root = d_make_root(root_inode);

	brelse(bh);

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

static int assoofs_iterate(struct file *filp, struct dir_context *ctx) {
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct assoofs_inode_info *inode_info = inode->i_private;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;
	int i;

	printk(KERN_INFO "assoofs_iterate called\n");

	if (ctx->pos)
    	return 0;

	bh = sb_bread(sb, inode_info->data_block_number);
	record = (struct assoofs_dir_record_entry *)bh->b_data;

	for (i = 0; i < inode_info->dir_children_count; i++) {
    	if (record->entry_removed == ASSOOFS_FALSE) {
        	dir_emit(ctx, record->filename, strlen(record->filename),
                 	record->inode_no, DT_UNKNOWN);
        	ctx->pos += sizeof(struct assoofs_dir_record_entry);
    	}
    	record++;
	}

	brelse(bh);
	return 0;
}

static int assoofs_create(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode, bool excl) {
	struct super_block *sb = dir->i_sb;
	struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
	struct assoofs_inode_info *parent_info = dir->i_private;
	struct inode *inode;
	struct assoofs_inode_info *inode_info;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;

	printk(KERN_INFO "assoofs_create called\n");

	if (assoofs_sb->free_inodes == 0) {
    	printk(KERN_ERR "No free inodes\n");
    	return -ENOSPC;
	}

	inode = new_inode(sb);
	inode->i_ino = parent_info->dir_children_count + 1;
	inode->i_sb = sb;
	inode_init_owner(&nop_mnt_idmap, inode, dir, S_IFREG | mode);

	inode->i_op = &assoofs_inode_ops;
	inode->i_fop = &assoofs_file_operations;


	inode_info = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
	inode_info->inode_no = inode->i_ino;
	inode_info->mode = S_IFREG | mode;
	inode_info->file_size = 0;
	inode_info->data_block_number = ASSOOFS_LAST_RESERVED_BLOCK + inode->i_ino;

	inode->i_private = inode_info;

	// Añadir entrada en el directorio
	bh = sb_bread(sb, parent_info->data_block_number);
	record = (struct assoofs_dir_record_entry *)bh->b_data;
	record += parent_info->dir_children_count;

	strcpy(record->filename, dentry->d_name.name);
	record->inode_no = inode_info->inode_no;
	record->entry_removed = ASSOOFS_FALSE;

	mark_buffer_dirty(bh);
	brelse(bh);

	parent_info->dir_children_count++;
	assoofs_sb->inodes_count++;
	assoofs_sb->free_inodes--;

	d_instantiate(dentry, inode);

	printk(KERN_INFO "File %s created successfully\n", dentry->d_name.name);
	return 0;
}
static int assoofs_mkdir(struct mnt_idmap *idmap, struct inode *dir, struct dentry *dentry, umode_t mode) {
	struct super_block *sb = dir->i_sb;
	struct assoofs_super_block_info *assoofs_sb = sb->s_fs_info;
	struct assoofs_inode_info *parent_info = dir->i_private;
	struct inode *inode;
	struct assoofs_inode_info *inode_info;
	struct buffer_head *bh;
	struct assoofs_dir_record_entry *record;

	printk(KERN_INFO "assoofs_mkdir called\n");

	if (assoofs_sb->free_inodes == 0) {
    	printk(KERN_ERR "No free inodes\n");
    	return -ENOSPC;
	}

	inode = new_inode(sb);
	inode->i_ino = parent_info->dir_children_count + 1;
	inode->i_sb = sb;
	inode_init_owner(&nop_mnt_idmap, inode, dir, S_IFDIR | mode);

	inode->i_op = &assoofs_inode_ops;
	inode->i_fop = &assoofs_dir_operations;

	inode_info = kzalloc(sizeof(struct assoofs_inode_info), GFP_KERNEL);
	inode_info->inode_no = inode->i_ino;
	inode_info->mode = S_IFDIR | mode;
	inode_info->file_size = 0;
	inode_info->data_block_number = ASSOOFS_LAST_RESERVED_BLOCK + inode->i_ino;

	inode->i_private = inode_info;

	// Añadir entrada en el directorio
	bh = sb_bread(sb, parent_info->data_block_number);
	record = (struct assoofs_dir_record_entry *)bh->b_data;
	record += parent_info->dir_children_count;

	strcpy(record->filename, dentry->d_name.name);
	record->inode_no = inode_info->inode_no;
	record->entry_removed = ASSOOFS_FALSE;

	mark_buffer_dirty(bh);
	brelse(bh);

	parent_info->dir_children_count++;
	assoofs_sb->inodes_count++;
	assoofs_sb->free_inodes--;

	d_instantiate(dentry, inode);

	printk(KERN_INFO "Directory %s created successfully\n", dentry->d_name.name);
	return 0;
}

struct dentry *assoofs_lookup(struct inode *parent_inode, struct dentry *child_dentry, unsigned int flags) {
	printk(KERN_INFO "assoofs_lookup called for name: %s\n", child_dentry->d_name.name);

	// Por ahora no vamos a buscar nada realmente
	return NULL;
}
static ssize_t assoofs_read(struct file *filp, char __user *buf, size_t len, loff_t *ppos) {
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct assoofs_inode_info *inode_info = inode->i_private;
	struct buffer_head *bh;
	char *data;
	ssize_t ret;

	printk(KERN_INFO "assoofs_read called\n");

	if (*ppos >= inode_info->file_size)
    	return 0;

	bh = sb_bread(sb, inode_info->data_block_number);
	data = (char *)bh->b_data;

	if (len > inode_info->file_size - *ppos)
  	len = inode_info->file_size - *ppos;

	if (copy_to_user(buf, data + *ppos, len)) {
    	brelse(bh);
    	return -EFAULT;
	}

	brelse(bh);
	*ppos += len;
	ret = len;

	return ret;
}
static ssize_t assoofs_write(struct file *filp, const char __user *buf, size_t len, loff_t *ppos) {
	struct inode *inode = file_inode(filp);
	struct super_block *sb = inode->i_sb;
	struct assoofs_inode_info *inode_info = inode->i_private;
	struct buffer_head *bh;
	char *data;
	ssize_t ret;

	printk(KERN_INFO "assoofs_write called\n");

	bh = sb_bread(sb, inode_info->data_block_number);
	data = (char *)bh->b_data;

	if (copy_from_user(data + *ppos, buf, len)) {
    	brelse(bh);
    	return -EFAULT;
	}

	mark_buffer_dirty(bh);
	brelse(bh);

	*ppos += len;
	inode_info->file_size = *ppos;
	inode->i_size = inode_info->file_size;

	mark_inode_dirty(inode);

	ret = len;
	return ret;
}
 

module_init(assoofs_init);
module_exit(assoofs_exit);


